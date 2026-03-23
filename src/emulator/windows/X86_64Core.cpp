#include "emulator/windows/X86_64Core.h"
#include "emulator/windows/WinAPILayer.h"
#include "emulator/windows/WinMemory.h"

#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace AIO::Emulator::Windows {

// ─── Construction / Reset
// ─────────────────────────────────────────────────────

X86_64Core::X86_64Core(WinMemory &mem, WinAPILayer &api)
    : mem_(mem), api_(api) {
  Reset();
}

void X86_64Core::Reset() {
  gpr_.fill(0);
  rip_ = 0;
  rflags_ = 0x202; // IF set
  for (auto &x : xmm_) {
    x.lo = 0;
    x.hi = 0;
  }
  halted_ = false;
  faulted_ = false;
  faultMsg_.clear();
}

// ─── Fetch helpers
// ────────────────────────────────────────────────────────────

uint8_t X86_64Core::Fetch8() {
  uint8_t v = mem_.Read8(rip_);
  rip_++;
  return v;
}

uint16_t X86_64Core::Fetch16() {
  uint16_t v = mem_.Read16(rip_);
  rip_ += 2;
  return v;
}

uint32_t X86_64Core::Fetch32() {
  uint32_t v = mem_.Read32(rip_);
  rip_ += 4;
  return v;
}

uint64_t X86_64Core::Fetch64() {
  uint64_t v = mem_.Read64(rip_);
  rip_ += 8;
  return v;
}

// ─── Stack helpers
// ────────────────────────────────────────────────────────────

void X86_64Core::Push64(uint64_t v) {
  gpr_[4] -= 8;
  mem_.Write64(gpr_[4], v);
}

uint64_t X86_64Core::Pop64() {
  uint64_t v = mem_.Read64(gpr_[4]);
  gpr_[4] += 8;
  return v;
}

// ─── Size / sign helpers ─────────────────────────────────────────────────────

uint64_t X86_64Core::SizeMask(int size) const {
  switch (size) {
  case 1:
    return 0xFFull;
  case 2:
    return 0xFFFFull;
  case 4:
    return 0xFFFF'FFFFull;
  default:
    return 0xFFFF'FFFF'FFFF'FFFFull;
  }
}

bool X86_64Core::SignBit(uint64_t v, int size) const {
  return (v >> (size * 8 - 1)) & 1;
}

// ─── Flag updates
// ─────────────────────────────────────────────────────────────

void X86_64Core::UpdateFlagsLogical(uint64_t result, int size) {
  const uint64_t mask = SizeMask(size);
  result &= mask;
  rflags_ &= ~(kCF | kOF | kSF | kZF | kPF | kAF);
  if (result == 0)
    rflags_ |= kZF;
  if (SignBit(result, size))
    rflags_ |= kSF;
  // Parity of low byte
  uint8_t lo = static_cast<uint8_t>(result);
  lo ^= (lo >> 4);
  lo ^= (lo >> 2);
  lo ^= (lo >> 1);
  if (!(lo & 1))
    rflags_ |= kPF;
}

void X86_64Core::UpdateFlagsAdd(uint64_t a, uint64_t b, uint64_t r, int size) {
  const uint64_t mask = SizeMask(size);
  a &= mask;
  b &= mask;
  r &= mask;
  rflags_ &= ~(kCF | kOF | kSF | kZF | kPF | kAF);
  if (r < a)
    rflags_ |= kCF; // unsigned overflow
  if (r == 0)
    rflags_ |= kZF;
  if (SignBit(r, size))
    rflags_ |= kSF;
  // Overflow: both operands same sign, result different sign
  const bool sa = SignBit(a, size), sb = SignBit(b, size),
             sr = SignBit(r, size);
  if (sa == sb && sa != sr)
    rflags_ |= kOF;
  if (((a ^ b ^ r) >> 4) & 1)
    rflags_ |= kAF;
  uint8_t lo = static_cast<uint8_t>(r);
  lo ^= (lo >> 4);
  lo ^= (lo >> 2);
  lo ^= (lo >> 1);
  if (!(lo & 1))
    rflags_ |= kPF;
}

void X86_64Core::UpdateFlagsSub(uint64_t a, uint64_t b, uint64_t r, int size) {
  const uint64_t mask = SizeMask(size);
  a &= mask;
  b &= mask;
  r &= mask;
  rflags_ &= ~(kCF | kOF | kSF | kZF | kPF | kAF);
  if (b > a)
    rflags_ |= kCF; // borrow
  if (r == 0)
    rflags_ |= kZF;
  if (SignBit(r, size))
    rflags_ |= kSF;
  // Overflow: operands different sign, result sign differs from a
  const bool sa = SignBit(a, size), sb = SignBit(b, size),
             sr = SignBit(r, size);
  if (sa != sb && sa != sr)
    rflags_ |= kOF;
  if (((a ^ b ^ r) >> 4) & 1)
    rflags_ |= kAF;
  uint8_t lo = static_cast<uint8_t>(r);
  lo ^= (lo >> 4);
  lo ^= (lo >> 2);
  lo ^= (lo >> 1);
  if (!(lo & 1))
    rflags_ |= kPF;
}

// ─── ALU operations ──────────────────────────────────────────────────────────

uint64_t X86_64Core::DoAdd(uint64_t a, uint64_t b, int size) {
  const uint64_t mask = SizeMask(size);
  const uint64_t r = (a + b) & mask;
  UpdateFlagsAdd(a, b, r, size);
  return r;
}

uint64_t X86_64Core::DoSub(uint64_t a, uint64_t b, int size) {
  const uint64_t mask = SizeMask(size);
  const uint64_t r = (a - b) & mask;
  UpdateFlagsSub(a, b, r, size);
  return r;
}

uint64_t X86_64Core::DoAnd(uint64_t a, uint64_t b, int size) {
  const uint64_t r = (a & b) & SizeMask(size);
  UpdateFlagsLogical(r, size);
  return r;
}

uint64_t X86_64Core::DoOr(uint64_t a, uint64_t b, int size) {
  const uint64_t r = (a | b) & SizeMask(size);
  UpdateFlagsLogical(r, size);
  return r;
}

uint64_t X86_64Core::DoXor(uint64_t a, uint64_t b, int size) {
  const uint64_t r = (a ^ b) & SizeMask(size);
  UpdateFlagsLogical(r, size);
  return r;
}

uint64_t X86_64Core::DoShl(uint64_t v, uint8_t cnt, int size) {
  if (cnt == 0)
    return v & SizeMask(size);
  const uint64_t mask = SizeMask(size);
  const int bits = size * 8;
  cnt &= (size == 8 ? 0x3F : 0x1F);
  if (cnt == 0)
    return v & mask;
  const uint64_t r = (v << cnt) & mask;
  // CF = last bit shifted out
  rflags_ &= ~kCF;
  if ((v >> (bits - cnt)) & 1)
    rflags_ |= kCF;
  UpdateFlagsLogical(r, size);
  if (cnt == 1) {
    rflags_ &= ~kOF;
    if (SignBit(r, size) != ((rflags_ & kCF) != 0))
      rflags_ |= kOF;
  }
  return r;
}

uint64_t X86_64Core::DoShr(uint64_t v, uint8_t cnt, int size) {
  if (cnt == 0)
    return v & SizeMask(size);
  const uint64_t mask = SizeMask(size);
  v &= mask;
  cnt &= (size == 8 ? 0x3F : 0x1F);
  if (cnt == 0)
    return v;
  rflags_ &= ~kCF;
  if ((v >> (cnt - 1)) & 1)
    rflags_ |= kCF;
  const uint64_t r = v >> cnt;
  UpdateFlagsLogical(r, size);
  if (cnt == 1) {
    rflags_ &= ~kOF;
    if (SignBit(v, size))
      rflags_ |= kOF; // OF = MSB of original
  }
  return r;
}

uint64_t X86_64Core::DoSar(uint64_t v, uint8_t cnt, int size) {
  if (cnt == 0)
    return v & SizeMask(size);
  const uint64_t mask = SizeMask(size);
  v &= mask;
  cnt &= (size == 8 ? 0x3F : 0x1F);
  if (cnt == 0)
    return v;
  rflags_ &= ~kCF;
  if ((v >> (cnt - 1)) & 1)
    rflags_ |= kCF;
  const int bits = size * 8;
  int64_t sv;
  switch (size) {
  case 1:
    sv = static_cast<int8_t>(v);
    break;
  case 2:
    sv = static_cast<int16_t>(v);
    break;
  case 4:
    sv = static_cast<int32_t>(v);
    break;
  default:
    sv = static_cast<int64_t>(v);
    break;
  }
  const uint64_t r = static_cast<uint64_t>(sv >> cnt) & mask;
  UpdateFlagsLogical(r, size);
  if (cnt == 1)
    rflags_ &= ~kOF; // SAR sets OF=0 for count==1
  return r;
}

uint64_t X86_64Core::DoRol(uint64_t v, uint8_t cnt, int size) {
  const int bits = size * 8;
  cnt %= bits;
  if (cnt == 0)
    return v & SizeMask(size);
  const uint64_t mask = SizeMask(size);
  v &= mask;
  const uint64_t r = ((v << cnt) | (v >> (bits - cnt))) & mask;
  rflags_ &= ~kCF;
  if (r & 1)
    rflags_ |= kCF;
  if (cnt == 1) {
    rflags_ &= ~kOF;
    if (SignBit(r, size) != ((rflags_ & kCF) != 0))
      rflags_ |= kOF;
  }
  return r;
}

