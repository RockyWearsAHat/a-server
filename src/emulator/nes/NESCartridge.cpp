#include "emulator/nes/NESCartridge.h"
#include "emulator/common/SaveState.h"
#include <cstring>
#include <stdexcept>

namespace AIO::Emulator::NES {

// ── Mapper 0 (NROM) ───────────────────────────────────────────────────────

class NromMapper final : public IMapper {
public:
    NromMapper(const std::vector<uint8_t>& prg,
               const std::vector<uint8_t>& chr,
               std::array<uint8_t, 8192>&  chrRam)
        : prg_(prg), chr_(chr), chrRam_(chrRam),
          is32k_(prg.size() > 0x4000) {}

    void Reset() override {}

    [[nodiscard]] uint8_t ReadPRG(uint16_t addr) override {
        if (addr < 0x8000) return 0xFF;
        const uint32_t off = is32k_ ? (addr - 0x8000) & 0x7FFF : (addr - 0x8000) & 0x3FFF;
        return prg_[off];
    }

    void WritePRG(uint16_t /*addr*/, uint8_t /*val*/) override {}

    [[nodiscard]] uint8_t ReadCHR(uint16_t addr) override {
        if (addr >= 0x2000) return 0xFF;
        return chr_.empty() ? chrRam_[addr] : chr_[addr % chr_.size()];
    }

    void WriteCHR(uint16_t addr, uint8_t val) override {
        if (addr < 0x2000) chrRam_[addr] = val;
    }

    [[nodiscard]] MirrorMode GetMirrorMode() const noexcept override {
        return mirrorMode_;
    }

    void SetMirrorMode(MirrorMode m) { mirrorMode_ = m; }

private:
    const std::vector<uint8_t>& prg_;
    const std::vector<uint8_t>& chr_;
    std::array<uint8_t, 8192>&  chrRam_;
    bool       is32k_      = false;
    MirrorMode mirrorMode_ = MirrorMode::Horizontal;
};

// ── Mapper 1 (MMC1) ───────────────────────────────────────────────────────

class Mmc1Mapper final : public IMapper {
public:
    Mmc1Mapper(const std::vector<uint8_t>& prg,
               const std::vector<uint8_t>& chr,
               std::array<uint8_t, 8192>&  chrRam)
        : prg_(prg), chr_(chr), chrRam_(chrRam) {
        lastPrgBank_ = static_cast<uint8_t>(prg_.size() / 0x4000 - 1);
    }

    void Reset() override {
        shiftReg_   = 0x10;
        shiftCount_ = 0;
        control_    = 0x0C;
        prgBank_    = 0;
        chrBank0_   = 0;
        chrBank1_   = 0;
    }

    [[nodiscard]] uint8_t ReadPRG(uint16_t addr) override {
        if (addr < 0x8000) return 0xFF;
        uint32_t bank = 0;
        if (addr < 0xC000) {
            bank = (control_ & 0x0C) == 0x08 ? 0 :
                   (control_ & 0x0C) == 0x0C ? prgBank_ : prgBank_ & ~1u;
        } else {
            bank = (control_ & 0x0C) == 0x0C ? lastPrgBank_ :
                   (control_ & 0x0C) == 0x08 ? lastPrgBank_ : prgBank_ | 1u;
        }
        const uint32_t off = addr < 0xC000 ? (addr - 0x8000) : (addr - 0xC000);
        return prg_[(bank * 0x4000 + off) % prg_.size()];
    }

    void WritePRG(uint16_t addr, uint8_t val) override {
        if (addr < 0x8000) return;
        if (val & 0x80) { shiftReg_ = 0x10; shiftCount_ = 0; control_ |= 0x0C; return; }
        shiftReg_ = static_cast<uint8_t>(((val & 1) << 4) | (shiftReg_ >> 1));
        if (++shiftCount_ == 5) {
            const uint8_t reg = static_cast<uint8_t>((addr >> 13) & 0x03);
            switch (reg) {
                case 0: control_  = shiftReg_ & 0x1F; break;
                case 1: chrBank0_ = shiftReg_ & 0x1F; break;
                case 2: chrBank1_ = shiftReg_ & 0x1F; break;
                case 3: prgBank_  = shiftReg_ & 0x0F; break;
            }
            shiftReg_ = 0x10; shiftCount_ = 0;
        }
    }

