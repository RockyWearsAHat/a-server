#pragma once

#include "PS1Constants.h"
#include "emulator/common/Loggable.h"
#include <array>
#include <cstdint>
#include <queue>

namespace AIO::Emulator::PS1 {

// PS1 Motion DECoder (MDEC) — decompresses still images / FMV frames.
//
// The hardware pipeline is:
//   CPU/DMA ch0 → MDEC_IN FIFO  (compressed bitstream, 32-bit words)
//   MDEC decodes macroblocks in order: Cr, Cb, Y0, Y1, Y2, Y3 (16×16 block)
//   MDEC_OUT FIFO → DMA ch1     (decoded pixels, 32-bit words)
//
// Output pixel format depends on MDEC_CTRL bit 27:
//   0 = 15-bit RGB555 (two pixels per 32-bit word, used by Crash Bandicoot FMV)
//   1 = 24-bit RGB888 (packed, three bytes per pixel, padded to 32-bit)
//
// NOTE: This implementation is synchronous — no decode latency is modelled.
// The status bits correctly report FIFO ready/not-busy so polling loops
// terminate, but actual ~2000-cycle-per-macroblock timing is deferred.

class PS1MDEC : public Common::Loggable {
public:
  PS1MDEC();
  ~PS1MDEC() = default;

  void Reset();

  // ─── Register interface (CPU direct I/O) ────────────────────────────
  // 0x1F801820 write  — command word (first word sets decode mode when
  //                     upper 3 bits = 001; subsequent words are bitstream)
  void WriteCmd(uint32_t value);
  // 0x1F801820 read   — next decoded word from OUT FIFO
  uint32_t ReadData();
  // 0x1F801824 write  — control/reset register
  void WriteCtrl(uint32_t value);
  // 0x1F801824 read   — status register
  uint32_t ReadStat() const;

  // ─── DMA interface ───────────────────────────────────────────────────
  // DMA ch0 (MDEC_IN): feed one 32-bit compressed word
  void WriteDMAWord(uint32_t value);
  // DMA ch1 (MDEC_OUT): drain one 32-bit decoded word
  uint32_t ReadDMAWord();
  bool InFIFOReady() const;  // ch0 can write
  bool OutFIFOReady() const; // ch1 can read

private:
  // ─── Quantization and scale tables ──────────────────────────────────
  // Loaded by command 0x40 (SetQuantTable): 64 bytes luma + optional 64 bytes
  // chroma. Values are unsigned 8-bit Q-factors used during dequantization.
  std::array<uint8_t, 64> quantLuma{};
  std::array<uint8_t, 64> quantChroma{};

  // Scale table loaded by command 0x80 (SetScaleTable): 64 signed 16-bit
  // entries (MPEG-1 cosine coefficients) used during IDCT.
  std::array<int16_t, 64> scaleTable{};

  // ─── Decode state machine ────────────────────────────────────────────
  enum class State {
    Idle,
    SetQuantTable, // receiving quant table data
    SetScaleTable, // receiving scale table data
    DecodeBlocks,  // receiving compressed bitstream
  };

  State state = State::Idle;

  // Control register fields (written via 0x1F801824)
  bool outputRGB24 = false;  // bit 27 of CTRL: 0=15-bit, 1=24-bit
  bool outputSigned = false; // bit 26: colour data signed
  bool setBit15 = false;     // bit 25: set bit15 of every output RGB15 word

  // Words remaining to receive for current command
  uint32_t wordsRemaining = 0;

  // SetQuantTable: track how many bytes loaded so far
  uint32_t quantByteOffset = 0;
  bool quantColorTableIncluded = false;

  // SetScaleTable: 64 int16 values, arrives as 32 words
  uint32_t scaleWordOffset = 0;

  // ─── Decode context ─────────────────────────────────────────────────
  // Raw input words for the current block being decoded
  static constexpr uint32_t INPUT_FIFO_CAPACITY = 1024;
  static constexpr uint32_t OUTPUT_FIFO_CAPACITY = 1024;

  std::queue<uint32_t> inFIFO;  // compressed words waiting to be decoded
  std::queue<uint32_t> outFIFO; // decoded pixel words ready to be DMA'd

  // Current macroblock being built (Cr/Cb/Y0-Y3 order)
  // inFIFO words are consumed in DecodeFrame() once a full command arrives.

  uint32_t decodeQscale = 0;      // Q-scale prefix word from bitstream header
  uint32_t decodeOutputWords = 0; // expected output word count from header

  // ─── IDCT / decode helpers ───────────────────────────────────────────
  // Decode one bitstream block header word and all subsequent data words
  // currently buffered in inFIFO.  Produces decoded pixel words into outFIFO.
  void TryDecodeBlocks();

  // Decode one 8×8 block of RLE-compressed DCT coefficients from the bitstream.
  // Returns false if there is not enough data in inFIFO.
  bool DecodeBlock(const uint8_t *quantTable, int16_t *coeffOut);

  // Apply IDCT to an 8×8 block of DCT coefficients (in-place).
  void IDCT(int16_t *block);

  // Convert decoded YCbCr 16×16 macroblock to output pixel words.
  void ConvertMacroblock(const int16_t *y0, const int16_t *y1,
                         const int16_t *y2, const int16_t *y3,
                         const int16_t *cb, const int16_t *cr);

  // Bit-reader state for the current compressed stream
  uint32_t bitbuf = 0;
  uint32_t bitsLeft = 0;

  bool ReadBits(uint32_t count, uint32_t &out);
  void FeedWord(uint32_t word); // push word into bit-reader

  static int16_t Clamp8(int32_t v);
  static uint16_t PackRGB15(int32_t r, int32_t g, int32_t b, bool bit15);
};

} // namespace AIO::Emulator::PS1