uint64_t X86_64Core::DoRor(uint64_t v, uint8_t cnt, int size) {
  const int bits = size * 8;
  cnt %= bits;
  if (cnt == 0)
    return v & SizeMask(size);
  const uint64_t mask = SizeMask(size);
  v &= mask;
  const uint64_t r = ((v >> cnt) | (v << (bits - cnt))) & mask;
  rflags_ &= ~kCF;
  if (SignBit(r, size))
    rflags_ |= kCF;
  if (cnt == 1) {
    rflags_ &= ~kOF;
    if (SignBit(r, size) != SignBit(r << 1, size))
      rflags_ |= kOF;
  }
  return r;
}

// ─── Condition codes ─────────────────────────────────────────────────────────

bool X86_64Core::TestCC(uint8_t cc) const {
  switch (cc & 0xF) {
  case 0x0:
    return (rflags_ & kOF) != 0; // O
  case 0x1:
    return (rflags_ & kOF) == 0; // NO
  case 0x2:
    return (rflags_ & kCF) != 0; // B/C/NAE
  case 0x3:
    return (rflags_ & kCF) == 0; // AE/NB/NC
  case 0x4:
    return (rflags_ & kZF) != 0; // E/Z
  case 0x5:
    return (rflags_ & kZF) == 0; // NE/NZ
  case 0x6:
    return (rflags_ & (kCF | kZF)) != 0; // BE/NA
  case 0x7:
    return (rflags_ & (kCF | kZF)) == 0; // A/NBE
  case 0x8:
    return (rflags_ & kSF) != 0; // S
  case 0x9:
    return (rflags_ & kSF) == 0; // NS
  case 0xA:
    return (rflags_ & kPF) != 0; // P/PE
  case 0xB:
    return (rflags_ & kPF) == 0; // NP/PO
  case 0xC:
    return ((rflags_ & kSF) != 0) != ((rflags_ & kOF) != 0); // L/NGE
  case 0xD:
    return ((rflags_ & kSF) != 0) == ((rflags_ & kOF) != 0); // GE/NL
  case 0xE:
    return (rflags_ & kZF) != 0 ||
           ((rflags_ & kSF) != 0) != ((rflags_ & kOF) != 0); // LE/NG
  case 0xF:
    return (rflags_ & kZF) == 0 &&
           ((rflags_ & kSF) != 0) == ((rflags_ & kOF) != 0); // G/NLE
  }
  return false;
}

// ─── Register R/W
// ─────────────────────────────────────────────────────────────

uint64_t X86_64Core::ReadReg(int idx, int size, bool rex_present) const {
  if (size == 1 && !rex_present && idx >= 4 && idx <= 7) {
    // Legacy high-byte: AH(4) CH(5) DH(6) BH(7)
    return (gpr_[idx - 4] >> 8) & 0xFF;
  }
  return gpr_[idx & 15] & SizeMask(size);
}

void X86_64Core::WriteReg(int idx, int size, uint64_t value, bool rex_present) {
  if (size == 1 && !rex_present && idx >= 4 && idx <= 7) {
    const int realIdx = idx - 4;
    gpr_[realIdx] = (gpr_[realIdx] & ~0xFF00ull) | ((value & 0xFF) << 8);
    return;
  }
  const int n = idx & 15;
  if (size == 4) {
    // 32-bit writes zero-extend to 64-bit in x86-64
    gpr_[n] = value & 0xFFFF'FFFFull;
  } else if (size == 8) {
    gpr_[n] = value;
  } else {
    const uint64_t mask = SizeMask(size);
    gpr_[n] = (gpr_[n] & ~mask) | (value & mask);
  }
}

// ─── ModRM / SIB decode ──────────────────────────────────────────────────────

X86_64Core::MRM X86_64Core::DecodeModRM(const PfxState &pfx) {
  MRM m{};
  const uint8_t byte = Fetch8();
  m.mod = (byte >> 6) & 3;
  m.reg = (byte >> 3) & 7;
  m.rm = byte & 7;

  // Apply REX.R to reg field
  if (pfx.rex_r)
    m.reg |= 8;

  if (m.mod == 3) {
    // Register direct
    m.is_mem = false;
    if (pfx.rex_b)
      m.rm |= 8;
    return m;
  }

  m.is_mem = true;
  uint64_t base_val = 0;

  if (m.rm == 4) {
    // SIB byte follows
    const uint8_t sib = Fetch8();
    const uint8_t ss = (sib >> 6) & 3;
    uint8_t idx = (sib >> 3) & 7;
    uint8_t base = sib & 7;
    if (pfx.rex_x)
      idx |= 8;
    if (pfx.rex_b)
      base |= 8;

    // Base
    if (base == 5 && m.mod == 0) {
      base_val = static_cast<uint64_t>(FetchS32()); // disp32
    } else {
      base_val = gpr_[base];
    }

    // Index (index=4 means "no index" unless REX.X extends it)
    if (idx != 4) {
      base_val += gpr_[idx] << ss;
    }
  } else if (m.rm == 5 && m.mod == 0) {
    // RIP-relative
    const int32_t disp = FetchS32();
    m.ea = rip_ + static_cast<int64_t>(disp);
    m.rip_rel = true;
    return m;
  } else {
    uint8_t rm_ext = m.rm;
    if (pfx.rex_b)
      rm_ext |= 8;
    base_val = gpr_[rm_ext];
  }

  // Displacement
  if (m.mod == 1) {
    base_val += static_cast<int64_t>(FetchS8());
  } else if (m.mod == 2) {
    base_val += static_cast<int64_t>(FetchS32());
  }

  m.ea = base_val;
  return m;
}

// ─── ModRM read/write ────────────────────────────────────────────────────────

uint64_t X86_64Core::ReadRM(const MRM &mr, const PfxState &pfx,
                            int size) const {
  if (!mr.is_mem) {
    return ReadReg(mr.rm, size, pfx.has_rex);
  }
  switch (size) {
  case 1:
    return mem_.Read8(mr.ea);
  case 2:
    return mem_.Read16(mr.ea);
  case 4:
    return mem_.Read32(mr.ea);
  case 8:
    return mem_.Read64(mr.ea);
  }
  return 0;
}

void X86_64Core::WriteRM(const MRM &mr, const PfxState &pfx, int size,
                         uint64_t value) {
  if (!mr.is_mem) {
    WriteReg(mr.rm, size, value, pfx.has_rex);
    return;
  }
  switch (size) {
  case 1:
    mem_.Write8(mr.ea, static_cast<uint8_t>(value));
    break;
  case 2:
    mem_.Write16(mr.ea, static_cast<uint16_t>(value));
    break;
  case 4:
    mem_.Write32(mr.ea, static_cast<uint32_t>(value));
    break;
  case 8:
    mem_.Write64(mr.ea, value);
    break;
  }
}

// ─── Fault helpers
// ────────────────────────────────────────────────────────────

void X86_64Core::Fault(const std::string &msg) {
  faulted_ = true;
  faultMsg_ = msg;
  std::cerr << "[X86_64] FAULT at RIP=" << std::hex << rip_ << ": " << msg
            << "\n";
}

void X86_64Core::FaultUnknownOpcode(uint8_t op) {
  std::ostringstream ss;
  ss << "Unknown opcode 0x" << std::hex << std::setw(2) << std::setfill('0')
     << static_cast<int>(op) << " at RIP=0x" << rip_;
  Fault(ss.str());
}

// ─── Main execute loop ───────────────────────────────────────────────────────

int X86_64Core::Execute(int count) {
  int executed = 0;
  while (executed < count && !halted_ && !faulted_) {
    if (!ExecuteOne())
      break;
    ++executed;
  }
  return executed;
}

// ─── ExecuteOne ──────────────────────────────────────────────────────────────