    [[nodiscard]] uint8_t ReadCHR(uint16_t addr) override {
        if (addr >= 0x2000) return 0xFF;
        if (chr_.empty()) return chrRam_[addr];
        const bool mode4k = (control_ & 0x10) != 0;
        const uint32_t bank = addr < 0x1000
            ? (mode4k ? chrBank0_ : (chrBank0_ & ~1u))
            : (mode4k ? chrBank1_ : (chrBank0_  | 1u));
        return chr_[(bank * 0x1000 + (addr & 0x0FFF)) % chr_.size()];
    }

    void WriteCHR(uint16_t addr, uint8_t val) override {
        if (addr < 0x2000) chrRam_[addr] = val;
    }

    [[nodiscard]] MirrorMode GetMirrorMode() const noexcept override {
        switch (control_ & 0x03) {
            case 0: return MirrorMode::SingleLow;
            case 1: return MirrorMode::SingleHigh;
            case 2: return MirrorMode::Vertical;
            case 3: return MirrorMode::Horizontal;
            default: return MirrorMode::Horizontal;
        }
    }

    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override {
        w.WriteU8(control_); w.WriteU8(prgBank_); w.WriteU8(chrBank0_);
        w.WriteU8(chrBank1_); w.WriteU8(shiftReg_); w.WriteU8(shiftCount_);
    }

    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override {
        control_ = r.ReadU8(); prgBank_ = r.ReadU8(); chrBank0_ = r.ReadU8();
        chrBank1_ = r.ReadU8(); shiftReg_ = r.ReadU8(); shiftCount_ = r.ReadU8();
    }

private:
    const std::vector<uint8_t>& prg_;
    const std::vector<uint8_t>& chr_;
    std::array<uint8_t, 8192>&  chrRam_;
    uint8_t control_    = 0x0C;
    uint8_t prgBank_    = 0;
    uint8_t lastPrgBank_= 0;
    uint8_t chrBank0_   = 0;
    uint8_t chrBank1_   = 0;
    uint8_t shiftReg_   = 0x10;
    uint8_t shiftCount_ = 0;
};

// ── Mapper 2 (UxROM) ──────────────────────────────────────────────────────

class UxromMapper final : public IMapper {
public:
    UxromMapper(const std::vector<uint8_t>& prg,
                const std::vector<uint8_t>& chr,
                std::array<uint8_t, 8192>&  chrRam)
        : prg_(prg), chr_(chr), chrRam_(chrRam) {}

    void Reset() override { prgBank_ = 0; }

    [[nodiscard]] uint8_t ReadPRG(uint16_t addr) override {
        if (addr < 0x8000) return 0xFF;
        const uint32_t size16k = static_cast<uint32_t>(prg_.size()) / 0x4000;
        const uint32_t bank = addr < 0xC000 ? prgBank_ : size16k - 1;
        return prg_[(bank * 0x4000 + (addr & 0x3FFF)) % prg_.size()];
    }

    void WritePRG(uint16_t addr, uint8_t val) override {
        if (addr >= 0x8000) prgBank_ = val & 0x0F;
    }

    [[nodiscard]] uint8_t ReadCHR(uint16_t addr) override {
        if (addr >= 0x2000) return 0xFF;
        return chr_.empty() ? chrRam_[addr] : chr_[addr % chr_.size()];
    }

    void WriteCHR(uint16_t addr, uint8_t val) override {
        if (addr < 0x2000) chrRam_[addr] = val;
    }

    [[nodiscard]] MirrorMode GetMirrorMode() const noexcept override {
        return mirrorMode_;
    }

