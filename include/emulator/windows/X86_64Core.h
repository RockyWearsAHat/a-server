#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace AIO::Emulator::Windows {

class WinMemory;
class WinAPILayer;

// x86-64 interpreter.
// Register file order matches Intel ModRM encoding:
//   0=RAX  1=RCX  2=RDX  3=RBX  4=RSP  5=RBP  6=RSI  7=RDI
//   8=R8   9=R9  10=R10 11=R11 12=R12 13=R13 14=R14 15=R15
class X86_64Core {
public:
    X86_64Core(WinMemory& mem, WinAPILayer& api);
    ~X86_64Core() = default;

    void Reset();

    // Execute up to `count` instructions.  Returns instructions executed.
    int Execute(int count);

    // Register accessors
    uint64_t  GetGPR(int n) const         { return gpr_[n & 15]; }
    void      SetGPR(int n, uint64_t v)   { gpr_[n & 15] = v; }

    uint64_t  GetRIP()    const { return rip_; }
    void      SetRIP(uint64_t v){ rip_ = v; }
    uint64_t  GetRSP()    const { return gpr_[4]; }
    void      SetRSP(uint64_t v){ gpr_[4] = v; }
    uint64_t  GetRFLAGS() const { return rflags_; }
    void      SetRFLAGS(uint64_t v){ rflags_ = v; }

    struct XMMReg { uint64_t lo = 0, hi = 0; };
    XMMReg& GetXMM(int n)  { return xmm_[n & 15]; }

    bool IsHalted()  const { return halted_; }
    bool IsFaulted() const { return faulted_; }
    std::string GetFaultMessage() const { return faultMsg_; }
    std::string GetStateString() const;

private:
    WinMemory&   mem_;
    WinAPILayer& api_;

    std::array<uint64_t, 16> gpr_{};
    uint64_t rip_    = 0;
    uint64_t rflags_ = 0x202; // IF set
    std::array<XMMReg, 16>   xmm_{};

    bool halted_  = false;
    bool faulted_ = false;
    std::string faultMsg_;

    // Per-instruction prefix state
    struct PfxState {
        bool rex_w = false, rex_r = false, rex_x = false, rex_b = false;
        bool has_rex  = false;
        bool op_size  = false; // 0x66 prefix
        bool addr_size= false; // 0x67 prefix
        bool rep      = false; // F3
        bool repne    = false; // F2
        bool lock     = false;
    };

    // Decoded ModRM result
    struct MRM {
        uint8_t  mod, reg, rm;
        uint64_t ea       = 0;   // effective address (only valid when is_mem)
        bool     is_mem   = false;
        bool     rip_rel  = false;
    };

    // ── Fetch helpers ────────────────────────────────────────────────────
    uint8_t  Fetch8();
    uint16_t Fetch16();
    uint32_t Fetch32();
    uint64_t Fetch64();
    int8_t   FetchS8()  { return static_cast<int8_t>(Fetch8());  }
    int16_t  FetchS16() { return static_cast<int16_t>(Fetch16()); }
    int32_t  FetchS32() { return static_cast<int32_t>(Fetch32()); }

    // ── ModRM / SIB ──────────────────────────────────────────────────────
    MRM DecodeModRM(const PfxState& pfx);

    // ── Register R/W (handles REX extension + high-byte legacy) ─────────
    uint64_t ReadReg (int idx, int size, bool rex_present) const;
    void     WriteReg(int idx, int size, uint64_t value, bool rex_present);

    // Read/write through ModRM result (register or memory)
    uint64_t ReadRM (const MRM& mr, const PfxState& pfx, int size) const;
    void     WriteRM(const MRM& mr, const PfxState& pfx, int size, uint64_t value);

    // Operand size for default 32-bit instructions
    int OpSize(const PfxState& pfx) const {
        if (pfx.rex_w)    return 8;
        if (pfx.op_size)  return 2;
        return 4; // 32-bit default; writes zero-extend to 64
    }

    // ── ALU helpers ──────────────────────────────────────────────────────
    static constexpr uint64_t kCF = 1ull << 0;
    static constexpr uint64_t kPF = 1ull << 2;
    static constexpr uint64_t kAF = 1ull << 4;
    static constexpr uint64_t kZF = 1ull << 6;
    static constexpr uint64_t kSF = 1ull << 7;
    static constexpr uint64_t kTF = 1ull << 8;
    static constexpr uint64_t kIF = 1ull << 9;
    static constexpr uint64_t kDF = 1ull << 10;
    static constexpr uint64_t kOF = 1ull << 11;

    uint64_t SizeMask(int size) const;
    bool     SignBit(uint64_t v, int size)  const;
    void     UpdateFlagsLogical(uint64_t result, int size);
    void     UpdateFlagsAdd(uint64_t a, uint64_t b, uint64_t r, int size);
    void     UpdateFlagsSub(uint64_t a, uint64_t b, uint64_t r, int size);
    uint64_t DoAdd(uint64_t a, uint64_t b, int size);
    uint64_t DoSub(uint64_t a, uint64_t b, int size);
    uint64_t DoAnd(uint64_t a, uint64_t b, int size);
    uint64_t DoOr (uint64_t a, uint64_t b, int size);
    uint64_t DoXor(uint64_t a, uint64_t b, int size);
    uint64_t DoShl(uint64_t v, uint8_t cnt, int size);
    uint64_t DoShr(uint64_t v, uint8_t cnt, int size);
    uint64_t DoSar(uint64_t v, uint8_t cnt, int size);
    uint64_t DoRol(uint64_t v, uint8_t cnt, int size);
    uint64_t DoRor(uint64_t v, uint8_t cnt, int size);

    bool TestCC(uint8_t cc) const;

    // ── Stack helpers ────────────────────────────────────────────────────
    void     Push64(uint64_t v);
    uint64_t Pop64();

    // ── Main decode/execute ──────────────────────────────────────────────
    bool ExecuteOne();

    // Opcode group handlers
    void ExecGroup1 (const PfxState& pfx, const MRM& mr, int size, uint8_t op);
    void ExecGroup2 (const PfxState& pfx, const MRM& mr, int size, uint8_t cnt_src);
    void ExecGroup3 (const PfxState& pfx, const MRM& mr, int size);
    void ExecGroup5 (const PfxState& pfx, const MRM& mr);
    void ExecGroup11(const PfxState& pfx, const MRM& mr, int size);
    void ExecTwoByte(const PfxState& pfx, uint8_t op2);

    void Fault(const std::string& msg);
    void FaultUnknownOpcode(uint8_t op);
};

} // namespace AIO::Emulator::Windows