bool X86_64Core::ExecuteOne() {
  if (halted_ || faulted_)
    return false;

  // Check if RIP is in a stub region — if so, dispatch to WinAPI
  if (api_.Dispatch(rip_)) {
    // The stub at that address is a RET (0xC3). Pop return address.
    rip_ = Pop64();
    return true;
  }

  // Sentinel for ExitProcess
  if (rip_ == 0xDEAD'BEEF'DEAD'BEEFull) {
    halted_ = true;
    return false;
  }

  const uint64_t inst_rip = rip_;

  // ── Prefix decoding ──────────────────────────────────────────────────
  PfxState pfx{};
  bool decoding_prefix = true;
  while (decoding_prefix) {
    const uint8_t b = Fetch8();
    switch (b) {
    case 0xF0:
      pfx.lock = true;
      break;
    case 0xF2:
      pfx.repne = true;
      break;
    case 0xF3:
      pfx.rep = true;
      break;
    case 0x66:
      pfx.op_size = true;
      break;
    case 0x67:
      pfx.addr_size = true;
      break;
    case 0x26:
    case 0x2E:
    case 0x36:
    case 0x3E:
    case 0x64:
    case 0x65:
      break; // segment override — ignored in 64-bit mode
    default:
      if ((b & 0xF0) == 0x40) {
        // REX prefix
        pfx.has_rex = true;
        pfx.rex_w = (b >> 3) & 1;
        pfx.rex_r = (b >> 2) & 1;
        pfx.rex_x = (b >> 1) & 1;
        pfx.rex_b = (b >> 0) & 1;
      } else {
        // Not a prefix — this is the opcode
        rip_ = rip_ - 1; // un-fetch
        decoding_prefix = false;
      }
      break;
    }
  }

  const uint8_t op = Fetch8();
  const int defSize = OpSize(pfx);

  switch (op) {
  // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  // ADD / OR / ADC / SBB / AND / SUB / XOR / CMP    (0x00–0x3F)
  // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  case 0x00:
  case 0x01:
  case 0x02:
  case 0x03:
  case 0x04:
  case 0x05:
  case 0x08:
  case 0x09:
  case 0x0A:
  case 0x0B:
  case 0x0C:
  case 0x0D:
  case 0x10:
  case 0x11:
  case 0x12:
  case 0x13:
  case 0x14:
  case 0x15:
  case 0x18:
  case 0x19:
  case 0x1A:
  case 0x1B:
  case 0x1C:
  case 0x1D:
  case 0x20:
  case 0x21:
  case 0x22:
  case 0x23:
  case 0x24:
  case 0x25:
  case 0x28:
  case 0x29:
  case 0x2A:
  case 0x2B:
  case 0x2C:
  case 0x2D:
  case 0x30:
  case 0x31:
  case 0x32:
  case 0x33:
  case 0x34:
  case 0x35:
  case 0x38:
  case 0x39:
  case 0x3A:
  case 0x3B:
  case 0x3C:
  case 0x3D: {
    const uint8_t aluOp =
        (op >> 3) & 7; // 0=ADD 1=OR 2=ADC 3=SBB 4=AND 5=SUB 6=XOR 7=CMP
    const uint8_t direction = op & 7; // see below
    // direction encoding:
    //  0: r/m8, reg8          (ModRM, size=1, dir=rm←op reg)
    //  1: r/m, reg            (ModRM, size=defSize, dir=rm←op reg)
    //  2: reg8, r/m8          (ModRM, size=1, dir=reg←op rm)
    //  3: reg, r/m            (ModRM, size=defSize, dir=reg←op rm)
    //  4: AL, imm8            (no ModRM, size=1)
    //  5: rAX, imm            (no ModRM, size=defSize)

    if (direction == 4 || direction == 5) {
      // AL/rAX, imm
      const int sz = (direction == 4) ? 1 : defSize;
      uint64_t imm = (sz == 1)   ? Fetch8()
                     : (sz == 2) ? Fetch16()
                                 : static_cast<uint64_t>(FetchS32());
      const uint64_t a = ReadReg(0, sz, pfx.has_rex);
      uint64_t r;
      switch (aluOp) {
      case 0:
        r = DoAdd(a, imm, sz);
        WriteReg(0, sz, r, pfx.has_rex);
        break;
      case 1:
        r = DoOr(a, imm, sz);
        WriteReg(0, sz, r, pfx.has_rex);
        break;
      case 2: {
        uint64_t cf = (rflags_ & kCF) ? 1 : 0;
        r = DoAdd(a, imm + cf, sz);
        WriteReg(0, sz, r, pfx.has_rex);
        break;
      }
      case 3: {
        uint64_t cf = (rflags_ & kCF) ? 1 : 0;
        r = DoSub(a, imm + cf, sz);
        WriteReg(0, sz, r, pfx.has_rex);
        break;
      }
      case 4:
        r = DoAnd(a, imm, sz);
        WriteReg(0, sz, r, pfx.has_rex);
        break;
      case 5:
        r = DoSub(a, imm, sz);
        WriteReg(0, sz, r, pfx.has_rex);
        break;
      case 6:
        r = DoXor(a, imm, sz);
        WriteReg(0, sz, r, pfx.has_rex);
        break;
      case 7:
        DoSub(a, imm, sz);
        break; // CMP — discard result
      }
      break;
    }

    const int sz = (direction & 1) ? defSize : 1;
    const bool regIsDst = (direction & 2);
    MRM mr = DecodeModRM(pfx);
    const uint64_t rmVal = ReadRM(mr, pfx, sz);
    const uint64_t regVal = ReadReg(mr.reg, sz, pfx.has_rex);

    uint64_t a, b;
    if (regIsDst) {
      a = regVal;
      b = rmVal;
    } else {
      a = rmVal;
      b = regVal;
    }

    uint64_t r;
    switch (aluOp) {
    case 0:
      r = DoAdd(a, b, sz);
      break;
    case 1:
      r = DoOr(a, b, sz);
      break;
    case 2: {
      uint64_t cf = (rflags_ & kCF) ? 1 : 0;
      r = DoAdd(a, b + cf, sz);
      break;
    }
    case 3: {
      uint64_t cf = (rflags_ & kCF) ? 1 : 0;
      r = DoSub(a, b + cf, sz);
      break;
    }
    case 4:
      r = DoAnd(a, b, sz);
      break;
    case 5:
      r = DoSub(a, b, sz);
      break;
    case 6:
      r = DoXor(a, b, sz);
      break;
    case 7:
      DoSub(a, b, sz);
      r = 0;
      break; // CMP
    default:
      r = 0;
      break;
    }
    if (aluOp != 7) { // not CMP
      if (regIsDst)
        WriteReg(mr.reg, sz, r, pfx.has_rex);
      else
        WriteRM(mr, pfx, sz, r);
    }
    break;
  }

  // ── PUSH reg (0x50–0x57) ────────────────────────────────────────────
  case 0x50:
  case 0x51:
  case 0x52:
  case 0x53:
  case 0x54:
  case 0x55:
  case 0x56:
  case 0x57: {
    int reg = (op - 0x50);
    if (pfx.rex_b)
      reg |= 8;
    Push64(gpr_[reg]);
    break;
  }

  // ── POP reg (0x58–0x5F) ─────────────────────────────────────────────
  case 0x58:
  case 0x59:
  case 0x5A:
  case 0x5B:
  case 0x5C:
  case 0x5D:
  case 0x5E:
  case 0x5F: {
    int reg = (op - 0x58);
    if (pfx.rex_b)
      reg |= 8;
    gpr_[reg] = Pop64();
    break;
  }

  // ── MOVSXD r64, r/m32 (0x63) ──────────────────────────────────────
  case 0x63: {
    MRM mr = DecodeModRM(pfx);
    int32_t val = static_cast<int32_t>(ReadRM(mr, pfx, 4));
    if (pfx.rex_w)
      WriteReg(mr.reg, 8, static_cast<uint64_t>(static_cast<int64_t>(val)),
               pfx.has_rex);
    else
      WriteReg(mr.reg, 4, static_cast<uint32_t>(val), pfx.has_rex);
    break;
  }

  // ── PUSH imm16/32 (0x68) ───────────────────────────────────────────
  case 0x68: {
    if (pfx.op_size)
      Push64(static_cast<uint64_t>(static_cast<int16_t>(Fetch16())));
    else
      Push64(static_cast<uint64_t>(static_cast<int64_t>(FetchS32())));
    break;
  }

  // ── IMUL r, r/m, imm (0x69, 0x6B) ─────────────────────────────────
  case 0x69:
  case 0x6B: {
    MRM mr = DecodeModRM(pfx);
    int64_t rmVal = 0;
    switch (defSize) {
    case 2:
      rmVal = static_cast<int16_t>(ReadRM(mr, pfx, 2));
      break;
    case 4:
      rmVal = static_cast<int32_t>(ReadRM(mr, pfx, 4));
      break;
    case 8:
      rmVal = static_cast<int64_t>(ReadRM(mr, pfx, 8));
      break;
    }
    int64_t imm;
    if (op == 0x6B)
      imm = static_cast<int64_t>(FetchS8());
    else {
      if (defSize == 2)
        imm = static_cast<int64_t>(FetchS16());
      else
        imm = static_cast<int64_t>(FetchS32());
    }
    const int64_t result = rmVal * imm;
    WriteReg(mr.reg, defSize, static_cast<uint64_t>(result), pfx.has_rex);
    // Set CF/OF if result truncated
    const uint64_t mask = SizeMask(defSize);
    const int64_t trunc =
        static_cast<int64_t>(static_cast<uint64_t>(result) & mask);
    rflags_ &= ~(kCF | kOF);
    if (trunc != result)
      rflags_ |= (kCF | kOF);
    break;
  }

  // ── PUSH imm8 (sign-extended) (0x6A) ───────────────────────────────
  case 0x6A: {
    Push64(static_cast<uint64_t>(static_cast<int64_t>(FetchS8())));
    break;
  }

  // ── Jcc short (0x70–0x7F) ──────────────────────────────────────────
  case 0x70:
  case 0x71:
  case 0x72:
  case 0x73:
  case 0x74:
  case 0x75:
  case 0x76:
  case 0x77:
  case 0x78:
  case 0x79:
  case 0x7A:
  case 0x7B:
  case 0x7C:
  case 0x7D:
  case 0x7E:
  case 0x7F: {
    const int8_t disp = FetchS8();
    if (TestCC(op & 0xF))
      rip_ = rip_ + static_cast<int64_t>(disp);
    break;
  }

  // ── Group 1: op r/m, imm  (0x80–0x83) ─────────────────────────────
  case 0x80:
  case 0x81:
  case 0x82:
  case 0x83: {
    const int sz = (op & 1) ? defSize : 1;
    MRM mr = DecodeModRM(pfx);
    uint64_t imm;
    if (op == 0x81) {
      if (sz == 2)
        imm = Fetch16();
      else
        imm = static_cast<uint64_t>(FetchS32());
    } else {
      imm = static_cast<uint64_t>(FetchS8());
    }
    ExecGroup1(pfx, mr, sz, mr.reg & 7);
    // We need to pass imm — let's inline:
    {
      const uint64_t a = ReadRM(mr, pfx, sz);
      uint64_t r;
      switch (mr.reg & 7) {
      case 0:
        r = DoAdd(a, imm, sz);
        WriteRM(mr, pfx, sz, r);
        break;
      case 1:
        r = DoOr(a, imm, sz);
        WriteRM(mr, pfx, sz, r);
        break;
      case 2: {
        uint64_t cf = (rflags_ & kCF) ? 1 : 0;
        r = DoAdd(a, imm + cf, sz);
        WriteRM(mr, pfx, sz, r);
        break;
      }
      case 3: {
        uint64_t cf = (rflags_ & kCF) ? 1 : 0;
        r = DoSub(a, imm + cf, sz);
        WriteRM(mr, pfx, sz, r);
        break;
      }
      case 4:
        r = DoAnd(a, imm, sz);
        WriteRM(mr, pfx, sz, r);
        break;
      case 5:
        r = DoSub(a, imm, sz);
        WriteRM(mr, pfx, sz, r);
        break;
      case 6:
        r = DoXor(a, imm, sz);
        WriteRM(mr, pfx, sz, r);
        break;
      case 7:
        DoSub(a, imm, sz);
        break; // CMP
      }
    }
    break;
  }

  // ── TEST r/m, reg (0x84, 0x85) ─────────────────────────────────────
  case 0x84:
  case 0x85: {
    const int sz = (op & 1) ? defSize : 1;
    MRM mr = DecodeModRM(pfx);
    DoAnd(ReadRM(mr, pfx, sz), ReadReg(mr.reg, sz, pfx.has_rex), sz);
    break;
  }

  // ── XCHG r/m, reg (0x86, 0x87) ─────────────────────────────────────
  case 0x86:
  case 0x87: {
    const int sz = (op & 1) ? defSize : 1;
    MRM mr = DecodeModRM(pfx);
    uint64_t a = ReadRM(mr, pfx, sz);
    uint64_t b = ReadReg(mr.reg, sz, pfx.has_rex);
    WriteRM(mr, pfx, sz, b);
    WriteReg(mr.reg, sz, a, pfx.has_rex);
    break;
  }

  // ── MOV r/m, reg (0x88, 0x89) ──────────────────────────────────────
  case 0x88:
  case 0x89: {
    const int sz = (op & 1) ? defSize : 1;
    MRM mr = DecodeModRM(pfx);
    WriteRM(mr, pfx, sz, ReadReg(mr.reg, sz, pfx.has_rex));
    break;
  }

  // ── MOV reg, r/m (0x8A, 0x8B) ──────────────────────────────────────
  case 0x8A:
  case 0x8B: {
    const int sz = (op & 1) ? defSize : 1;
    MRM mr = DecodeModRM(pfx);
    WriteReg(mr.reg, sz, ReadRM(mr, pfx, sz), pfx.has_rex);
    break;
  }

  // ── LEA reg, m (0x8D) ──────────────────────────────────────────────
  case 0x8D: {
    MRM mr = DecodeModRM(pfx);
    WriteReg(mr.reg, defSize, mr.ea, pfx.has_rex);
    break;
  }

  // ── POP r/m (0x8F) ─────────────────────────────────────────────────
  case 0x8F: {
    MRM mr = DecodeModRM(pfx);
    WriteRM(mr, pfx, 8, Pop64());
    break;
  }

  // ── NOP / XCHG rAX, reg (0x90–0x97) ────────────────────────────────
  case 0x90: {
    if (pfx.rex_b) {
      // XCHG RAX, R8
      std::swap(gpr_[0], gpr_[8]);
    }
    // else NOP
    break;
  }
  case 0x91:
  case 0x92:
  case 0x93:
  case 0x94:
  case 0x95:
  case 0x96:
  case 0x97: {
    int reg = (op - 0x90);
    if (pfx.rex_b)
      reg |= 8;
    std::swap(gpr_[0], gpr_[reg]);
    break;
  }

  // ── CBW/CWDE/CDQE (0x98) ───────────────────────────────────────────
  case 0x98: {
    if (pfx.rex_w) {
      // CDQE: sign-extend EAX to RAX
      gpr_[0] = static_cast<uint64_t>(
          static_cast<int64_t>(static_cast<int32_t>(gpr_[0])));
    } else if (pfx.op_size) {
      // CBW: sign-extend AL to AX
      gpr_[0] = (gpr_[0] & ~0xFFFFull) |
                static_cast<uint16_t>(
                    static_cast<int16_t>(static_cast<int8_t>(gpr_[0])));
    } else {
      // CWDE: sign-extend AX to EAX
      gpr_[0] = static_cast<uint32_t>(
          static_cast<int32_t>(static_cast<int16_t>(gpr_[0])));
    }
    break;
  }

  // ── CDQ/CQO (0x99) ─────────────────────────────────────────────────
  case 0x99: {
    if (pfx.rex_w) {
      // CQO: sign-extend RAX into RDX:RAX
      gpr_[2] = (static_cast<int64_t>(gpr_[0]) < 0) ? ~0ull : 0ull;
    } else {
      // CDQ: sign-extend EAX into EDX:EAX
      gpr_[2] = (static_cast<int32_t>(gpr_[0]) < 0) ? 0xFFFF'FFFFull : 0ull;
    }
    break;
  }

  // ── MOVS (0xA4/0xA5) ───────────────────────────────────────────────
  case 0xA4:
  case 0xA5: {
    const int sz = (op & 1) ? defSize : 1;
    const int dir = (rflags_ & kDF) ? -1 : 1;
    auto doMovs = [&]() {
      switch (sz) {
      case 1:
        mem_.Write8(gpr_[7], mem_.Read8(gpr_[6]));
        break;
      case 2:
        mem_.Write16(gpr_[7], mem_.Read16(gpr_[6]));
        break;
      case 4:
        mem_.Write32(gpr_[7], mem_.Read32(gpr_[6]));
        break;
      case 8:
        mem_.Write64(gpr_[7], mem_.Read64(gpr_[6]));
        break;
      }
      gpr_[6] += dir * sz;
      gpr_[7] += dir * sz;
    };
    if (pfx.rep) {
      while (gpr_[1] != 0) {
        doMovs();
        --gpr_[1];
      }
    } else {
      doMovs();
    }
    break;
  }

  // ── CMPS (0xA6/0xA7) ───────────────────────────────────────────────
  case 0xA6:
  case 0xA7: {
    const int sz = (op & 1) ? defSize : 1;
    const int dir = (rflags_ & kDF) ? -1 : 1;
    auto doCmps = [&]() {
      uint64_t a = 0, b = 0;
      switch (sz) {
      case 1:
        a = mem_.Read8(gpr_[6]);
        b = mem_.Read8(gpr_[7]);
        break;
      case 2:
        a = mem_.Read16(gpr_[6]);
        b = mem_.Read16(gpr_[7]);
        break;
      case 4:
        a = mem_.Read32(gpr_[6]);
        b = mem_.Read32(gpr_[7]);
        break;
      case 8:
        a = mem_.Read64(gpr_[6]);
        b = mem_.Read64(gpr_[7]);
        break;
      }
      DoSub(a, b, sz);
      gpr_[6] += dir * sz;
      gpr_[7] += dir * sz;
    };
    if (pfx.rep) {
      while (gpr_[1] != 0) {
        doCmps();
        --gpr_[1];
        if (rflags_ & kZF)
          break;
      }
    } else if (pfx.repne) {
      while (gpr_[1] != 0) {
        doCmps();
        --gpr_[1];
        if (!(rflags_ & kZF))
          break;
      }
    } else {
      doCmps();
    }
    break;
  }

  // ── STOS (0xAA/0xAB) ───────────────────────────────────────────────
  case 0xAA:
  case 0xAB: {
    const int sz = (op & 1) ? defSize : 1;
    const int dir = (rflags_ & kDF) ? -1 : 1;
    auto doStos = [&]() {
      switch (sz) {
      case 1:
        mem_.Write8(gpr_[7], static_cast<uint8_t>(gpr_[0]));
        break;
      case 2:
        mem_.Write16(gpr_[7], static_cast<uint16_t>(gpr_[0]));
        break;
      case 4:
        mem_.Write32(gpr_[7], static_cast<uint32_t>(gpr_[0]));
        break;
      case 8:
        mem_.Write64(gpr_[7], gpr_[0]);
        break;
      }
      gpr_[7] += dir * sz;
    };
    if (pfx.rep) {
      while (gpr_[1] != 0) {
        doStos();
        --gpr_[1];
      }
    } else {
      doStos();
    }
    break;
  }

  // ── LODS (0xAC/0xAD) ───────────────────────────────────────────────
  case 0xAC:
  case 0xAD: {
    const int sz = (op & 1) ? defSize : 1;
    const int dir = (rflags_ & kDF) ? -1 : 1;
    auto doLods = [&]() {
      switch (sz) {
      case 1:
        WriteReg(0, 1, mem_.Read8(gpr_[6]), pfx.has_rex);
        break;
      case 2:
        WriteReg(0, 2, mem_.Read16(gpr_[6]), pfx.has_rex);
        break;
      case 4:
        WriteReg(0, 4, mem_.Read32(gpr_[6]), pfx.has_rex);
        break;
      case 8:
        gpr_[0] = mem_.Read64(gpr_[6]);
        break;
      }
      gpr_[6] += dir * sz;
    };
    if (pfx.rep) {
      while (gpr_[1] != 0) {
        doLods();
        --gpr_[1];
      }
    } else {
      doLods();
    }
    break;
  }

  // ── SCAS (0xAE/0xAF) ───────────────────────────────────────────────
  case 0xAE:
  case 0xAF: {
    const int sz = (op & 1) ? defSize : 1;
    const int dir = (rflags_ & kDF) ? -1 : 1;
    auto doScas = [&]() {
      uint64_t mem_val = 0;
      switch (sz) {
      case 1:
        mem_val = mem_.Read8(gpr_[7]);
        break;
      case 2:
        mem_val = mem_.Read16(gpr_[7]);
        break;
      case 4:
        mem_val = mem_.Read32(gpr_[7]);
        break;
      case 8:
        mem_val = mem_.Read64(gpr_[7]);
        break;
      }
      DoSub(ReadReg(0, sz, pfx.has_rex), mem_val, sz);
      gpr_[7] += dir * sz;
    };
    if (pfx.rep) {
      while (gpr_[1] != 0) {
        doScas();
        --gpr_[1];
        if (!(rflags_ & kZF))
          break;
      }
    } else if (pfx.repne) {
      while (gpr_[1] != 0) {
        doScas();
        --gpr_[1];
        if (rflags_ & kZF)
          break;
      }
    } else {
      doScas();
    }
    break;
  }

  // ── MOV reg, imm (0xB0–0xBF) ──────────────────────────────────────
  case 0xB0:
  case 0xB1:
  case 0xB2:
  case 0xB3:
  case 0xB4:
  case 0xB5:
  case 0xB6:
  case 0xB7: {
    int reg = (op - 0xB0);
    if (pfx.rex_b)
      reg |= 8;
    WriteReg(reg, 1, Fetch8(), pfx.has_rex);
    break;
  }
  case 0xB8:
  case 0xB9:
  case 0xBA:
  case 0xBB:
  case 0xBC:
  case 0xBD:
  case 0xBE:
  case 0xBF: {
    int reg = (op - 0xB8);
    if (pfx.rex_b)
      reg |= 8;
    if (pfx.rex_w) {
      gpr_[reg] = Fetch64();
    } else if (pfx.op_size) {
      WriteReg(reg, 2, Fetch16(), pfx.has_rex);
    } else {
      gpr_[reg] = Fetch32(); // 32-bit → zero-extend
    }
    break;
  }

  // ── Group 2: shift r/m, imm8 (0xC0/0xC1) ──────────────────────────
  case 0xC0:
  case 0xC1: {
    const int sz = (op & 1) ? defSize : 1;
    MRM mr = DecodeModRM(pfx);
    const uint8_t cnt = Fetch8();
    ExecGroup2(pfx, mr, sz, cnt);
    break;
  }

  // ── RET near (0xC2/0xC3) ───────────────────────────────────────────
  case 0xC2: {
    const uint16_t imm = Fetch16();
    rip_ = Pop64();
    gpr_[4] += imm;
    break;
  }
  case 0xC3: {
    rip_ = Pop64();
    break;
  }

  // ── MOV r/m, imm (0xC6/0xC7) — Group 11 ───────────────────────────
  case 0xC6:
  case 0xC7: {
    const int sz = (op & 1) ? defSize : 1;
    MRM mr = DecodeModRM(pfx);
    uint64_t imm;
    if (sz == 1)
      imm = Fetch8();
    else if (sz == 2)
      imm = Fetch16();
    else
      imm = static_cast<uint64_t>(FetchS32());
    WriteRM(mr, pfx, sz, imm);
    break;
  }

  // ── LEAVE (0xC9) ───────────────────────────────────────────────────
  case 0xC9: {
    gpr_[4] = gpr_[5]; // RSP = RBP
    gpr_[5] = Pop64(); // POP RBP
    break;
  }

  // ── INT 3 (0xCC) ────────────────────────────────────────────────────
  case 0xCC: {
    // Software breakpoint — treat as NOP in emulation
    break;
  }

  // ── Group 2: shift r/m, 1 (0xD0/0xD1) ─────────────────────────────
  case 0xD0:
  case 0xD1: {
    const int sz = (op & 1) ? defSize : 1;
    MRM mr = DecodeModRM(pfx);
    ExecGroup2(pfx, mr, sz, 1);
    break;
  }

  // ── Group 2: shift r/m, CL (0xD2/0xD3) ─────────────────────────────
  case 0xD2:
  case 0xD3: {
    const int sz = (op & 1) ? defSize : 1;
    MRM mr = DecodeModRM(pfx);
    ExecGroup2(pfx, mr, sz, static_cast<uint8_t>(gpr_[1] & 0xFF));
    break;
  }

  // ── CALL rel32 (0xE8) ──────────────────────────────────────────────
  case 0xE8: {
    const int32_t disp = FetchS32();
    Push64(rip_);
    rip_ = rip_ + static_cast<int64_t>(disp);
    break;
  }

  // ── JMP rel32 (0xE9) ───────────────────────────────────────────────
  case 0xE9: {
    const int32_t disp = FetchS32();
    rip_ = rip_ + static_cast<int64_t>(disp);
    break;
  }

  // ── JMP rel8 (0xEB) ────────────────────────────────────────────────
  case 0xEB: {
    const int8_t disp = FetchS8();
    rip_ = rip_ + static_cast<int64_t>(disp);
    break;
  }

  // ── CLC/STC/CLI/STI/CLD/STD (0xF8–0xFD) ───────────────────────────
  case 0xF8:
    rflags_ &= ~kCF;
    break; // CLC
  case 0xF9:
    rflags_ |= kCF;
    break; // STC
  case 0xFA:
    rflags_ &= ~kIF;
    break; // CLI
  case 0xFB:
    rflags_ |= kIF;
    break; // STI
  case 0xFC:
    rflags_ &= ~kDF;
    break; // CLD
  case 0xFD:
    rflags_ |= kDF;
    break; // STD

  // ── HLT (0xF4) ─────────────────────────────────────────────────────
  case 0xF4: {
    halted_ = true;
    return false;
  }

  // ── Group 3: TEST / NOT / NEG / MUL / IMUL / DIV / IDIV (0xF6/0xF7)
  case 0xF6:
  case 0xF7: {
    const int sz = (op & 1) ? defSize : 1;
    MRM mr = DecodeModRM(pfx);
    ExecGroup3(pfx, mr, sz);
    break;
  }

  // ── Group 4 (INC/DEC r/m8) 0xFE ────────────────────────────────────
  case 0xFE: {
    MRM mr = DecodeModRM(pfx);
    const uint64_t v = ReadRM(mr, pfx, 1);
    if ((mr.reg & 7) == 0) {
      const uint64_t r = DoAdd(v, 1, 1);
      WriteRM(mr, pfx, 1, r);
    } else {
      const uint64_t r = DoSub(v, 1, 1);
      WriteRM(mr, pfx, 1, r);
    }
    break;
  }

  // ── Group 5 (INC/DEC/CALL/JMP/PUSH) 0xFF ───────────────────────────
  case 0xFF: {
    MRM mr = DecodeModRM(pfx);
    ExecGroup5(pfx, mr);
    break;
  }

  // ── Two-byte escape (0x0F) ──────────────────────────────────────────
  case 0x0F: {
    const uint8_t op2 = Fetch8();
    ExecTwoByte(pfx, op2);
    break;
  }

  default:
    FaultUnknownOpcode(op);
    return false;
  }

  (void)inst_rip;
  return !faulted_ && !halted_;
}