    void SetMirrorMode(MirrorMode m) { mirrorMode_ = m; }

    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override { w.WriteU8(prgBank_); }
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override { prgBank_ = r.ReadU8(); }

private:
    const std::vector<uint8_t>& prg_;
    const std::vector<uint8_t>& chr_;
    std::array<uint8_t, 8192>&  chrRam_;
    uint8_t    prgBank_    = 0;
    MirrorMode mirrorMode_ = MirrorMode::Horizontal;
};

// ── Mapper 3 (CNROM) ──────────────────────────────────────────────────────

class CnromMapper final : public IMapper {
public:
    CnromMapper(const std::vector<uint8_t>& prg,
                const std::vector<uint8_t>& chr,
                std::array<uint8_t, 8192>&  chrRam)
        : prg_(prg), chr_(chr), chrRam_(chrRam) {}

    void Reset() override { chrBank_ = 0; }

    [[nodiscard]] uint8_t ReadPRG(uint16_t addr) override {
        if (addr < 0x8000) return 0xFF;
        return prg_[(addr - 0x8000) % prg_.size()];
    }

    void WritePRG(uint16_t addr, uint8_t val) override {
        if (addr >= 0x8000) chrBank_ = val & 0x03;
    }

    [[nodiscard]] uint8_t ReadCHR(uint16_t addr) override {
        if (addr >= 0x2000) return 0xFF;
        return chr_.empty() ? chrRam_[addr]
            : chr_[(static_cast<uint32_t>(chrBank_) * 0x2000 + addr) % chr_.size()];
    }

    void WriteCHR(uint16_t addr, uint8_t val) override {
        if (addr < 0x2000) chrRam_[addr] = val;
    }

    [[nodiscard]] MirrorMode GetMirrorMode() const noexcept override {
        return mirrorMode_;
    }

    void SetMirrorMode(MirrorMode m) { mirrorMode_ = m; }

    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override { w.WriteU8(chrBank_); }
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override { chrBank_ = r.ReadU8(); }

private:
    const std::vector<uint8_t>& prg_;
    const std::vector<uint8_t>& chr_;
    std::array<uint8_t, 8192>&  chrRam_;
    uint8_t    chrBank_    = 0;
    MirrorMode mirrorMode_ = MirrorMode::Horizontal;
};

// ── Mapper 4 (MMC3) ───────────────────────────────────────────────────────

class Mmc3Mapper final : public IMapper {
public:
    Mmc3Mapper(const std::vector<uint8_t>& prg,
               const std::vector<uint8_t>& chr,
               std::array<uint8_t, 8192>&  chrRam)
        : prg_(prg), chr_(chr), chrRam_(chrRam) {}

    void Reset() override {
        bankSel_    = 0;
        std::memset(bankReg_, 0, sizeof(bankReg_));
        irqLatch_   = 0;
        irqCounter_ = 0;
        irqEnable_  = false;
        irqPending_ = false;
        mirrorMode_ = MirrorMode::Horizontal;
    }

    [[nodiscard]] uint8_t ReadPRG(uint16_t addr) override {
        if (addr < 0x8000) return 0xFF;
        const uint32_t tot8k = static_cast<uint32_t>(prg_.size()) / 0x2000;
        uint32_t bank = 0;
        if      (addr < 0xA000) bank = (bankSel_ & 0x40) ? tot8k - 2 : bankReg_[6];
        else if (addr < 0xC000) bank = bankReg_[7];
        else if (addr < 0xE000) bank = (bankSel_ & 0x40) ? bankReg_[6] : tot8k - 2;
        else                    bank = tot8k - 1;
        return prg_[((bank * 0x2000) + (addr & 0x1FFF)) % prg_.size()];
    }

    void WritePRG(uint16_t addr, uint8_t val) override {
        if (addr < 0x8000) return;
        const bool even = (addr & 1) == 0;
        if        (addr < 0xA000) {
            if (even) bankSel_ = val;
            else      bankReg_[bankSel_ & 0x07] = val;
        } else if (addr < 0xC000) {
            if (!even) mirrorMode_ = (val & 1) == 0 ? MirrorMode::Vertical : MirrorMode::Horizontal;
        } else if (addr < 0xE000) {
            if (even) irqLatch_   = val;
            else      irqCounter_ = 0;
        } else {
            irqEnable_ = !even;
            if (!irqEnable_) irqPending_ = false;
        }
    }

