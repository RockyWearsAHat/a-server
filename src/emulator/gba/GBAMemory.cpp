#include "emulator/gba/GBAMemory.h"
#include <cstring>
#include <iostream>
#include <iomanip>

namespace AIO::Emulator::GBA {

    // Static delay counter for EEPROM writes
    static int eepromWriteDelay = 0;

    GBAMemory::GBAMemory() {
        // Initialize memory vectors with correct sizes
        bios.resize(16 * 1024);
        wram_board.resize(256 * 1024);
        wram_chip.resize(32 * 1024);
        io_regs.resize(0x400);
        palette_ram.resize(1 * 1024);
        vram.resize(96 * 1024);
        oam.resize(1 * 1024);
        // ROM and SRAM sizes depend on the loaded game, but we can set defaults
        rom.resize(32 * 1024 * 1024); // Max 32MB
        sram.resize(64 * 1024);
        eepromData.resize(512, 0xFF); // 512 Bytes (4Kbit) default for SMA2
    }

    GBAMemory::~GBAMemory() = default;

    void GBAMemory::Reset() {
        std::fill(wram_board.begin(), wram_board.end(), 0);
        std::fill(wram_chip.begin(), wram_chip.end(), 0);
        std::fill(io_regs.begin(), io_regs.end(), 0);
        std::fill(palette_ram.begin(), palette_ram.end(), 0);
        std::fill(vram.begin(), vram.end(), 0);
        std::fill(oam.begin(), oam.end(), 0);
        
        // Initialize KEYINPUT to 0x03FF (All Released)
        if (io_regs.size() > 0x131) {
            io_regs[0x130] = 0xFF;
            io_regs[0x131] = 0x03;
        }

        eepromState = EEPROMState::Idle;
        eepromBitCounter = 0;
        eepromBuffer = 0;
        eepromAddress = 0;

        // Debug: Check EEPROM content
        if (eepromData.empty()) {
            eepromData.resize(512, 0xFF);
        }
    }

    void GBAMemory::LoadGamePak(const std::vector<uint8_t>& data) {
        if (data.size() > rom.size()) {
            rom.resize(data.size());
        }
        std::copy(data.begin(), data.end(), rom.begin());
    }

    void GBAMemory::LoadSave(const std::vector<uint8_t>& data) {
        if (!data.empty()) {
            eepromData = data;
            // Ensure minimum size
            if (eepromData.size() < 512) eepromData.resize(512, 0xFF);
        }
    }

    std::vector<uint8_t> GBAMemory::GetSaveData() const {
        return eepromData;
    }

    void GBAMemory::SetKeyInput(uint16_t value) {
        // KEYINPUT is at 0x4000130.
        // In io_regs (which starts at 0x4000000), this is offset 0x130.
        // Little endian storage.
        if (io_regs.size() > 0x131) {
            io_regs[0x130] = value & 0xFF;
            io_regs[0x131] = (value >> 8) & 0xFF;
        }
    }

    uint8_t GBAMemory::Read8(uint32_t address) {
        switch (address >> 24) {
            case 0x00: // BIOS
                if (address < bios.size()) return bios[address];
                break;
            case 0x02: // WRAM (Board)
                return wram_board[address & 0x3FFFF];
            case 0x03: // WRAM (Chip)
                return wram_chip[address & 0x7FFF];
            case 0x04: // IO Registers
            {
                uint32_t offset = address & 0x3FF;
                uint8_t val = 0;
                if (offset < io_regs.size()) val = io_regs[offset];
                return val;
            }
            case 0x05: // Palette RAM
            {
                uint32_t offset = address & 0x3FF;
                if (offset < palette_ram.size()) return palette_ram[offset];
                break;
            }
            case 0x06: // VRAM
            {
                uint32_t offset = address & 0x17FFF; // 96KB
                if (offset < vram.size()) return vram[offset];
                else if ((offset & 0x1FFFF) < vram.size()) return vram[offset & 0x1FFFF];
                break;
            }
            case 0x07: // OAM
            {
                uint32_t offset = address & 0x3FF;
                if (offset < oam.size()) return oam[offset];
                break;
            }
            case 0x08: // Game Pak ROM
            case 0x09:
            case 0x0A:
            case 0x0B:
            case 0x0C:
            {
                 uint32_t offset = address & 0x01FFFFFF;
                 if (offset < rom.size()) {
                     return rom[offset];
                 }
                 break;
            }
            case 0x0D: // EEPROM
            {
                // std::cout << "Read8 EEPROM (0x" << std::hex << address << ")" << std::endl;
                return 1;
            }
            case 0x0E: // SRAM/Flash
            {
                std::cout << "Read8 SRAM/Flash (0x" << std::hex << address << ")" << std::endl;
                // Flash ID Mode Read
                if (flashState == 3) {
                    uint32_t offset = address & 0xFFFF;
                    if (offset == 0) return 0xC2; // Maker: Macronix
                    if (offset == 1) return 0x1C; // Device: 512K
                }
                return 0;
            }
        }
        return 0;
    }