// ─── Group 1 (used from 0x80–0x83 path — the actual dispatch is inlined) ────

void X86_64Core::ExecGroup1(const PfxState & /*pfx*/, const MRM & /*mr*/,
                            int /*size*/, uint8_t /*op*/) {
  // Actual group 1 logic is inlined in the 0x80-0x83 handler
}

// ─── Group 2: shift/rotate ───────────────────────────────────────────────────

void X86_64Core::ExecGroup2(const PfxState &pfx, const MRM &mr, int size,
                            uint8_t cnt_src) {
  uint8_t cnt = cnt_src & (size == 8 ? 0x3F : 0x1F);
  uint64_t v = ReadRM(mr, pfx, size);
  uint64_t r;
  switch (mr.reg & 7) {
  case 0:
    r = DoRol(v, cnt, size);
    break;
  case 1:
    r = DoRor(v, cnt, size);
    break;
  case 2: { // RCL
    // Simplified: treat as ROL (close enough for most code)
    r = DoRol(v, cnt, size);
    break;
  }
  case 3: { // RCR
    r = DoRor(v, cnt, size);
    break;
  }
  case 4:
    r = DoShl(v, cnt, size);
    break;
  case 5:
    r = DoShr(v, cnt, size);
    break;
  case 6:
    r = DoShl(v, cnt, size);
    break; // SAL == SHL
  case 7:
    r = DoSar(v, cnt, size);
    break;
  default:
    r = v;
    break;
  }
  if (cnt != 0)
    WriteRM(mr, pfx, size, r);
}

