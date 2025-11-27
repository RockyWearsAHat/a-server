#pragma once
#include <cstdint>
#include <vector>

namespace AIO::Emulator::GBA {

    class GBAMemory {
    public:
        GBAMemory();
        ~GBAMemory();

        uint32_t debugPC = 0; // Debug PC for logging

        void Reset();
        void LoadGamePak(const std::vector<uint8_t>& data);
        void LoadSave(const std::vector<uint8_t>& data);
        std::vector<uint8_t> GetSaveData() const;

        // Debug/Test helper
        void WriteROM(uint32_t address, uint8_t value) {
            uint32_t offset = address & 0x01FFFFFF;
            if (offset < rom.size()) {
                rom[offset] = value;
            }
        }
        void WriteROM32(uint32_t address, uint32_t value) {
            WriteROM(address, value & 0xFF);
            WriteROM(address + 1, (value >> 8) & 0xFF);
            WriteROM(address + 2, (value >> 16) & 0xFF);
            WriteROM(address + 3, (value >> 24) & 0xFF);
        }

        // IO Registers
        void SetKeyInput(uint16_t value);

        uint8_t Read8(uint32_t address);
        uint16_t Read16(uint32_t address);
        uint32_t Read32(uint32_t address);

        void Write8(uint32_t address, uint8_t value);
        void Write16(uint32_t address, uint16_t value);
        void Write32(uint32_t address, uint32_t value);

        // Internal IO Write (Bypasses Read-Only checks)
        void WriteIORegisterInternal(uint32_t offset, uint16_t value);

        void PerformDMA(int channel);
        void CheckDMA(int timing);
        void UpdateTimers(int cycles);

    private:
        int cycleCount = 0;
        int timerPrescalerCounters[4] = {0};
        uint16_t timerCounters[4] = {0};

        // Flash State
        int flashState = 0; // 0=Idle, 1=Cmd1(AA), 2=Cmd2(55), 3=ID Mode

        // EEPROM State
        enum class EEPROMState {
            Idle,
            ReadCommand,
            ReadAddress,
            ReadDummy,
            ReadData,
            WriteAddress,
            WriteData,
            WriteTermination
        };

        std::vector<uint8_t> eepromData;
        EEPROMState eepromState = EEPROMState::Idle;
        int eepromBitCounter = 0;
        uint64_t eepromBuffer = 0;
        uint32_t eepromAddress = 0;

        // GBA Memory Map Regions
        // 00000000 - 00003FFF: BIOS (16KB)
        // 02000000 - 0203FFFF: On-board WRAM (256KB)
        // 03000000 - 03007FFF: On-chip WRAM (32KB)
        // 04000000 - 040003FE: I/O Registers
        // 05000000 - 050003FF: Palette RAM (1KB)
        // 06000000 - 06017FFF: VRAM (96KB)
        // 07000000 - 070003FF: OAM (1KB)
        // 08000000 - 0DFFFFFF: Game Pak ROM (Wait State 0, 1, 2)
        // 0E000000 - 0E00FFFF: Game Pak SRAM (64KB)

        std::vector<uint8_t> bios;
        std::vector<uint8_t> wram_board;
        std::vector<uint8_t> wram_chip;
        std::vector<uint8_t> io_regs;
        std::vector<uint8_t> palette_ram;
        std::vector<uint8_t> vram;
        std::vector<uint8_t> oam;
        std::vector<uint8_t> rom;
        std::vector<uint8_t> sram;

        // EEPROM Helpers
        uint16_t ReadEEPROM();
        void WriteEEPROM(uint16_t value);
    };

}