    uint16_t GBAMemory::Read16(uint32_t address) {
        // EEPROM Handling
        if (address >= 0x0D000000 && address < 0x0E000000) {
            std::cout << "Read16 EEPROM (0x" << std::hex << address << ")" << std::endl;
            return ReadEEPROM();
        }

        // Debug: Watch for KEYINPUT reads
        if (address == 0x04000130) {
             std::cout << "Read KEYINPUT (0x4000130) PC=" << std::hex << debugPC << " Val=" << (io_regs[0x130] | (io_regs[0x131] << 8)) << std::dec << std::endl;
        }

        // Debug: Watch for IF/IE reads
        if (address == 0x04000200 || address == 0x04000202) {
             // std::cout << "Read16 IO (0x" << std::hex << address << ") PC=" << debugPC << std::dec << std::endl;
        }

        // Little Endian
        uint16_t val = Read8(address) | (Read8(address + 1) << 8);

        // Timer Counters (Read from internal state)
        if ((address & 0xFF000000) == 0x04000000) {
             uint32_t offset = address & 0x3FF;
             
             if (offset >= 0x100 && offset <= 0x10F) {
                 int timerIdx = (offset - 0x100) / 4;
                 if ((offset % 4) == 0) { // TMxCNT_L
                     return timerCounters[timerIdx];
                 }
             }
        }

        return val;
    }

    uint32_t GBAMemory::Read32(uint32_t address) {
        // Little Endian
        uint32_t val = Read8(address) | (Read8(address + 1) << 8) | (Read8(address + 2) << 16) | (Read8(address + 3) << 24);
        
        if (address == 0x03007FF8) {
             // std::cout << "Read32 BIOS Flags (0x03007FF8): " << std::hex << val << " PC=" << debugPC << std::dec << std::endl;
        }

        if (address >= 0x03007A00 && address <= 0x03007A20) {
             // std::cout << "Read32 IRQ Table (0x" << std::hex << address << "): " << val << " PC=" << debugPC << std::dec << std::endl;
        }
        
        /*
        if ((address & 0xFFFFFFF0) == 0x03007FF0) {
             std::cout << "Read32 WRAM (0x" << std::hex << address << "): " << val << " PC=" << debugPC << std::dec << std::endl;
        }
        */
        return val;
    }