// ─── Group 3: TEST / NOT / NEG / MUL / IMUL / DIV / IDIV ────────────────────

void X86_64Core::ExecGroup3(const PfxState &pfx, const MRM &mr, int size) {
  const uint64_t v = ReadRM(mr, pfx, size);
  switch (mr.reg & 7) {
  case 0:
  case 1: { // TEST r/m, imm
    uint64_t imm;
    if (size == 1)
      imm = Fetch8();
    else if (size == 2)
      imm = Fetch16();
    else
      imm = static_cast<uint64_t>(FetchS32());
    DoAnd(v, imm, size);
    break;
  }
  case 2: { // NOT
    WriteRM(mr, pfx, size, ~v & SizeMask(size));
    break;
  }
  case 3: { // NEG
    const uint64_t r = DoSub(0, v, size);
    WriteRM(mr, pfx, size, r);
    // NEG sets CF=1 if source != 0
    if (v != 0)
      rflags_ |= kCF;
    else
      rflags_ &= ~kCF;
    break;
  }
  case 4: { // MUL (unsigned)
    const uint64_t mask = SizeMask(size);
    __uint128_t result;
    switch (size) {
    case 1: {
      uint16_t r = static_cast<uint16_t>(gpr_[0] & 0xFF) *
                   static_cast<uint16_t>(v & 0xFF);
      gpr_[0] = (gpr_[0] & ~0xFFFFull) | r;
      rflags_ &= ~(kCF | kOF);
      if (r > 0xFF)
        rflags_ |= (kCF | kOF);
      return;
    }
    case 2: {
      uint32_t r = static_cast<uint32_t>(gpr_[0] & 0xFFFF) *
                   static_cast<uint32_t>(v & 0xFFFF);
      gpr_[0] = (gpr_[0] & ~0xFFFFull) | (r & 0xFFFF);
      gpr_[2] = (gpr_[2] & ~0xFFFFull) | (r >> 16);
      rflags_ &= ~(kCF | kOF);
      if (r > 0xFFFF)
        rflags_ |= (kCF | kOF);
      return;
    }
    case 4: {
      uint64_t r =
          static_cast<uint64_t>(gpr_[0] & 0xFFFF'FFFF) * (v & 0xFFFF'FFFF);
      gpr_[0] = r & 0xFFFF'FFFFull;
      gpr_[2] = (r >> 32) & 0xFFFF'FFFFull;
      rflags_ &= ~(kCF | kOF);
      if (r > 0xFFFF'FFFFull)
        rflags_ |= (kCF | kOF);
      return;
    }
    default: {
      result = static_cast<__uint128_t>(gpr_[0]) * static_cast<__uint128_t>(v);
      gpr_[0] = static_cast<uint64_t>(result);
      gpr_[2] = static_cast<uint64_t>(result >> 64);
      rflags_ &= ~(kCF | kOF);
      if (gpr_[2] != 0)
        rflags_ |= (kCF | kOF);
      return;
    }
    }
    (void)result;
    break;
  }
  case 5: { // IMUL (one-operand)
    switch (size) {
    case 1: {
      int16_t r = static_cast<int8_t>(gpr_[0]) * static_cast<int8_t>(v);
      gpr_[0] = (gpr_[0] & ~0xFFFFull) | static_cast<uint16_t>(r);
      rflags_ &= ~(kCF | kOF);
      if (r != static_cast<int8_t>(r))
        rflags_ |= (kCF | kOF);
      break;
    }
    case 2: {
      int32_t r = static_cast<int16_t>(gpr_[0]) * static_cast<int16_t>(v);
      gpr_[0] = (gpr_[0] & ~0xFFFFull) | (static_cast<uint32_t>(r) & 0xFFFF);
      gpr_[2] =
          (gpr_[2] & ~0xFFFFull) | ((static_cast<uint32_t>(r) >> 16) & 0xFFFF);
      rflags_ &= ~(kCF | kOF);
      if (r != static_cast<int16_t>(r))
        rflags_ |= (kCF | kOF);
      break;
    }
    case 4: {
      int64_t r = static_cast<int64_t>(static_cast<int32_t>(gpr_[0])) *
                  static_cast<int64_t>(static_cast<int32_t>(v));
      gpr_[0] = static_cast<uint64_t>(r) & 0xFFFF'FFFFull;
      gpr_[2] = (static_cast<uint64_t>(r) >> 32) & 0xFFFF'FFFFull;
      rflags_ &= ~(kCF | kOF);
      if (r != static_cast<int32_t>(r))
        rflags_ |= (kCF | kOF);
      break;
    }
    default: {
      __int128 r = static_cast<__int128>(static_cast<int64_t>(gpr_[0])) *
                   static_cast<__int128>(static_cast<int64_t>(v));
      gpr_[0] = static_cast<uint64_t>(r);
      gpr_[2] = static_cast<uint64_t>(static_cast<__uint128_t>(r) >> 64);
      rflags_ &= ~(kCF | kOF);
      if (r != static_cast<int64_t>(r))
        rflags_ |= (kCF | kOF);
      break;
    }
    }
    break;
  }
  case 6: { // DIV (unsigned)
    if (v == 0) {
      Fault("Division by zero");
      return;
    }
    switch (size) {
    case 1: {
      uint16_t ax = static_cast<uint16_t>(gpr_[0] & 0xFFFF);
      gpr_[0] = (gpr_[0] & ~0xFFFFull) |
                (static_cast<uint16_t>(ax / (v & 0xFF)) & 0xFF) |
                (static_cast<uint16_t>(ax % (v & 0xFF)) << 8);
      break;
    }
    case 2: {
      uint32_t dx_ax = (static_cast<uint32_t>(gpr_[2] & 0xFFFF) << 16) |
                       static_cast<uint32_t>(gpr_[0] & 0xFFFF);
      uint32_t div16 = static_cast<uint32_t>(v & 0xFFFF);
      gpr_[0] = (gpr_[0] & ~0xFFFFull) | (dx_ax / div16) & 0xFFFF;
      gpr_[2] = (gpr_[2] & ~0xFFFFull) | (dx_ax % div16) & 0xFFFF;
      break;
    }
    case 4: {
      uint64_t edx_eax = (static_cast<uint64_t>(gpr_[2] & 0xFFFF'FFFF) << 32) |
                         (gpr_[0] & 0xFFFF'FFFF);
      uint64_t div32 = v & 0xFFFF'FFFF;
      gpr_[0] = (edx_eax / div32) & 0xFFFF'FFFF;
      gpr_[2] = (edx_eax % div32) & 0xFFFF'FFFF;
      break;
    }
    default: {
      __uint128_t rdx_rax = (static_cast<__uint128_t>(gpr_[2]) << 64) | gpr_[0];
      gpr_[0] = static_cast<uint64_t>(rdx_rax / v);
      gpr_[2] = static_cast<uint64_t>(rdx_rax % v);
      break;
    }
    }
    break;
  }
  case 7: { // IDIV (signed)
    if (v == 0) {
      Fault("Division by zero (IDIV)");
      return;
    }
    switch (size) {
    case 1: {
      int16_t ax = static_cast<int16_t>(gpr_[0] & 0xFFFF);
      int8_t div8 = static_cast<int8_t>(v);
      gpr_[0] = (gpr_[0] & ~0xFFFFull) | (static_cast<uint8_t>(ax / div8)) |
                (static_cast<uint16_t>(static_cast<uint8_t>(ax % div8)) << 8);
      break;
    }
    case 4: {
      int64_t edx_eax =
          (static_cast<int64_t>(static_cast<int32_t>(gpr_[2])) << 32) |
          static_cast<int64_t>(gpr_[0] & 0xFFFF'FFFF);
      int32_t div32 = static_cast<int32_t>(v);
      gpr_[0] = static_cast<uint32_t>(edx_eax / div32);
      gpr_[2] = static_cast<uint32_t>(edx_eax % div32);
      break;
    }
    default: {
      __int128 rdx_rax =
          (static_cast<__int128>(static_cast<int64_t>(gpr_[2])) << 64) |
          static_cast<__int128>(gpr_[0]);
      int64_t sv = static_cast<int64_t>(v);
      gpr_[0] = static_cast<uint64_t>(rdx_rax / sv);
      gpr_[2] = static_cast<uint64_t>(rdx_rax % sv);
      break;
    }
    }
    break;
  }
  }
}

// ─── Group 5 (0xFF): INC/DEC/CALL/JMP/PUSH ──────────────────────────────────

void X86_64Core::ExecGroup5(const PfxState &pfx, const MRM &mr) {
  const int size = OpSize(pfx);
  switch (mr.reg & 7) {
  case 0: { // INC
    const uint64_t v = ReadRM(mr, pfx, size);
    const bool oldCF = (rflags_ & kCF) != 0;
    const uint64_t r = DoAdd(v, 1, size);
    WriteRM(mr, pfx, size, r);
    if (oldCF)
      rflags_ |= kCF;
    else
      rflags_ &= ~kCF; // INC doesn't affect CF
    break;
  }
  case 1: { // DEC
    const uint64_t v = ReadRM(mr, pfx, size);
    const bool oldCF = (rflags_ & kCF) != 0;
    const uint64_t r = DoSub(v, 1, size);
    WriteRM(mr, pfx, size, r);
    if (oldCF)
      rflags_ |= kCF;
    else
      rflags_ &= ~kCF;
    break;
  }
  case 2: { // CALL r/m64
    const uint64_t target = ReadRM(mr, pfx, 8);
    Push64(rip_);
    rip_ = target;
    break;
  }
  case 4: { // JMP r/m64
    rip_ = ReadRM(mr, pfx, 8);
    break;
  }
  case 6: { // PUSH r/m
    Push64(ReadRM(mr, pfx, 8));
    break;
  }
  default:
    Fault("Unhandled Group 5 sub-opcode " + std::to_string(mr.reg & 7));
    break;
  }
}

// ─── Group 11 (MOV r/m, imm — currently handled in main switch) ─────────────

void X86_64Core::ExecGroup11(const PfxState & /*pfx*/, const MRM & /*mr*/,
                             int /*size*/) {
  // Implemented inline in the 0xC6/0xC7 handler
}

// ─── Two-byte opcodes (0x0F xx) ──────────────────────────────────────────────

void X86_64Core::ExecTwoByte(const PfxState &pfx, uint8_t op2) {
  const int defSize = OpSize(pfx);

  switch (op2) {
  // ── Multi-byte NOP (0x0F 0x1F) ─────────────────────────────────────
  case 0x1F: {
    MRM mr = DecodeModRM(pfx); // consume ModRM + displacement
    (void)mr;
    break;
  }

  // ── SYSCALL (0x0F 0x05) — trap, not a real syscall ──────────────────
  case 0x05: {
    // Games shouldn't use raw syscalls; if they do, halt.
    Fault("SYSCALL not supported");
    break;
  }

  // ── CPUID (0x0F 0xA2) ──────────────────────────────────────────────
  case 0xA2: {
    // Return basic AMD64 CPU identity
    const uint32_t leaf = static_cast<uint32_t>(gpr_[0]);
    switch (leaf) {
    case 0:                 // Max leaf + vendor string
      gpr_[0] = 0x16;       // max basic leaf
      gpr_[1] = 0x756E6547; // "Genu"
      gpr_[2] = 0x6C65746E; // "ntel"
      gpr_[3] = 0x49656E69; // "ineI"
      break;
    case 1:                 // Version info + feature flags
      gpr_[0] = 0x000906A0; // family/model/stepping
      gpr_[1] = 0x00000800;
      gpr_[2] = 0x7FFAFBFF; // SSE2/SSE3/SSSE3/SSE4.1/SSE4.2/AVX etc.
      gpr_[3] = 0xBFEBFBFF;
      break;
    default:
      gpr_[0] = gpr_[1] = gpr_[2] = gpr_[3] = 0;
      break;
    }
    break;
  }

  // ── Jcc near (0x0F 0x80–0x8F) ──────────────────────────────────────
  case 0x80:
  case 0x81:
  case 0x82:
  case 0x83:
  case 0x84:
  case 0x85:
  case 0x86:
  case 0x87:
  case 0x88:
  case 0x89:
  case 0x8A:
  case 0x8B:
  case 0x8C:
  case 0x8D:
  case 0x8E:
  case 0x8F: {
    const int32_t disp = FetchS32();
    if (TestCC(op2 & 0xF))
      rip_ = rip_ + static_cast<int64_t>(disp);
    break;
  }

  // ── SETcc r/m8 (0x0F 0x90–0x9F) ────────────────────────────────────
  case 0x90:
  case 0x91:
  case 0x92:
  case 0x93:
  case 0x94:
  case 0x95:
  case 0x96:
  case 0x97:
  case 0x98:
  case 0x99:
  case 0x9A:
  case 0x9B:
  case 0x9C:
  case 0x9D:
  case 0x9E:
  case 0x9F: {
    MRM mr = DecodeModRM(pfx);
    WriteRM(mr, pfx, 1, TestCC(op2 & 0xF) ? 1 : 0);
    break;
  }

  // ── CMOVcc (0x0F 0x40–0x4F) ────────────────────────────────────────
  case 0x40:
  case 0x41:
  case 0x42:
  case 0x43:
  case 0x44:
  case 0x45:
  case 0x46:
  case 0x47:
  case 0x48:
  case 0x49:
  case 0x4A:
  case 0x4B:
  case 0x4C:
  case 0x4D:
  case 0x4E:
  case 0x4F: {
    MRM mr = DecodeModRM(pfx);
    const uint64_t v = ReadRM(mr, pfx, defSize);
    if (TestCC(op2 & 0xF))
      WriteReg(mr.reg, defSize, v, pfx.has_rex);
    break;
  }

  // ── PUSH FS (0x0F 0xA0) / PUSH GS (0x0F 0xA8) ────────────────────
  case 0xA0:
    Push64(0);
    break; // FS = 0 in emulation
  case 0xA8:
    Push64(0);
    break; // GS = 0

  // ── POP FS (0x0F 0xA1) / POP GS (0x0F 0xA9) ──────────────────────
  case 0xA1:
    Pop64();
    break;
  case 0xA9:
    Pop64();
    break;

  // ── BT r/m, reg (0x0F 0xA3) ────────────────────────────────────────
  case 0xA3: {
    MRM mr = DecodeModRM(pfx);
    const uint64_t v = ReadRM(mr, pfx, defSize);
    const uint64_t bit = ReadReg(mr.reg, defSize, pfx.has_rex) % (defSize * 8);
    rflags_ &= ~kCF;
    if ((v >> bit) & 1)
      rflags_ |= kCF;
    break;
  }

  // ── BTS r/m, reg (0x0F 0xAB) ───────────────────────────────────────
  case 0xAB: {
    MRM mr = DecodeModRM(pfx);
    uint64_t v = ReadRM(mr, pfx, defSize);
    const uint64_t bit = ReadReg(mr.reg, defSize, pfx.has_rex) % (defSize * 8);
    rflags_ &= ~kCF;
    if ((v >> bit) & 1)
      rflags_ |= kCF;
    v |= (1ull << bit);
    WriteRM(mr, pfx, defSize, v);
    break;
  }

  // ── BTR r/m, reg (0x0F 0xB3) ───────────────────────────────────────
  case 0xB3: {
    MRM mr = DecodeModRM(pfx);
    uint64_t v = ReadRM(mr, pfx, defSize);
    const uint64_t bit = ReadReg(mr.reg, defSize, pfx.has_rex) % (defSize * 8);
    rflags_ &= ~kCF;
    if ((v >> bit) & 1)
      rflags_ |= kCF;
    v &= ~(1ull << bit);
    WriteRM(mr, pfx, defSize, v);
    break;
  }

  // ── BTC r/m, reg (0x0F 0xBB) ───────────────────────────────────────
  case 0xBB: {
    MRM mr = DecodeModRM(pfx);
    uint64_t v = ReadRM(mr, pfx, defSize);
    const uint64_t bit = ReadReg(mr.reg, defSize, pfx.has_rex) % (defSize * 8);
    rflags_ &= ~kCF;
    if ((v >> bit) & 1)
      rflags_ |= kCF;
    v ^= (1ull << bit);
    WriteRM(mr, pfx, defSize, v);
    break;
  }

  // ── BT/BTS/BTR/BTC r/m, imm8 (0x0F 0xBA) ──────────────────────────
  case 0xBA: {
    MRM mr = DecodeModRM(pfx);
    uint64_t v = ReadRM(mr, pfx, defSize);
    const uint8_t bit = Fetch8() % (defSize * 8);
    rflags_ &= ~kCF;
    if ((v >> bit) & 1)
      rflags_ |= kCF;
    switch (mr.reg & 7) {
    case 4:
      break; // BT — test only
    case 5:
      v |= (1ull << bit);
      WriteRM(mr, pfx, defSize, v);
      break; // BTS
    case 6:
      v &= ~(1ull << bit);
      WriteRM(mr, pfx, defSize, v);
      break; // BTR
    case 7:
      v ^= (1ull << bit);
      WriteRM(mr, pfx, defSize, v);
      break; // BTC
    }
    break;
  }

  // ── IMUL reg, r/m (0x0F 0xAF) ─────────────────────────────────────
  case 0xAF: {
    MRM mr = DecodeModRM(pfx);
    int64_t a = 0, b = 0;
    switch (defSize) {
    case 2:
      a = static_cast<int16_t>(ReadReg(mr.reg, 2, pfx.has_rex));
      b = static_cast<int16_t>(ReadRM(mr, pfx, 2));
      break;
    case 4:
      a = static_cast<int32_t>(ReadReg(mr.reg, 4, pfx.has_rex));
      b = static_cast<int32_t>(ReadRM(mr, pfx, 4));
      break;
    case 8:
      a = static_cast<int64_t>(ReadReg(mr.reg, 8, pfx.has_rex));
      b = static_cast<int64_t>(ReadRM(mr, pfx, 8));
      break;
    }
    int64_t r = a * b;
    WriteReg(mr.reg, defSize, static_cast<uint64_t>(r), pfx.has_rex);
    const uint64_t mask = SizeMask(defSize);
    rflags_ &= ~(kCF | kOF);
    int64_t trunc;
    switch (defSize) {
    case 2:
      trunc = static_cast<int16_t>(static_cast<uint64_t>(r) & mask);
      break;
    case 4:
      trunc = static_cast<int32_t>(static_cast<uint64_t>(r) & mask);
      break;
    default:
      trunc = r;
      break;
    }
    if (trunc != r)
      rflags_ |= (kCF | kOF);
    break;
  }

  // ── MOVZX r, r/m8 (0x0F 0xB6) ─────────────────────────────────────
  case 0xB6: {
    MRM mr = DecodeModRM(pfx);
    WriteReg(mr.reg, defSize, ReadRM(mr, pfx, 1), pfx.has_rex);
    break;
  }

  // ── MOVZX r, r/m16 (0x0F 0xB7) ────────────────────────────────────
  case 0xB7: {
    MRM mr = DecodeModRM(pfx);
    WriteReg(mr.reg, defSize, ReadRM(mr, pfx, 2), pfx.has_rex);
    break;
  }

  // ── MOVSX r, r/m8 (0x0F 0xBE) ─────────────────────────────────────
  case 0xBE: {
    MRM mr = DecodeModRM(pfx);
    int8_t val = static_cast<int8_t>(ReadRM(mr, pfx, 1));
    if (pfx.rex_w)
      WriteReg(mr.reg, 8, static_cast<uint64_t>(static_cast<int64_t>(val)),
               pfx.has_rex);
    else
      WriteReg(mr.reg, defSize,
               static_cast<uint64_t>(static_cast<int32_t>(val)) &
                   SizeMask(defSize),
               pfx.has_rex);
    break;
  }

  // ── MOVSX r, r/m16 (0x0F 0xBF) ────────────────────────────────────
  case 0xBF: {
    MRM mr = DecodeModRM(pfx);
    int16_t val = static_cast<int16_t>(ReadRM(mr, pfx, 2));
    if (pfx.rex_w)
      WriteReg(mr.reg, 8, static_cast<uint64_t>(static_cast<int64_t>(val)),
               pfx.has_rex);
    else
      WriteReg(mr.reg, defSize,
               static_cast<uint32_t>(static_cast<int32_t>(val)), pfx.has_rex);
    break;
  }

  // ── BSF (0x0F 0xBC) ────────────────────────────────────────────────
  case 0xBC: {
    MRM mr = DecodeModRM(pfx);
    uint64_t v = ReadRM(mr, pfx, defSize) & SizeMask(defSize);
    if (v == 0) {
      rflags_ |= kZF;
    } else {
      rflags_ &= ~kZF;
      int bit = 0;
      while (!(v & 1)) {
        v >>= 1;
        ++bit;
      }
      WriteReg(mr.reg, defSize, static_cast<uint64_t>(bit), pfx.has_rex);
    }
    break;
  }

  // ── BSR (0x0F 0xBD) ────────────────────────────────────────────────
  case 0xBD: {
    MRM mr = DecodeModRM(pfx);
    uint64_t v = ReadRM(mr, pfx, defSize) & SizeMask(defSize);
    if (v == 0) {
      rflags_ |= kZF;
    } else {
      rflags_ &= ~kZF;
      int bit = defSize * 8 - 1;
      while (bit >= 0 && !((v >> bit) & 1))
        --bit;
      WriteReg(mr.reg, defSize, static_cast<uint64_t>(bit), pfx.has_rex);
    }
    break;
  }

  // ── XADD (0x0F 0xC0 / 0x0F 0xC1) ──────────────────────────────────
  case 0xC0:
  case 0xC1: {
    const int sz = (op2 & 1) ? defSize : 1;
    MRM mr = DecodeModRM(pfx);
    uint64_t dst = ReadRM(mr, pfx, sz);
    uint64_t src = ReadReg(mr.reg, sz, pfx.has_rex);
    uint64_t sum = DoAdd(dst, src, sz);
    WriteRM(mr, pfx, sz, sum);
    WriteReg(mr.reg, sz, dst, pfx.has_rex);
    break;
  }

  // ── CMPXCHG (0x0F 0xB0 / 0x0F 0xB1) ──────────────────────────────
  case 0xB0:
  case 0xB1: {
    const int sz = (op2 & 1) ? defSize : 1;
    MRM mr = DecodeModRM(pfx);
    uint64_t dst = ReadRM(mr, pfx, sz);
    uint64_t acc = ReadReg(0, sz, pfx.has_rex);
    DoSub(acc, dst, sz); // set flags
    if (rflags_ & kZF) {
      WriteRM(mr, pfx, sz, ReadReg(mr.reg, sz, pfx.has_rex));
    } else {
      WriteReg(0, sz, dst, pfx.has_rex);
    }
    break;
  }

  // ── BSWAP (0x0F 0xC8–0xCF) ────────────────────────────────────────
  case 0xC8:
  case 0xC9:
  case 0xCA:
  case 0xCB:
  case 0xCC:
  case 0xCD:
  case 0xCE:
  case 0xCF: {
    int reg = (op2 - 0xC8);
    if (pfx.rex_b)
      reg |= 8;
    if (pfx.rex_w) {
      uint64_t v = gpr_[reg];
      v = ((v & 0x00000000000000FFull) << 56) |
          ((v & 0x000000000000FF00ull) << 40) |
          ((v & 0x0000000000FF0000ull) << 24) |
          ((v & 0x00000000FF000000ull) << 8) |
          ((v & 0x000000FF00000000ull) >> 8) |
          ((v & 0x0000FF0000000000ull) >> 24) |
          ((v & 0x00FF000000000000ull) >> 40) |
          ((v & 0xFF00000000000000ull) >> 56);
      gpr_[reg] = v;
    } else {
      uint32_t v = static_cast<uint32_t>(gpr_[reg]);
      v = ((v & 0x000000FF) << 24) | ((v & 0x0000FF00) << 8) |
          ((v & 0x00FF0000) >> 8) | ((v & 0xFF000000) >> 24);
      gpr_[reg] = v; // zero-extends
    }
    break;
  }

  // ── MOVD/MOVQ xmm, r/m  (0x0F 0x6E) — with 0x66 prefix ───────────
  case 0x6E: {
    if (pfx.op_size || pfx.rex_w) {
      MRM mr = DecodeModRM(pfx);
      uint64_t v = ReadRM(mr, pfx, pfx.rex_w ? 8 : 4);
      xmm_[mr.reg & 15].lo = v;
      xmm_[mr.reg & 15].hi = 0;
    }
    break;
  }

  // ── MOVD/MOVQ r/m, xmm  (0x0F 0x7E) — with 0x66 prefix ───────────
  case 0x7E: {
    if (pfx.op_size || pfx.rep) {
      MRM mr = DecodeModRM(pfx);
      const uint64_t v = xmm_[mr.reg & 15].lo;
      if (pfx.rex_w)
        WriteRM(mr, pfx, 8, v);
      else
        WriteRM(mr, pfx, 4, v & 0xFFFF'FFFF);
    }
    break;
  }

  // ── RDTSC (0x0F 0x31) ──────────────────────────────────────────────
  case 0x31: {
    // Return a monotonically increasing fake TSC
    static uint64_t fakeTsc = 0;
    fakeTsc += 100;
    gpr_[0] = fakeTsc & 0xFFFF'FFFFull;         // EAX
    gpr_[2] = (fakeTsc >> 32) & 0xFFFF'FFFFull; // EDX
    break;
  }

  // ── LFENCE/MFENCE/SFENCE (0x0F 0xAE) ──────────────────────────────
  case 0xAE: {
    MRM mr = DecodeModRM(pfx);
    // Fence instructions are NOPs in single-threaded emulation
    (void)mr;
    break;
  }

  // ── MOVAPS / MOVUPS xmm, xmm/m128 (0x0F 0x28 / 0x0F 0x10) ───────
  case 0x10:
  case 0x28: {
    MRM mr = DecodeModRM(pfx);
    if (mr.is_mem) {
      xmm_[mr.reg & 15].lo = mem_.Read64(mr.ea);
      xmm_[mr.reg & 15].hi = mem_.Read64(mr.ea + 8);
    } else {
      xmm_[mr.reg & 15] = xmm_[mr.rm & 15];
    }
    break;
  }

  // ── MOVAPS / MOVUPS xmm/m128, xmm (0x0F 0x29 / 0x0F 0x11) ───────
  case 0x11:
  case 0x29: {
    MRM mr = DecodeModRM(pfx);
    if (mr.is_mem) {
      mem_.Write64(mr.ea, xmm_[mr.reg & 15].lo);
      mem_.Write64(mr.ea + 8, xmm_[mr.reg & 15].hi);
    } else {
      xmm_[mr.rm & 15] = xmm_[mr.reg & 15];
    }
    break;
  }

  // ── XORPS/XORPD (0x0F 0x57) ───────────────────────────────────────
  case 0x57: {
    MRM mr = DecodeModRM(pfx);
    uint64_t lo, hi;
    if (mr.is_mem) {
      lo = mem_.Read64(mr.ea);
      hi = mem_.Read64(mr.ea + 8);
    } else {
      lo = xmm_[mr.rm & 15].lo;
      hi = xmm_[mr.rm & 15].hi;
    }
    xmm_[mr.reg & 15].lo ^= lo;
    xmm_[mr.reg & 15].hi ^= hi;
    break;
  }

  // ── PXOR (0x0F 0xEF) ──────────────────────────────────────────────
  case 0xEF: {
    MRM mr = DecodeModRM(pfx);
    uint64_t lo, hi;
    if (mr.is_mem) {
      lo = mem_.Read64(mr.ea);
      hi = mem_.Read64(mr.ea + 8);
    } else {
      lo = xmm_[mr.rm & 15].lo;
      hi = xmm_[mr.rm & 15].hi;
    }
    xmm_[mr.reg & 15].lo ^= lo;
    xmm_[mr.reg & 15].hi ^= hi;
    break;
  }

  // ── MOVDQA/MOVDQU load (0x0F 0x6F) ────────────────────────────────
  case 0x6F: {
    MRM mr = DecodeModRM(pfx);
    if (mr.is_mem) {
      xmm_[mr.reg & 15].lo = mem_.Read64(mr.ea);
      xmm_[mr.reg & 15].hi = mem_.Read64(mr.ea + 8);
    } else {
      xmm_[mr.reg & 15] = xmm_[mr.rm & 15];
    }
    break;
  }

  // ── MOVDQA/MOVDQU store (0x0F 0x7F) ───────────────────────────────
  case 0x7F: {
    MRM mr = DecodeModRM(pfx);
    if (mr.is_mem) {
      mem_.Write64(mr.ea, xmm_[mr.reg & 15].lo);
      mem_.Write64(mr.ea + 8, xmm_[mr.reg & 15].hi);
    } else {
      xmm_[mr.rm & 15] = xmm_[mr.reg & 15];
    }
    break;
  }

  // ── NOP-like 0x0F 0x18–0x1E (prefetch hints, NOPs) ────────────────
  case 0x18:
  case 0x19:
  case 0x1A:
  case 0x1B:
  case 0x1C:
  case 0x1D:
  case 0x1E: {
    MRM mr = DecodeModRM(pfx);
    (void)mr;
    break;
  }

  default: {
    std::ostringstream ss;
    ss << "Unknown two-byte opcode 0x0F 0x" << std::hex << std::setw(2)
       << std::setfill('0') << static_cast<int>(op2);
    Fault(ss.str());
    break;
  }
  }
}

// ─── Debug state string ──────────────────────────────────────────────────────

std::string X86_64Core::GetStateString() const {
  std::ostringstream ss;
  ss << std::hex << std::setfill('0');
  ss << "RIP=" << std::setw(16) << rip_ << " RSP=" << std::setw(16) << gpr_[4]
     << " RFLAGS=" << std::setw(16) << rflags_ << "\n";
  const char *names[] = {"RAX", "RCX", "RDX", "RBX", "RSP", "RBP",
                         "RSI", "RDI", "R8 ", "R9 ", "R10", "R11",
                         "R12", "R13", "R14", "R15"};
  for (int i = 0; i < 16; ++i) {
    ss << names[i] << "=" << std::setw(16) << gpr_[i];
    if (i % 4 == 3)
      ss << "\n";
    else
      ss << " ";
  }
  if (faulted_)
    ss << "FAULT: " << faultMsg_ << "\n";
  if (halted_)
    ss << "HALTED\n";
  return ss.str();
}

} // namespace AIO::Emulator::Windows