    [[nodiscard]] uint8_t ReadCHR(uint16_t addr) override {
        if (addr >= 0x2000) return 0xFF;
        if (chr_.empty()) return chrRam_[addr];
        const uint32_t tot1k = static_cast<uint32_t>(chr_.size()) / 0x0400;
        const bool invert    = (bankSel_ & 0x80) != 0;
        uint32_t bank = 0;
        if (!invert) {
            if      (addr < 0x0800) bank = bankReg_[0] & ~1u;
            else if (addr < 0x1000) bank = bankReg_[0] |  1u;
            else if (addr < 0x1400) bank = bankReg_[2];
            else if (addr < 0x1800) bank = bankReg_[3];
            else if (addr < 0x1C00) bank = bankReg_[4];
            else                    bank = bankReg_[5];
        } else {
            if      (addr < 0x0400) bank = bankReg_[2];
            else if (addr < 0x0800) bank = bankReg_[3];
            else if (addr < 0x0C00) bank = bankReg_[4];
            else if (addr < 0x1000) bank = bankReg_[5];
            else if (addr < 0x1800) bank = bankReg_[0] & ~1u;
            else                    bank = bankReg_[0] |  1u;
        }
        return chr_[((bank * 0x0400) + (addr & 0x03FF)) % (tot1k * 0x0400)];
    }

    void WriteCHR(uint16_t addr, uint8_t val) override {
        if (addr < 0x2000) chrRam_[addr] = val;
    }

    [[nodiscard]] MirrorMode GetMirrorMode() const noexcept override { return mirrorMode_; }

    void ScanlineIRQ() override {
        if (irqCounter_ == 0) {
            irqCounter_ = irqLatch_;
        } else {
            --irqCounter_;
        }
        if (irqCounter_ == 0 && irqEnable_) irqPending_ = true;
    }