    void GBAMemory::Write8(uint32_t address, uint8_t value) {
        // Check for IO Writes (Mirrored)
        if ((address & 0xFF000000) == 0x04000000) {
            uint32_t offset = address & 0x3FF;
            
            std::cout << "IO Write8: Addr=0x" << std::hex << address << " Val=0x" << (int)value << " PC=" << debugPC << std::dec << std::endl;

            if (offset == 0x00 || offset == 0x01) {
                 std::cout << "DISPCNT Write8: 0x" << std::hex << (int)value << " at " << address << " PC=" << debugPC << std::dec << std::endl;
            }

            if (offset == 0x208) {
                std::cout << "IO Write8: IME = " << std::hex << (int)value << " at " << address << " PC=" << debugPC << std::dec << std::endl;
            }
            if (offset == 0x200 || offset == 0x201) {
                std::cout << "IO Write8: IE byte = " << std::hex << (int)value << " at " << address << " PC=" << debugPC << std::dec << std::endl;
            }
            
        }
        
        
        if (address == 0x03002b64 || address == 0x03002b65 || address == 0x03002BE5) {
             // std::cout << "Write8 to WaitFlag (0x" << std::hex << address << "): " << (int)value << " PC=" << debugPC << std::dec << std::endl;
        }


        switch (address >> 24) {
            case 0x02: // WRAM (Board)
                wram_board[address & 0x3FFFF] = value;
                break;
            case 0x03: // WRAM (Chip)
                wram_chip[address & 0x7FFF] = value;
                break;
            case 0x04: // IO Registers
            {
                uint32_t offset = address & 0x3FF;
                
                // Handle IF (Interrupt Request) - Write 1 to Clear (Byte Write)
                if (offset == 0x202 || offset == 0x203) {
                    if (offset < io_regs.size()) {
                        io_regs[offset] &= ~value;
                    }
                    // Do not perform the default write
                } else {
                    // Protect DISPSTAT (0x04) Read-Only bits (0-2)
                    if (offset == 0x04) {
                        uint8_t currentVal = io_regs[offset];
                        uint8_t readOnlyMask = 0x07; // Bits 0, 1, 2
                        value = (value & ~readOnlyMask) | (currentVal & readOnlyMask);
                    }

                    if (offset < io_regs.size()) io_regs[offset] = value;
                }

                // Handle DMA Enable via 8-bit write to high byte of CNT_H
                // DMAxCNT_H is at 0xBA, 0xC6, 0xD2, 0xDE.
                // High byte is at 0xBB, 0xC7, 0xD3, 0xDF.
                if (offset == 0xBB || offset == 0xC7 || offset == 0xD3 || offset == 0xDF) {
                    if (value & 0x80) { // Bit 15 (Enable) is Bit 7 of high byte
                        int channel = 0;
                        if (offset == 0xBB) channel = 0;
                        else if (offset == 0xC7) channel = 1;
                        else if (offset == 0xD3) channel = 2;
                        else if (offset == 0xDF) channel = 3;

                        // We need the full control register to check timing
                        // The low byte is at offset-1
                        uint16_t control = io_regs[offset-1] | (value << 8);
                        int timing = (control >> 12) & 3;
                        if (timing == 0) PerformDMA(channel);
                    }
                }
                break;
            }
            case 0x05: // Palette RAM
            {
                uint32_t offset = address & 0x3FF;
                if (offset < palette_ram.size()) palette_ram[offset] = value;
                break;
            }
            case 0x06: // VRAM
            {
                uint32_t offset = address & 0x17FFF;
                if (offset < vram.size()) vram[offset] = value;
                else if ((offset & 0x1FFFF) < vram.size()) vram[offset & 0x1FFFF] = value;
                break;
            }
            case 0x07: // OAM
            {
                uint32_t offset = address & 0x3FF;
                if (offset < oam.size()) oam[offset] = value;
                break;
            }
            // SRAM/Flash
            if (address >= 0x0E000000 && address < 0x0F000000) {
                uint32_t offset = address & 0xFFFF;
                
                // Flash Command State Machine
                if (offset == 0x5555 && value == 0xAA) {
                    flashState = 1;
                } else if (offset == 0x2AAA && value == 0x55 && flashState == 1) {
                    flashState = 2;
                } else if (offset == 0x5555 && value == 0x90 && flashState == 2) {
                    flashState = 3; // Enter ID Mode
                } else if (value == 0xF0) {
                    flashState = 0; // Reset / Exit ID Mode
                }
                return; 
            }
            
            // EEPROM
            if (address >= 0x0D000000 && address < 0x0E000000) {
                return;
            }
        }
    }

