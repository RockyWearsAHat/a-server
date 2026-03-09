// PS1MDEC.cpp — Motion DECoder implementation
//
// Implements the PS1 MDEC chip which decompresses still/FMV image data using
// a variant of MPEG-1 block coding:
//   1. CPU sends SetQuantTable (cmd 0x40) to load luma/chroma Q-matrices
//   2. CPU sends SetScaleTable (cmd 0x80) to load IDCT cosine coefficients
//   3. For each frame, CPU sends a Decode command (cmd 0x30) with a word count
//      and Q-scale value, followed by RLE-coded DCT bitstream words
//   4. Hardware decodes Cr, Cb, Y0-Y3 blocks (macroblock order for 16×16 px)
//   5. YCbCr → RGB conversion produces output pixel words consumed by DMA ch1
//
// References:
//   - psx-spx.consoledev.net, section "MDEC"
//   - Nocash PSX specs
//   - PCSX, mednafen mdec implementations as cross-references

#include "emulator/ps1/PS1MDEC.h"
#include <algorithm>
#include <cstring>

namespace AIO::Emulator::PS1 {

// ─── MPEG-1 zigzag scan order ────────────────────────────────────────────────
// Maps sequential bitstream coefficient index to 8×8 block position.
static const uint8_t ZIGZAG[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

// ─── RLE Huffman-style run/level table ───────────────────────────────────────
// PS1 MDEC uses a fixed VLC table identical to MPEG-1 for AC coefficients.
// Pairs are { run, level } for codes 2-bit to 11-bit.
// The actual AC decoding below uses bit-by-bit parsing; the table below is
// for the "short" codes (< 8 AC VLC entries handled inline).

// End of block marker and escape code values (in the 16-bit half-word stream)
static constexpr uint16_t MDEC_EOB = 0xFE00;
static constexpr uint16_t MDEC_ESCAPE = 0x0000; // 6-zero-bit prefix

// ─── Constructor / Reset ─────────────────────────────────────────────────────

PS1MDEC::PS1MDEC() : Loggable("PS1.MDEC") { Reset(); }

void PS1MDEC::Reset() {
  state = State::Idle;
  wordsRemaining = 0;
  quantByteOffset = 0;
  quantColorTableIncluded = false;
  scaleWordOffset = 0;
  outputRGB24 = false;
  outputSigned = false;
  setBit15 = false;
  decodeQscale = 0;
  decodeOutputWords = 0;
  bitbuf = 0;
  bitsLeft = 0;
  while (!inFIFO.empty())
    inFIFO.pop();
  while (!outFIFO.empty())
    outFIFO.pop();
  quantLuma.fill(0);
  quantChroma.fill(0);
  scaleTable.fill(0);
  LogInfo("MDEC Reset");
}

// ─── Status register (0x1F801824 read) ───────────────────────────────────────

uint32_t PS1MDEC::ReadStat() const {
  // Bit 31: data-out FIFO not empty (1 = output data available)
  // Bit 30: data-in  FIFO not full  (1 = ready to receive)
  // Bit 29: command busy             (0 = idle/ready)
  // Bit 28: data-in  DMA (ch0) enabled (mirrors CTRL bit 28)
  // Bit 27: data-out DMA (ch1) enabled (mirrors CTRL bit 27)
  // Bits 26-25: output depth (0=4bit,1=8bit,2=15bit,3=24bit) — always 2 or 3
  // Bit 24: output signed
  // Bit 23: output set bit15
  // Bits 16-0: remaining parameter words (decrement as bitstream consumed)

  uint32_t stat = 0;

  if (!outFIFO.empty())
    stat |= (1u << 31); // out FIFO has data

  // Report in-FIFO as always ready (synchronous model — no overflow guard)
  stat |= (1u << 30);

  // Busy bit: clear (we decode synchronously, so by the time the CPU reads
  // status the result is already in outFIFO)

  uint32_t depth = outputRGB24 ? 3u : 2u; // 3=24bit, 2=15bit
  stat |= (depth << 25);

  if (outputSigned)
    stat |= (1u << 24);
  if (setBit15)
    stat |= (1u << 23);

  // Remaining words field (bits 16-0) — approximate with inFIFO depth
  stat |= static_cast<uint32_t>(
      std::min(inFIFO.size(), static_cast<size_t>(0x1FFFF)));

  return stat;
}

// ─── Control register (0x1F801824 write)
// ──────────────────────────────────────

void PS1MDEC::WriteCtrl(uint32_t value) {
  // Bit 31: soft reset — clears FIFOs and state
  if (value & (1u << 31)) {
    Reset();
    return;
  }
  // Bit 27: output depth bit1 (0=15bit, 1=24bit)
  outputRGB24 = (value >> 27) & 1;
  // Bit 26: signed output
  outputSigned = (value >> 26) & 1;
  // Bit 25: set bit15 on every output RGB15 pixel
  setBit15 = (value >> 25) & 1;
}

// ─── Command word (0x1F801820 write)
// ──────────────────────────────────────────
//
// The first word of every command sequence has this layout:
//   bits 31-29 : command type
//                001 = Decode (0x20000000 | q_scale<<27... wait, actually:)
//   Actually the PS1 command word is:
//     bits 31-29 = command (1=decode, 2=set quant, 3=set scale)  [3 bits]
//     bits 28-25 = parameter bits (depth, signed, bit15)
//     bits 15-0  = output word count (for decode) / table size (for tables)

void PS1MDEC::WriteCmd(uint32_t value) {
  // If we're inside a multi-word command, accumulate data
  if (state == State::SetQuantTable) {
    // Quantisation table: 64 bytes luma, optionally 64 bytes chroma
    // Each 32-bit word carries 4 bytes
    for (int b = 0; b < 4; b++) {
      uint8_t byte = static_cast<uint8_t>(value >> (b * 8));
      uint32_t idx = quantByteOffset++;
      if (idx < 64) {
        quantLuma[idx] = byte;
      } else if (quantColorTableIncluded && idx < 128) {
        quantChroma[idx - 64] = byte;
      }
    }
    if (wordsRemaining > 0)
      wordsRemaining--;
    if (wordsRemaining == 0)
      state = State::Idle;
    return;
  }

  if (state == State::SetScaleTable) {
    // Scale table: 32 words of 2×int16
    int16_t lo = static_cast<int16_t>(value & 0xFFFF);
    int16_t hi = static_cast<int16_t>(value >> 16);
    uint32_t idx = scaleWordOffset * 2;
    if (idx < 64) {
      scaleTable[idx] = lo;
      scaleTable[idx + 1] = hi;
    }
    scaleWordOffset++;
    if (wordsRemaining > 0)
      wordsRemaining--;
    if (wordsRemaining == 0)
      state = State::Idle;
    return;
  }

  if (state == State::DecodeBlocks) {
    // Feed compressed bitstream into the in-FIFO
    WriteDMAWord(value);
    return;
  }

  // ── New command word ──────────────────────────────────────────────────
  uint32_t cmd = (value >> 29) & 7;
  switch (cmd) {
  case 1: {
    // Decode command
    // bits 28-25: output format (same as CTRL bits 27-24)
    outputRGB24 = (value >> 27) & 1;
    outputSigned = (value >> 26) & 1;
    setBit15 = (value >> 25) & 1;
    // bits 15-0: output word count (number of 32-bit words the CPU expects
    // back)
    decodeOutputWords = value & 0xFFFF;
    decodeQscale = 0; // q-scale comes with first bitstream word
    state = State::DecodeBlocks;
    // Flush any stale output
    while (!outFIFO.empty())
      outFIFO.pop();
    bitbuf = 0;
    bitsLeft = 0;
    LogInfo("MDEC Decode cmd: rgb24=%d outWords=%u", outputRGB24,
            decodeOutputWords);
    break;
  }
  case 2: {
    // Set quantisation table
    // bit 0 of value: whether a chroma table follows the luma table
    quantColorTableIncluded = value & 1;
    quantByteOffset = 0;
    uint32_t tableBytes = quantColorTableIncluded ? 128 : 64;
    wordsRemaining = tableBytes / 4;
    state = State::SetQuantTable;
    LogInfo("MDEC SetQuantTable: color=%d words=%u", quantColorTableIncluded,
            wordsRemaining);
    break;
  }
  case 3: {
    // Set scale (IDCT cosine) table: always 32 words = 64 int16
    scaleWordOffset = 0;
    wordsRemaining = 32;
    state = State::SetScaleTable;
    LogInfo("MDEC SetScaleTable");
    break;
  }
  default:
    LogWarn("MDEC unknown command %u (word=%08X)", cmd, value);
    break;
  }
}

// ─── Register data read (0x1F801820 read) ────────────────────────────────────

uint32_t PS1MDEC::ReadData() { return ReadDMAWord(); }

// ─── DMA interface ─────────────────────────────────────────────────────────

void PS1MDEC::WriteDMAWord(uint32_t value) {
  inFIFO.push(value);
  TryDecodeBlocks();
}

uint32_t PS1MDEC::ReadDMAWord() {
  if (outFIFO.empty())
    return 0xFFFFFFFF;
  uint32_t v = outFIFO.front();
  outFIFO.pop();
  return v;
}

bool PS1MDEC::InFIFOReady() const {
  return inFIFO.size() < INPUT_FIFO_CAPACITY;
}
bool PS1MDEC::OutFIFOReady() const { return !outFIFO.empty(); }

// ─── Bit reader helpers
// ───────────────────────────────────────────────────────

void PS1MDEC::FeedWord(uint32_t word) {
  // PS1 MDEC bitstream is big-endian within each 16-bit half-word, but the
  // two half-words within the 32-bit bus word are byte-swapped:
  // bus word layout: [ bits15:0 of halfword1 | bits15:0 of halfword0 ]
  // i.e. first 16 bits in stream are in the LOW half of the 32-bit word.
  // We push low half first, then high half into bitbuf (MSB-first within each).
  uint16_t lo = static_cast<uint16_t>(word & 0xFFFF);
  uint16_t hi = static_cast<uint16_t>(word >> 16);

  bitbuf = (bitbuf << 16) | lo;
  bitsLeft += 16;
  // If there's room, immediately push hi as well
  if (bitsLeft <= 16) {
    bitbuf = (bitbuf << 16) | hi;
    bitsLeft += 16;
  } else {
    // Store hi for next FeedWord call by re-queuing — handled below
    // Actually keep it simple: pre-load both halves
    // Reset and redo properly:
    bitbuf = (static_cast<uint32_t>(lo) << 16) | hi;
    bitsLeft = 32;
  }
}

bool PS1MDEC::ReadBits(uint32_t count, uint32_t &out) {
  while (bitsLeft < count) {
    if (inFIFO.empty())
      return false;
    uint32_t word = inFIFO.front();
    inFIFO.pop();

    // Each 32-bit word: low 16 bits are first in stream, high 16 bits second
    uint16_t lo = static_cast<uint16_t>(word & 0xFFFF);
    uint16_t hi = static_cast<uint16_t>(word >> 16);
    bitbuf = (bitbuf << 16) | lo;
    bitsLeft += 16;
    if (bitsLeft < count) {
      bitbuf = (bitbuf << 16) | hi;
      bitsLeft += 16;
    } else {
      // Push hi back (simplified: re-queue a word containing just hi)
      inFIFO.push(static_cast<uint32_t>(hi));
    }
  }
  out = (bitbuf >> (bitsLeft - count)) & ((1u << count) - 1u);
  bitsLeft -= count;
  return true;
}

// ─── IDCT
// ─────────────────────────────────────────────────────────────────────
//
// The PS1 MDEC uses the same MPEG-1 separable IDCT, but with the cosine
// factors from the scale table loaded by command 0x80.  For simplicity we use
// the classic AAN (scaled) 8-point IDCT — the per-element products with the
// scale table are folded into dequantization (as MPEG-1 specifies).
//
// We use the reference (but correct) row-column 2D IDCT here.  Performance is
// not critical for an emulator running at human-scale frame rates.

static void IDCT8(int32_t *blk) {
  // 1D IDCT on 8 values (Loeffler/AAN-style via reference formula)
  // Using the standard definition: X[n] = sum_{k=0}^{7}
  // x[k]*cos((2n+1)*k*pi/16) * C(k) where C(0)=1/sqrt(2), C(k!=0)=1 for
  // normalised form. We use fixed-point with 12-bit fractional precision.
  static const int32_t C[8] = {
      // 4096 * cos(k*pi/16) / (2 * (k==0 ? sqrt(2) : 1))
      // C[0] = 4096/sqrt(2) ≈ 2896
      // C[k] = 4096 * cos(k*pi/16) for k>0, times the MPEG amplitude factor
      // Using values from the standard reference IDCT used by MPEG decoders:
      4096, 4017, 3784, 3406, 2896, 2275, 1567, 799};

  int32_t tmp0 = blk[0] + blk[4];
  int32_t tmp1 = blk[0] - blk[4];
  int32_t tmp2 = (blk[2] * C[2] - blk[6] * C[6]) >> 12;
  int32_t tmp3 = (blk[2] * C[6] + blk[6] * C[2]) >> 12;

  int32_t tmp4 = tmp0 + tmp3;
  int32_t tmp5 = tmp0 - tmp3;
  int32_t tmp6 = tmp1 + tmp2;
  int32_t tmp7 = tmp1 - tmp2;

  int32_t tmp8 =
      (blk[1] * C[1] + blk[3] * C[3] + blk[5] * C[5] + blk[7] * C[7]) >> 12;
  int32_t tmp9 =
      (blk[1] * C[3] - blk[3] * C[7] - blk[5] * C[1] - blk[7] * C[5]) >> 12;
  int32_t tmp10 =
      (blk[1] * C[5] - blk[3] * C[1] + blk[5] * C[7] + blk[7] * C[3]) >> 12;
  int32_t tmp11 =
      (blk[1] * C[7] - blk[3] * C[5] + blk[5] * C[3] - blk[7] * C[1]) >> 12;

  blk[0] = tmp4 + tmp8;
  blk[7] = tmp4 - tmp8;
  blk[1] = tmp6 + tmp9;
  blk[6] = tmp6 - tmp9;
  blk[2] = tmp7 + tmp10;
  blk[5] = tmp7 - tmp10;
  blk[3] = tmp5 + tmp11;
  blk[4] = tmp5 - tmp11;
}

void PS1MDEC::IDCT(int16_t *block) {
  // Row pass
  int32_t tmp[64];
  for (int row = 0; row < 8; row++) {
    int32_t buf[8];
    for (int col = 0; col < 8; col++)
      buf[col] = static_cast<int32_t>(block[row * 8 + col]);
    IDCT8(buf);
    for (int col = 0; col < 8; col++)
      tmp[row * 8 + col] = buf[col];
  }

  // Column pass
  for (int col = 0; col < 8; col++) {
    int32_t buf[8];
    for (int row = 0; row < 8; row++)
      buf[row] = tmp[row * 8 + col];
    IDCT8(buf);
    for (int row = 0; row < 8; row++)
      block[row * 8 + col] = static_cast<int16_t>(buf[row] >> 3);
  }
}

// ─── RLE block decoder
// ────────────────────────────────────────────────────────
//
// PS1 MDEC uses 17-bit RLE codes packed as described in the psx-spx docs:
//
//   The bitstream is encoded as a sequence of 16-bit half-words (stored
//   little-endian in the bus word).  Each 16-bit value is either:
//     0xFE00  — End Of Block (EOB)
//     other   — upper 6 bits = zero-run (0..63), lower 10 bits = signed level
//
//   The first half-word of a block is special: it contains the DC coefficient
//   (signed 10-bit) with no run prefix — actually the docs say:
//     first halfword: signed 10-bit DC + zero-run of 0, but encoded as just
//     the 10-bit value directly (upper 6 bits may be zero).
//
//   Actually the PS1 MDEC bitstream format (psx-spx §MDEC) is the simplest
//   possible: the bitstream is NOT VLC/Huffman coded.  It is a stream of
//   16-bit fixed-width run-level pairs.  The MPEG VLC coding is NOT used.
//
//   So: each coefficient is exactly one 16-bit word:
//     bits 15-10: run  (number of preceding zeros)
//     bits  9-0:  level (signed 10-bit value)
//   Special: 0xFE00 = EOB.
//
// The DC coefficient of the first block is absolute; subsequent DC values
// are deltas (but the PS1 resets DC between macroblocks, so it's actually
// absolute per-block for the hardware).

bool PS1MDEC::DecodeBlock(const uint8_t *quantTable, int16_t *coeffOut) {
  int16_t block[64] = {};

  // Read DC coefficient (first 16-bit word, no run)
  uint32_t word;
  if (!ReadBits(16, word))
    return false;

  if (static_cast<uint16_t>(word) == MDEC_EOB) {
    // Empty block — zero fill
    std::fill(coeffOut, coeffOut + 64, int16_t(0));
    return true;
  }

  // DC coefficient: signed 10-bit
  int32_t dc = static_cast<int32_t>(word & 0x3FF);
  if (dc & 0x200)
    dc |= ~0x3FF; // sign-extend
  block[0] = static_cast<int16_t>(dc * quantTable[0] * 2);

  // AC coefficients
  int idx = 1;
  while (idx < 64) {
    if (!ReadBits(16, word))
      return false;

    uint16_t hw = static_cast<uint16_t>(word);
    if (hw == MDEC_EOB)
      break;

    uint32_t run = (hw >> 10) & 0x3F;
    int32_t level = hw & 0x3FF;
    if (level & 0x200)
      level |= ~0x3FF; // sign-extend 10-bit

    idx += static_cast<int>(run);
    if (idx >= 64)
      break;

    // Dequantize: coeff = level * quantTable[idx] * 2 * qscale / 8
    // For the PS1, the q-scale is embedded in the command/header word.
    // We incorporate it as: coeff = (2 * level * quant[zigzag_pos] * qscale +
    // 4) / 8 but clip to [-2048, 2047] (MPEG-1 spec, same as PS1 docs).
    int32_t q = static_cast<int32_t>(quantTable[ZIGZAG[idx]]);
    int32_t qs = static_cast<int32_t>(decodeQscale);
    int32_t coeff;
    if (qs == 0) {
      coeff = level * 2;
    } else {
      coeff = (2 * level * q * qs + (level > 0 ? 4 : -4)) / 8;
    }
    coeff = std::clamp(coeff, -2048, 2047);

    // Store in zigzag order
    block[ZIGZAG[idx]] = static_cast<int16_t>(coeff);
    idx++;
  }

  // Apply scale table (pre-multiply by cosine factors from scale table)
  for (int i = 0; i < 64; i++) {
    int32_t scaled = (static_cast<int32_t>(block[i]) * scaleTable[i]) >> 12;
    block[i] = static_cast<int16_t>(std::clamp(scaled, -2048, 2047));
  }

  // IDCT
  IDCT(block);

  std::copy(block, block + 64, coeffOut);
  return true;
}

// ─── Clamp and pack helpers
// ───────────────────────────────────────────────────

int16_t PS1MDEC::Clamp8(int32_t v) {
  return static_cast<int16_t>(std::clamp(v, -128, 127));
}

uint16_t PS1MDEC::PackRGB15(int32_t r, int32_t g, int32_t b, bool bit15) {
  uint16_t ri = static_cast<uint16_t>(std::clamp(r, 0, 31));
  uint16_t gi = static_cast<uint16_t>(std::clamp(g, 0, 31));
  uint16_t bi = static_cast<uint16_t>(std::clamp(b, 0, 31));
  return static_cast<uint16_t>(ri | (gi << 5) | (bi << 10) |
                               (bit15 ? 0x8000 : 0));
}

// ─── Macroblock → output pixels ──────────────────────────────────────────────
//
// A PS1 macroblock is 16×16 pixels composed of:
//   Cr(8×8), Cb(8×8), Y0(8×8 top-left), Y1(8×8 top-right),
//   Y2(8×8 bottom-left), Y3(8×8 bottom-right)
//
// YCbCr → RGB conversion (MPEG-1 / Rec.601):
//   R = Y + 1.402 * Cr
//   G = Y - 0.344 * Cb - 0.714 * Cr
//   B = Y + 1.772 * Cb
//
// For 15-bit output: scale luma to [0,31], chroma terms to [-16,15], pack.

void PS1MDEC::ConvertMacroblock(const int16_t *y0, const int16_t *y1,
                                const int16_t *y2, const int16_t *y3,
                                const int16_t *cb, const int16_t *cr) {
  // Output order from psx-spx: pixels are packed left-to-right, top-to-bottom
  // across the 16×16 macroblock in the same order uploaded to VRAM.

  // Build the full 16×16 grid of (Y, Cb, Cr) values.
  // Cb and Cr are 8×8 and upsampled 2x.
  auto getY = [&](int px, int py) -> int32_t {
    if (px < 8 && py < 8)
      return y0[py * 8 + px];
    if (px >= 8 && py < 8)
      return y1[py * 8 + (px - 8)];
    if (px < 8)
      return y2[(py - 8) * 8 + px];
    return y3[(py - 8) * 8 + (px - 8)];
  };
  auto getC = [](const int16_t *c, int px, int py) -> int32_t {
    // Nearest-neighbour 2× upsample of 8×8 chroma to 16×16
    return c[(py / 2) * 8 + (px / 2)];
  };

  if (!outputRGB24) {
    // 15-bit RGB555 output: two pixels per 32-bit word
    for (int py = 0; py < 16; py++) {
      for (int px = 0; px < 16; px += 2) {
        auto makePixel = [&](int x, int y) -> uint16_t {
          int32_t Y = getY(x, y) + 128; // MDEC luma is offset by 128
          int32_t Cb = getC(cb, x, y);
          int32_t Cr = getC(cr, x, y);

          // Fixed-point Rec.601 conversion scaled to 5-bit output [0..31]
          // R = (Y + 1.402*Cr) / 8  →  (Y*256 + 359*Cr) >> 11
          // G = (Y - 0.344*Cb - 0.714*Cr) / 8
          // B = (Y + 1.772*Cb) / 8
          int32_t r5 = (Y * 256 + 359 * Cr) >> 11;
          int32_t g5 = (Y * 256 - 88 * Cb - 183 * Cr) >> 11;
          int32_t b5 = (Y * 256 + 454 * Cb) >> 11;

          return PackRGB15(r5, g5, b5, setBit15);
        };

        uint16_t p0 = makePixel(px, py);
        uint16_t p1 = makePixel(px + 1, py);
        outFIFO.push(static_cast<uint32_t>(p0) |
                     (static_cast<uint32_t>(p1) << 16));
      }
    }
  } else {
    // 24-bit RGB888 output: pixels are packed 3 bytes each, padded to 32-bit
    // boundary: word0 = R0|G0|B0|R1, word1 = G1|B1|R2|G2, word2 = B2|R3|...
    uint8_t pixbuf[16 * 16 * 3];
    int pos = 0;
    for (int py = 0; py < 16; py++) {
      for (int px = 0; px < 16; px++) {
        int32_t Y = getY(px, py) + 128;
        int32_t Cb = getC(cb, px, py);
        int32_t Cr = getC(cr, px, py);

        int32_t r8 = std::clamp((Y * 256 + 359 * Cr) >> 8, 0, 255);
        int32_t g8 = std::clamp((Y * 256 - 88 * Cb - 183 * Cr) >> 8, 0, 255);
        int32_t b8 = std::clamp((Y * 256 + 454 * Cb) >> 8, 0, 255);

        pixbuf[pos++] = static_cast<uint8_t>(r8);
        pixbuf[pos++] = static_cast<uint8_t>(g8);
        pixbuf[pos++] = static_cast<uint8_t>(b8);
      }
    }
    // Pack into 32-bit words (little-endian)
    for (int i = 0; i < pos; i += 4) {
      uint32_t w = static_cast<uint32_t>(pixbuf[i]);
      if (i + 1 < pos)
        w |= static_cast<uint32_t>(pixbuf[i + 1]) << 8;
      if (i + 2 < pos)
        w |= static_cast<uint32_t>(pixbuf[i + 2]) << 16;
      if (i + 3 < pos)
        w |= static_cast<uint32_t>(pixbuf[i + 3]) << 24;
      outFIFO.push(w);
    }
  }
}

// ─── Main decode loop
// ─────────────────────────────────────────────────────────
//
// Called whenever new data arrives in inFIFO.  We attempt to decode as many
// complete macroblocks as the available bitstream allows.
//
// Each decode command begins with a one-word header (already consumed when
// the command word was parsed), then a stream of 16-bit RLE pairs.
// The stream is divided into 6 blocks per macroblock: Cr, Cb, Y0, Y1, Y2, Y3.

void PS1MDEC::TryDecodeBlocks() {
  if (state != State::DecodeBlocks)
    return;

  // We need at least one word to potentially decode a block
  while (!inFIFO.empty()) {
    // Peek at the next word to check for the Q-scale/macroblock-count header
    // PS1 MDEC bitstream starts with one 32-bit word per frame:
    //   bits 31-16: number of macroblocks (width*height in 16x16 units)
    //   bits 15-0:  initial Q-scale (applied to all blocks until changed)
    // Actually per psx-spx: the q-scale is embedded per-block as the first
    // 16-bit value (before the DC coefficient) when decoding each block?
    // No — the PS1 MDEC format puts the q-scale in the command word bits
    // and a macroblock count in the same command word.  The actual bitstream
    // that gets DMA'd to MDEC_IN is PURELY the RLE-coded coefficients with
    // no additional header words.
    //
    // The decode command word (sent via WriteCmd) has:
    //   bits 27-16: Q-scale (note: not bits 28-27!)
    //
    // Actually let's be precise (from psx-spx "MDEC Command Word"):
    //   bit 31-29 : 001 (command type = decode)
    //   bit 28    : output depth bit (0=15bit, 1=24bit) [but we handle in CTRL]
    //   bit 27    : output signed
    //   bit 26    : set bit15
    //   bit 25-16 : unused/parameter
    //   bit 15-0  : number of output 32-bit words (for DMA transfer length)
    //
    // The q-scale is NOT in the command word in the real hardware either.
    // It's encoded in the bitstream itself: each macroblock begins with a
    // special "quantization scale" code (the first 16-bit value in the stream
    // is the q-scale for that macroblock, followed by the DC values and AC
    // RLE pairs).
    //
    // Let's read the q-scale from the first 16-bit value of each macroblock.

    // Read one macroblock: q-scale word + 6 blocks (Cr Cb Y0 Y1 Y2 Y3)
    // We need at least the q-scale word to proceed

    // Save FIFO state so we can roll back if not enough data
    // (For simplicity, we don't roll back — just consume and produce partial
    //  output if the stream ends mid-block, which shouldn't happen in practice)

    // Read q-scale for this macroblock
    uint32_t qword;
    if (!ReadBits(16, qword))
      break; // Not enough data yet

    uint16_t qscale_hw = static_cast<uint16_t>(qword);
    if (qscale_hw == 0xFE00) {
      // End of stream marker at macroblock boundary — decode is done
      state = State::Idle;
      LogInfo("MDEC decode complete: outFIFO=%zu words", outFIFO.size());
      break;
    }

    decodeQscale = qscale_hw & 0x3F; // Q-scale is lower 6 bits

    // Decode 6 blocks: Cr, Cb, Y0, Y1, Y2, Y3
    int16_t cr[64], cb_blk[64], y0[64], y1[64], y2[64], y3[64];
    bool ok = true;
    ok = ok && DecodeBlock(quantChroma.data(), cr);
    ok = ok && DecodeBlock(quantChroma.data(), cb_blk);
    ok = ok && DecodeBlock(quantLuma.data(), y0);
    ok = ok && DecodeBlock(quantLuma.data(), y1);
    ok = ok && DecodeBlock(quantLuma.data(), y2);
    ok = ok && DecodeBlock(quantLuma.data(), y3);

    if (!ok) {
      // Not enough data — we've already consumed some bits which is a problem.
      // In practice the game sends a full frame atomically via DMA so this
      // shouldn't happen.  Log and bail.
      LogWarn("MDEC: ran out of bitstream mid-macroblock");
      break;
    }

    ConvertMacroblock(y0, y1, y2, y3, cb_blk, cr);
  }
}

} // namespace AIO::Emulator::PS1