    [[nodiscard]] bool IrqPending() const noexcept override { return irqPending_; }

    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override {
        w.WriteU8(bankSel_);
        for (auto b : bankReg_) w.WriteU8(b);
        w.WriteU8(irqLatch_); w.WriteU8(irqCounter_);
        w.WriteBool(irqEnable_); w.WriteBool(irqPending_);
    }

    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override {
        bankSel_ = r.ReadU8();
        for (auto& b : bankReg_) b = r.ReadU8();
        irqLatch_ = r.ReadU8(); irqCounter_ = r.ReadU8();
        irqEnable_ = r.ReadBool(); irqPending_ = r.ReadBool();
    }

private:
    const std::vector<uint8_t>& prg_;
    const std::vector<uint8_t>& chr_;
    std::array<uint8_t, 8192>&  chrRam_;
    uint8_t    bankSel_    = 0;
    uint8_t    bankReg_[8] = {};
    uint8_t    irqLatch_   = 0;
    uint8_t    irqCounter_ = 0;
    bool       irqEnable_  = false;
    bool       irqPending_ = false;
    MirrorMode mirrorMode_ = MirrorMode::Horizontal;
};

// ── NESCartridge ──────────────────────────────────────────────────────────

NESCartridge::NESCartridge() = default;

void NESCartridge::ParseHeader(std::span<const uint8_t> data) {
    if (data.size() < 16)
        throw std::invalid_argument("ROM too small to contain iNES header");
    if (data[0] != 'N' || data[1] != 'E' || data[2] != 'S' || data[3] != 0x1A)
        throw std::invalid_argument("Invalid iNES magic bytes");

    const uint8_t flags6 = data[6];
    const uint8_t flags7 = data[7];
    mapperId_ = static_cast<uint8_t>(((flags6 >> 4) & 0x0F) | (flags7 & 0xF0));

    if (flags6 & 0x08)       mirrorMode_ = MirrorMode::FourScreen;
    else if (flags6 & 0x01)  mirrorMode_ = MirrorMode::Vertical;
    else                     mirrorMode_ = MirrorMode::Horizontal;

    const uint32_t prgSize      = data[4] * 16384u;
    const uint32_t chrSize      = data[5] * 8192u;
    const uint32_t trainerBytes = (flags6 & 0x04) ? 512u : 0u;
    const uint32_t dataOffset   = 16u + trainerBytes;

    if (data.size() < dataOffset + prgSize + chrSize)
        throw std::invalid_argument("ROM data truncated");

    prgRom_.assign(data.begin() + dataOffset,
                   data.begin() + dataOffset + prgSize);
    if (chrSize > 0) {
        chrRom_.assign(data.begin() + dataOffset + prgSize,
                       data.begin() + dataOffset + prgSize + chrSize);
        hasChrRam_ = false;
    } else {
        chrRom_.clear();
        hasChrRam_ = true;
    }
    chrRam_.fill(0);
}

void NESCartridge::InstantiateMapper() {
    switch (mapperId_) {
        case 0: {
            auto m = std::make_unique<NromMapper>(prgRom_, chrRom_, chrRam_);
            m->SetMirrorMode(mirrorMode_);
            mapper_ = std::move(m);
            break;
        }
        case 1:
            mapper_ = std::make_unique<Mmc1Mapper>(prgRom_, chrRom_, chrRam_);
            break;
        case 2: {
            auto m = std::make_unique<UxromMapper>(prgRom_, chrRom_, chrRam_);
            m->SetMirrorMode(mirrorMode_);
            mapper_ = std::move(m);
            break;
        }
        case 3: {
            auto m = std::make_unique<CnromMapper>(prgRom_, chrRom_, chrRam_);
            m->SetMirrorMode(mirrorMode_);
            mapper_ = std::move(m);
            break;
        }
        case 4:
            mapper_ = std::make_unique<Mmc3Mapper>(prgRom_, chrRom_, chrRam_);
            break;
        default:
            throw std::invalid_argument("Unsupported mapper ID");
    }
}

void NESCartridge::Load(std::span<const uint8_t> romData) {
    ParseHeader(romData);
    InstantiateMapper();
    loaded_  = true;
    romName_ = "";
}

void NESCartridge::Reset() {
    if (mapper_) mapper_->Reset();
}

uint8_t NESCartridge::CpuRead(uint16_t addr) {
    if (!loaded_ || !mapper_) return 0xFF;
    return mapper_->ReadPRG(addr);
}

void NESCartridge::CpuWrite(uint16_t addr, uint8_t value) {
    if (!loaded_ || !mapper_) return;
    mapper_->WritePRG(addr, value);
}

uint8_t NESCartridge::PpuRead(uint16_t addr) {
    if (!loaded_ || !mapper_) return 0xFF;
    return mapper_->ReadCHR(addr);
}

void NESCartridge::PpuWrite(uint16_t addr, uint8_t value) {
    if (!loaded_ || !mapper_) return;
    mapper_->WriteCHR(addr, value);
}

MirrorMode NESCartridge::GetMirrorMode() const noexcept {
    if (!mapper_) return mirrorMode_;
    return mapper_->GetMirrorMode();
}

void NESCartridge::NotifyScanline() {
    if (mapper_) mapper_->ScanlineIRQ();
}

bool NESCartridge::IrqPending() const noexcept {
    if (!mapper_) return false;
    return mapper_->IrqPending();
}

void NESCartridge::SaveState(AIO::Emulator::Common::SaveStateWriter& w) const {
    if (mapper_) mapper_->SaveState(w);
    w.WriteBytes(chrRam_.data(), chrRam_.size());
}

void NESCartridge::LoadState(AIO::Emulator::Common::SaveStateReader& r) {
    if (mapper_) mapper_->LoadState(r);
    r.ReadBytes(chrRam_.data(), chrRam_.size());
}

} // namespace AIO::Emulator::NES