    void GBAMemory::Write16(uint32_t address, uint16_t value) {
        if ((address & 0xFFFFFFF0) == 0x03007FF0) {
             std::cout << "Write16 WRAM (0x" << std::hex << address << "): " << value << " PC=" << debugPC << std::dec << std::endl;
        }

        // Debug: Log Palette Writes
        if ((address & 0xFF000000) == 0x05000000) {
             // Only log the first few to avoid spam, or log all?
             // Let's log all for now, but maybe limit it if it's too much.
             // Actually, palette load is usually 512 bytes (256 writes). That's fine.
             std::cout << "Palette Write16: Addr=0x" << std::hex << address << " Val=0x" << value << " PC=" << debugPC << std::dec << std::endl;
        }

        // Check for IO Writes (Mirrored)
        if ((address & 0xFF000000) == 0x04000000) {
            uint32_t offset = address & 0x3FF;
            
            if (offset == 0x208) {
                std::cout << "IO Write16: IME = " << std::hex << value << " at " << address << " PC=" << debugPC << std::dec << std::endl;
            }
            if (offset == 0x200) {
                std::cout << "IO Write16: IE = " << std::hex << value << " at " << address << " PC=" << debugPC << std::dec << std::endl;
            }
            if (offset == 0x04) {
                std::cout << "IO Write16: DISPSTAT = " << std::hex << value << " at " << address << " PC=" << debugPC << std::dec << std::endl;
            }
            
        }
        
        // Debug: Watch for writes to the wait flag
        /*
        if ((address & 0xFFFFFFFE) == 0x03002b64) {
             std::cout << "Write16 to WaitFlag (0x3002b64): " << std::hex << value << " at PC=" << debugPC << std::dec << std::endl;
        }

        if (address == 0x04000208) {
            // Keep original strict check just in case
            std::cout << "IO Write16 (Strict): IME = " << std::hex << value << " PC=" << debugPC << std::dec << std::endl;
        }
        */

        if ((address & 0xFF000000) == 0x04000000) {
            uint32_t offset = address & 0x3FF;
            
            if (offset == 0x00) {
                std::cout << "DISPCNT Write: 0x" << std::hex << value << std::dec << " at PC=" << debugPC << std::endl;
            }
            
            // DISPSTAT Write Masking
            if (offset == 0x04) {
                // DISPSTAT (0x04)
                // Bits 0-2 are Read-Only (VBlank, HBlank, VCount Match)
                // Bits 3-5 are R/W (IRQ Enables)
                // Bits 8-15 are R/W (VCount Setting)
                // We must preserve the Read-Only bits from the current value
                uint16_t currentVal = io_regs[offset] | (io_regs[offset+1] << 8);
                uint16_t readOnlyMask = 0x0007; // Bits 0, 1, 2
                value = (value & ~readOnlyMask) | (currentVal & readOnlyMask);
            }

            if (offset >= 0x100 && offset <= 0x10F) {
                 int timerIdx = (offset - 0x100) / 4;
                 if ((offset % 4) == 2) { // TMxCNT_H (Control)
                     uint16_t oldControl = io_regs[offset] | (io_regs[offset+1] << 8);
                     bool wasEnabled = oldControl & 0x80;
                     bool nowEnabled = value & 0x80;
                     
                     std::cout << "Timer " << timerIdx << " Control Write: 0x" << std::hex << value << " Enabled=" << nowEnabled << " IRQ=" << ((value & 0x40) != 0) << std::dec << std::endl;

                     if (!wasEnabled && nowEnabled) {
                         // Start Timer: Reload
                         uint16_t reload = io_regs[offset-2] | (io_regs[offset-1] << 8);
                         timerCounters[timerIdx] = reload;
                         timerPrescalerCounters[timerIdx] = 0;
                     }
                 }
            }
        }

        // EEPROM Handling
        if (address >= 0x0D000000 && address < 0x0E000000) {
            WriteEEPROM(value);
            return;
        }

        // Little Endian
        Write8(address, value & 0xFF);
        Write8(address + 1, (value >> 8) & 0xFF);


    }

    void GBAMemory::Write32(uint32_t address, uint32_t value) {
        /*
        if (address == 0x030004f0) {
             std::cout << "Write32 to 0x30004f0: Val=0x" << std::hex << value << " PC=" << debugPC << std::endl;
        }
        */
        
        if (address == 0x03007FFC) {
             std::cout << "Write32 User IRQ Handler (0x03007FFC): 0x" << std::hex << value << " PC=" << debugPC << std::dec << std::endl;
        }

        /*
        if ((address & 0xFFFFFFF0) == 0x03007FF0) {
             std::cout << "Write32 WRAM (0x" << std::hex << address << "): " << value << " PC=" << debugPC << std::dec << std::endl;
        }
        */

        if (address >= 0x03007A00 && address <= 0x03007A20) {
             // std::cout << "Write32 IRQ Table (0x" << std::hex << address << "): " << value << " PC=" << debugPC << std::dec << std::endl;
        }
        
        /*
        if ((address & 0xFFFFFFFC) == 0x03002b64) {
             std::cout << "Write32 to WaitFlag (0x3002b64): " << std::hex << value << " at " << address << " PC=" << debugPC << std::dec << std::endl;
        }
        */
        
        // Handle IF (Interrupt Request) - Write 1 to Clear (Upper 16 bits of 0x200)
        // Handled by Write8 automatically

        Write8(address, value & 0xFF);
        Write8(address + 1, (value >> 8) & 0xFF);
        Write8(address + 2, (value >> 16) & 0xFF);
        Write8(address + 3, (value >> 24) & 0xFF);

        if ((address & 0xFF000000) == 0x04000000) {
            uint32_t offset = address & 0x3FF;
            
            /*
            if (offset == 0x200) {
                 // IE/IF Write
                 std::cout << "IO Write32: IE/IF (0x200) = 0x" << std::hex << value << std::endl;
            }
            if (offset == 0x208) {
                 // IME Write
                 std::cout << "IO Write32: IME (0x208) = 0x" << std::hex << value << std::endl;
            }
            */

        }
    }

