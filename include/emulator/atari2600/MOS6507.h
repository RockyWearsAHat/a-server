#pragma once
// MOS 6507 CPU — 6502 variant with 13-bit address bus (A0–A12).
// Used in the Atari 2600. No NMI or IRQ interrupt pins exposed.
// All behaviour matches the NMOS 6502 except the address space is 8 KB.

#include <cstdint>

namespace Atari2600 {

class Atari2600Memory;

class MOS6507 {
public:
    explicit MOS6507(Atari2600Memory& memory) noexcept;
    ~MOS6507() = default;

    MOS6507(const MOS6507&)            = delete;
    MOS6507& operator=(const MOS6507&) = delete;

    void     Reset();
    int      Step();   // Returns cycles consumed

    // Register accessors
    uint16_t GetPC() const noexcept { return pc_; }
    uint8_t  GetA()  const noexcept { return a_;  }
    uint8_t  GetX()  const noexcept { return x_;  }
    uint8_t  GetY()  const noexcept { return y_;  }
    uint8_t  GetS()  const noexcept { return s_;  }
    uint8_t  GetP()  const noexcept { return p_;  }

    void SetPC(uint16_t pc) noexcept { pc_ = pc; }

    struct State {
        uint16_t pc;
        uint8_t  a, x, y, s, p;
    };

    State SaveState()              const noexcept;
    void  LoadState(const State&)  noexcept;

private:
    Atari2600Memory& mem_;

    uint16_t pc_ = 0;
    uint8_t  a_  = 0;
    uint8_t  x_  = 0;
    uint8_t  y_  = 0;
    uint8_t  s_  = 0xFD;
    uint8_t  p_  = 0x34; // I and unused flags set

    // Status flag masks
    static constexpr uint8_t kFlagC = 0x01;
    static constexpr uint8_t kFlagZ = 0x02;
    static constexpr uint8_t kFlagI = 0x04;
    static constexpr uint8_t kFlagD = 0x08;
    static constexpr uint8_t kFlagB = 0x10;
    static constexpr uint8_t kFlagU = 0x20; // Always 1
    static constexpr uint8_t kFlagV = 0x40;
    static constexpr uint8_t kFlagN = 0x80;

    static constexpr uint16_t kStackBase   = 0x0100;
    static constexpr uint16_t kResetVector = 0xFFFC;
    static constexpr uint16_t kNMIVector   = 0xFFFA; // Exposed for completeness
    static constexpr uint16_t kIRQVector   = 0xFFFE;
    static constexpr uint16_t kAddrMask    = 0x1FFF; // 13-bit bus

    // Flag helpers
    bool GetC()  const noexcept { return (p_ & kFlagC) != 0; }
    bool GetZ()  const noexcept { return (p_ & kFlagZ) != 0; }
    bool GetI()  const noexcept { return (p_ & kFlagI) != 0; }
    bool GetD()  const noexcept { return (p_ & kFlagD) != 0; }
    bool GetV()  const noexcept { return (p_ & kFlagV) != 0; }
    bool GetN()  const noexcept { return (p_ & kFlagN) != 0; }
    void SetC(bool v) noexcept { p_ = v ? (p_ | kFlagC) : (p_ & ~kFlagC); }
    void SetZ(bool v) noexcept { p_ = v ? (p_ | kFlagZ) : (p_ & ~kFlagZ); }
    void SetI(bool v) noexcept { p_ = v ? (p_ | kFlagI) : (p_ & ~kFlagI); }
    void SetD(bool v) noexcept { p_ = v ? (p_ | kFlagD) : (p_ & ~kFlagD); }
    void SetV(bool v) noexcept { p_ = v ? (p_ | kFlagV) : (p_ & ~kFlagV); }
    void SetN(bool v) noexcept { p_ = v ? (p_ | kFlagN) : (p_ & ~kFlagN); }
    void SetNZ(uint8_t v) noexcept { SetN(v & 0x80); SetZ(v == 0); }

    // Memory access (masks to 13-bit address space)
    uint8_t  Read8(uint16_t addr);
    void     Write8(uint16_t addr, uint8_t val);
    uint16_t Read16(uint16_t addr);
    uint16_t Read16Wrapped(uint16_t addr); // Page-wrap for JMP (indirect)

    // Stack
    void    Push8(uint8_t val);
    uint8_t Pop8();
    void    Push16(uint16_t val);
    uint16_t Pop16();

    // Addressing mode helpers (return effective address)
    uint16_t AddrImmediate();
    uint16_t AddrZeroPage();
    uint16_t AddrZeroPageX();
    uint16_t AddrZeroPageY();
    uint16_t AddrAbsolute();
    uint16_t AddrAbsoluteX(int& extraCycles);
    uint16_t AddrAbsoluteY(int& extraCycles);
    uint16_t AddrIndirectX();
    uint16_t AddrIndirectY(int& extraCycles);
    int8_t   AddrRelative();

    // Instruction implementations
    void ExecADC(uint16_t ea);
    void ExecAND(uint16_t ea);
    void ExecASL_A();
    void ExecASL_M(uint16_t ea);
    void ExecBIT(uint16_t ea);
    void ExecCMP(uint16_t ea);
    void ExecCPX(uint16_t ea);
    void ExecCPY(uint16_t ea);
    void ExecDEC(uint16_t ea);
    void ExecEOR(uint16_t ea);
    void ExecINC(uint16_t ea);
    void ExecJMP(uint16_t addr);
    void ExecJSR(uint16_t addr);
    void ExecLDA(uint16_t ea);
    void ExecLDX(uint16_t ea);
    void ExecLDY(uint16_t ea);
    void ExecLSR_A();
    void ExecLSR_M(uint16_t ea);
    void ExecORA(uint16_t ea);
    void ExecROL_A();
    void ExecROL_M(uint16_t ea);
    void ExecROR_A();
    void ExecROR_M(uint16_t ea);
    void ExecSBC(uint16_t ea);
    void ExecSTA(uint16_t ea);
    void ExecSTX(uint16_t ea);
    void ExecSTY(uint16_t ea);

    // Branch helper (returns 1 if page crossed)
    int Branch(bool condition, int8_t offset);
};

} // namespace Atari2600