    void GBAMemory::WriteIORegisterInternal(uint32_t offset, uint16_t value) {
        if (offset + 1 < io_regs.size()) {
            io_regs[offset] = value & 0xFF;
            io_regs[offset + 1] = (value >> 8) & 0xFF;
        }
    }

    void GBAMemory::CheckDMA(int timing) {
        for (int i = 0; i < 4; ++i) {
            uint32_t baseOffset = 0xB0 + (i * 0xC);
            uint16_t control = io_regs[baseOffset+10] | (io_regs[baseOffset+11] << 8);
            
            if (control & 0x8000) { // Enabled
                int dmaTiming = (control >> 12) & 3;
                if (dmaTiming == timing) {
                    PerformDMA(i);
                }
            }
        }
    }

    void GBAMemory::PerformDMA(int channel) {
        uint32_t baseOffset = 0xB0 + (channel * 0xC);
        
        // Read Registers directly from io_regs vector
        // SAD (Source Address) - 32 bit
        uint32_t sad = io_regs[baseOffset] | (io_regs[baseOffset+1] << 8) | (io_regs[baseOffset+2] << 16) | (io_regs[baseOffset+3] << 24);
        
        // DAD (Dest Address) - 32 bit
        uint32_t dad = io_regs[baseOffset+4] | (io_regs[baseOffset+5] << 8) | (io_regs[baseOffset+6] << 16) | (io_regs[baseOffset+7] << 24);
        
        // CNT_L (Count) - 16 bit
        uint32_t count = io_regs[baseOffset+8] | (io_regs[baseOffset+9] << 8);
        
        // CNT_H (Control) - 16 bit
        uint16_t control = io_regs[baseOffset+10] | (io_regs[baseOffset+11] << 8);
        
        // Decode Control
        bool is32Bit = (control >> 10) & 1;
        int timing = (control >> 12) & 3;
        int destCtrl = (control >> 5) & 3; // 0=Inc, 1=Dec, 2=Fixed, 3=Inc/Reload
        int srcCtrl = (control >> 7) & 3; // 0=Inc, 1=Dec, 2=Fixed
        bool repeat = (control >> 9) & 1;
        bool irq = (control >> 14) & 1;
        
        std::cout << "DMA" << channel << ": Src=" << std::hex << sad << " Dst=" << dad << " Cnt=" << count << " Ctrl=" << control << " Timing=" << timing << std::endl;

        if (count == 0) {
            count = (channel == 3) ? 0x10000 : 0x4000;
        }
        
        int step = is32Bit ? 4 : 2;
        
        uint32_t currentSrc = sad;
        uint32_t currentDst = dad;

        // Reset EEPROM Read Counter if writing to EEPROM
        if (dad >= 0x0D000000 && dad < 0x0E000000) {
             eepromBitCounter = 0;
             eepromState = EEPROMState::Idle;
        }
        
        for (uint32_t i = 0; i < count; ++i) {
            if (is32Bit) {
                uint32_t val = Read32(currentSrc);
                Write32(currentDst, val);
            } else {
                uint16_t val = Read16(currentSrc);
                Write16(currentDst, val);
            }
            
            // Update Source Address
            if (srcCtrl == 0) currentSrc += step;
            else if (srcCtrl == 1) currentSrc -= step;
            // Fixed (2) -> No change
            
            // Update Dest Address
            if (destCtrl == 0 || destCtrl == 3) currentDst += step;
            else if (destCtrl == 1) currentDst -= step;
            // Fixed (2) -> No change
        }
        
        // Update Registers (SAD/DAD) if not Reload/Repeat?
        // Actually, internal registers update, but IO regs might not reflect it unless we write back.
        // For now, we don't write back SAD/DAD to IO regs as they are usually read-only or write-only.
        
        // Trigger IRQ
        if (irq) {
            uint16_t if_reg = io_regs[0x202] | (io_regs[0x203] << 8);
            if_reg |= (1 << (8 + channel)); // DMA0=Bit8, DMA1=Bit9, DMA2=Bit10, DMA3=Bit11
            io_regs[0x202] = if_reg & 0xFF;
            io_regs[0x203] = (if_reg >> 8) & 0xFF;
            // std::cout << "DMA" << channel << " IRQ Triggered" << std::endl;
        }

        if (!repeat) {
            // Clear Enable Bit
            io_regs[baseOffset+11] &= 0x7F; // Clear Bit 15 of CNT_H (High byte)
        } else {
            // If Repeat, we reload DAD if DestCtrl is 3 (Inc/Reload)
            if (destCtrl == 3) {
                // Reload DAD from IO regs (it wasn't modified in IO regs)
                // So next time it uses the original DAD.
            }
        }
    }

    void GBAMemory::UpdateTimers(int cycles) {
        if (eepromWriteDelay > 0) {
            eepromWriteDelay -= cycles;
            if (eepromWriteDelay < 0) eepromWriteDelay = 0;
        }

        bool previousOverflow = false;
        
        for (int i = 0; i < 4; ++i) {
            uint32_t baseOffset = 0x100 + (i * 4);
            uint16_t control = io_regs[baseOffset + 2] | (io_regs[baseOffset + 3] << 8);
            
            if (control & 0x80) { // Timer Enabled
                bool overflow = false;
                int increments = 0;
                
                if (control & 0x4) { // Count-Up (Cascade)
                    if (previousOverflow) {
                        increments = 1;
                    }
                } else {
                    // Prescaler
                    int prescaler = control & 3;
                    int threshold = 0;
                    switch (prescaler) {
                        case 0: threshold = 1; break;
                        case 1: threshold = 64; break;
                        case 2: threshold = 256; break;
                        case 3: threshold = 1024; break;
                    }
                    
                    timerPrescalerCounters[i] += cycles;
                    while (timerPrescalerCounters[i] >= threshold) {
                        timerPrescalerCounters[i] -= threshold;
                        increments++;
                    }
                }
                
                if (increments > 0) {
                    uint32_t newVal = timerCounters[i] + increments;
                    if (newVal > 0xFFFF) {
                        overflow = true;
                        // Reload
                        uint16_t reload = io_regs[baseOffset] | (io_regs[baseOffset + 1] << 8);
                        timerCounters[i] = reload;
                        
                        // IRQ
                        if (control & 0x40) {
                            uint16_t if_reg = io_regs[0x202] | (io_regs[0x203] << 8);
                            if_reg |= (1 << (3 + i)); // Timer 0 = Bit 3
                            io_regs[0x202] = if_reg & 0xFF;
                            io_regs[0x203] = (if_reg >> 8) & 0xFF;
                            std::cout << "Timer " << i << " IRQ Requested" << std::endl;
                        }
                    } else {
                        timerCounters[i] = newVal;
                    }
                }
                previousOverflow = overflow;
            } else {
                previousOverflow = false;
            }
        }
    }

    uint16_t GBAMemory::ReadEEPROM() {
        // Default to Ready (1)
        uint16_t ret = 1; 
        
        // If Busy, return 0
        if (eepromWriteDelay > 0) {
            return 0;
        }

        if (eepromState == EEPROMState::ReadDummy) {
            // The GBA might read the dummy bit (should be 0)
            ret = 0;
            eepromBitCounter++;
            // 1 dummy bit (Standard for 4Kbit and 64Kbit)
            if (eepromBitCounter >= 1) {
                eepromState = EEPROMState::ReadData;
                eepromBitCounter = 0;
            }
            std::cout << "EEPROM Read Dummy: 0" << std::endl;
        } else if (eepromState == EEPROMState::ReadData) {
            // eepromBuffer holds the 64-bit data to read
            // We read MSB first
            int bitIndex = 63 - eepromBitCounter;
            ret = (eepromBuffer >> bitIndex) & 1;
            
            std::cout << "EEPROM Read Data Bit " << eepromBitCounter << ": " << ret << std::endl;

            eepromBitCounter++;
            if (eepromBitCounter >= 64) {
                eepromState = EEPROMState::Idle;
                eepromBitCounter = 0;
                std::cout << "EEPROM Read Complete" << std::endl;
            }
        }
        
        return ret;
    }

    void GBAMemory::WriteEEPROM(uint16_t value) {
        // Ignore writes while busy (programming)
        if (eepromWriteDelay > 0) {
            std::cout << "EEPROM Write Ignored (Busy)" << std::endl;
            return;
        }

        uint8_t bit = value & 1;
        // std::cout << "EEPROM Write Bit: " << (int)bit << " State=" << (int)eepromState << std::endl;
        
        switch (eepromState) {
            case EEPROMState::Idle:
                if (bit == 1) {
                    eepromState = EEPROMState::ReadCommand; // Waiting for second bit of command
                    std::cout << "EEPROM Start Bit Received" << std::endl;
                }
                break;
                
            case EEPROMState::ReadCommand: // We received '1', waiting for next
                if (bit == 1) {
                    eepromState = EEPROMState::ReadAddress;
                    eepromBitCounter = 0;
                    eepromAddress = 0;
                    std::cout << "EEPROM Command: Read" << std::endl;
                } else {
                    eepromState = EEPROMState::WriteAddress; // Command 10
                    eepromBitCounter = 0;
                    eepromAddress = 0;
                    std::cout << "EEPROM Command: Write" << std::endl;
                }
                break;
                
            case EEPROMState::ReadAddress:
                // 6-bit addressing (4Kbit) for SMA2
                // TODO: Support 14-bit (64Kbit) based on game or detection
                eepromAddress = (eepromAddress << 1) | bit;
                eepromBitCounter++;
                
                if (eepromBitCounter >= 6) {
                    // Mask Address for 4Kbit (6 bits)
                    eepromAddress &= 0x3F;

                    // Prepare Read
                    // Address is in 8-byte blocks (64 bits)
                    uint32_t offset = eepromAddress * 8;
                    eepromBuffer = 0;
                    if (offset + 7 < eepromData.size()) {
                        for (int i = 0; i < 8; ++i) {
                            eepromBuffer |= ((uint64_t)eepromData[offset + i] << (56 - i * 8));
                        }
                    }
                    
                    // 1 dummy bit
                    eepromState = EEPROMState::ReadDummy;
                    eepromBitCounter = 0;
                }
                break;
                
            case EEPROMState::WriteAddress:
                // 6 bits address
                eepromAddress = (eepromAddress << 1) | bit;
                eepromBitCounter++;
                if (eepromBitCounter >= 6) {
                    // Mask Address for 4Kbit (6 bits)
                    eepromAddress &= 0x3F;

                    eepromState = EEPROMState::WriteData;
                    eepromBitCounter = 0;
                    eepromBuffer = 0;
                }
                break;
                
            case EEPROMState::WriteData:
                // 64 bits data
                eepromBuffer = (eepromBuffer << 1) | bit;
                eepromBitCounter++;
                if (eepromBitCounter >= 64) {
                    eepromState = EEPROMState::WriteTermination;
                    eepromBitCounter = 0;
                    
                    // Perform Write
                    uint32_t offset = eepromAddress * 8;
                    if (offset + 7 < eepromData.size()) {
                        for (int i = 0; i < 8; ++i) {
                            eepromData[offset + i] = (eepromBuffer >> (56 - i * 8)) & 0xFF;
                        }
                    }
                }
                break;
                
            case EEPROMState::WriteTermination:
                // Expecting a termination bit (usually 0)
                eepromState = EEPROMState::Idle;
                // Set Write Delay (approx 10ms = 160,000 cycles)
                // Reduced to 500 to satisfy "Wait for Busy" but be fast enough
                eepromWriteDelay = 500;
                // std::cout << "EEPROM: Write Complete. Busy for 10000 cycles." << std::endl;
                break;
                
            case EEPROMState::ReadDummy:
            case EEPROMState::ReadData:
                // If we receive a Start Bit (1) during read phase, restart command
                if (bit == 1) {
                    // std::cout << "EEPROM: Restarting Command from Read Phase" << std::endl;
                    eepromState = EEPROMState::ReadCommand;
                }
                break;
                
            default:
                eepromState = EEPROMState::Idle;
                break;
        }
    }



}
