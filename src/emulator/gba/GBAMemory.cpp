#include "emulator/gba/GBAMemory.h"
#include "emulator/gba/ARM7TDMI.h"
#include "emulator/gba/APU.h"
#include "emulator/gba/PPU.h"
#include "emulator/gba/GameDB.h"
#include <cstring>
#include <iostream>
#include <iomanip>
#include <fstream>

namespace AIO::Emulator::GBA {

    GBAMemory::GBAMemory() {
        // Initialize memory vectors with correct sizes
        bios.resize(16 * 1024, 0);
        wram_board.resize(256 * 1024);
        wram_chip.resize(32 * 1024);
        io_regs.resize(0x400);
        palette_ram.resize(1 * 1024);
        vram.resize(96 * 1024);
        oam.resize(1 * 1024);
        // ROM and SRAM sizes depend on the loaded game, but we can set defaults
        rom.resize(32 * 1024 * 1024); // Max 32MB
        sram.resize(64 * 1024);
        // Initialize to 0xFF (Erased State)
        eepromData.resize(8192, 0xFF); 
        eepromIs64Kbit = true; // SMA2 uses 64Kbit EEPROM
        saveTypeLocked = false;
        
        // Initialize HLE BIOS
        // The BIOS is High-Level Emulated - we don't need the actual copyrighted BIOS
        // We just need to provide expected values at key addresses
        InitializeHLEBIOS();
    }

    GBAMemory::~GBAMemory() = default;

    void GBAMemory::InitializeHLEBIOS() {
        // High-Level Emulated BIOS initialization
        // This provides the minimum BIOS content for games to boot
        // Works for all regions (no region locking needed)
        
        // Fill with NOP instructions (0xE320F000 = ARM NOP)
        for (size_t i = 0; i < bios.size(); i += 4) {
            bios[i] = 0x00;
            bios[i+1] = 0xF0;
            bios[i+2] = 0x20;
            bios[i+3] = 0xE3;
        }
        
        // BIOS entry points (required by games)
        // SWI table at 0x00-0x7F (32 SWI calls)
        for (int swi = 0; swi < 32; ++swi) {
            uint32_t addr = swi * 4;
            // Each entry: branch to actual handler
            // B instruction: 0xEA000000 | ((offset >> 2) & 0xFFFFFF)
            // For HLE, we just put NOPs since CPU will intercept SWI
            bios[addr] = 0x00;
            bios[addr+1] = 0xF0;
            bios[addr+2] = 0x20;
            bios[addr+3] = 0xE3;
        }
        
        // BIOS IRQ handler at 0x18 (reset vector + 0x18)
        // Real BIOS jumps to address in 0x03007FFC
        // We'll put a trampoline at 0x180 to do this safely
        // At 0x18: B 0x180
        // 0x180 - 0x18 - 8 = 0x160
        // 0x160 >> 2 = 0x58
        // Opcode: EA 00 00 58
        bios[0x18] = 0x58;
        bios[0x19] = 0x00;
        bios[0x1A] = 0x00;
        bios[0x1B] = 0xEA;

        // IRQ Trampoline at 0x180
        // NOTE: The CPU core (CheckInterrupts) already:
        //   1. Calculated triggered = IE & IF at IRQ entry
        //   2. Updated BIOS_IF at 0x03007FF8
        //   3. Saved triggered bits to 0x03007FF4
        // This is necessary because PPU/Timer updates between IRQ entry
        // and BIOS execution can change IF, breaking the interrupt logic.
        //
        // This trampoline now reads the pre-saved triggered bits instead
        // of recalculating IE & IF, matching real GBA atomic behavior.
        
        uint32_t base = 0x180;
        const uint32_t trampoline[] = {
            // 0x180: Save context on IRQ stack
            0xE92D500F, // STMDB SP!, {R0-R3, R12, LR}
            
            // 0x184: Load pre-saved triggered bits from 0x03007FF4
            // PC+8 = 0x18c, pool at 0x1dc, offset = 0x1dc - 0x18c = 0x50 = 80
            0xE59F3050, // LDR R3, [PC, #80] -> Load 0x03007FF4 address from pool
            // 0x188:
            0xE1D310B0, // LDRH R1, [R3] (triggered bits saved by CPU)
            
            // BIOS_IF already updated by CPU, so skip those steps
            // (0x18c - 0x1a4 are now NOPs)
            0xE1A00000, // 0x18c: NOP
            0xE1A00000, // 0x190: NOP
            0xE1A00000, // 0x194: NOP
            0xE1A00000, // 0x198: NOP
            0xE1A00000, // 0x19c: NOP
            0xE1A00000, // 0x1a0: NOP
            0xE1A00000, // 0x1a4: NOP
            
            // 0x1a8: Switch to System mode with IRQs DISABLED (0x9F)
            0xE321F09F, // MSR CPSR_c, #0x9F
            
            // 0x1ac: Push LR to System stack
            0xE92D4000, // STMDB SP!, {LR}
            
            // 0x1b0: Load user handler
            // PC+8 = 0x1b8, pool at 0x1d8, offset = 0x1d8 - 0x1b8 = 0x20 = 32
            0xE59F3020, // LDR R3, [PC, #32] -> Load 0x03007FF8 address
            // 0x1b4:
            0xE5933004, // LDR R3, [R3, #4] (User Handler at 0x03007FFC)
            // 0x1b8: Set LR for return (ADR LR, PC+8 = 0x1c0)
            0xE28FE000, // ADD LR, PC, #0
            // 0x1bc: Jump to user handler
            0xE12FFF13, // BX R3
            
            // 0x1c0: User handler returned. Pop LR from System stack
            0xE8BD4000, // LDMIA SP!, {LR}
            
            // 0x1c4: Switch back to IRQ mode with IRQs disabled (0x92)
            0xE321F092, // MSR CPSR_c, #0x92
            
            // 0x1c8: Restore context and return
            0xE8BD500F, // LDMIA SP!, {R0-R3, R12, LR}
            // 0x1cc:
            0xE25EF004, // SUBS PC, LR, #4
            
            // 0x1d0: Padding
            0xE1A00000, // NOP
            0xE1A00000, // 0x1d4: NOP
            
            // 0x1d8: Pool
            0x03007FF8, // BIOS_IF Address (0x03007FFC = User Handler = +4)
            0x03007FF4  // Pre-saved triggered bits address (0x1dc)
        };

        for (size_t i = 0; i < sizeof(trampoline)/sizeof(uint32_t); ++i) {
            uint32_t instr = trampoline[i];
            bios[base + i*4 + 0] = instr & 0xFF;
            bios[base + i*4 + 1] = (instr >> 8) & 0xFF;
            bios[base + i*4 + 2] = (instr >> 16) & 0xFF;
            bios[base + i*4 + 3] = (instr >> 24) & 0xFF;
        }

        // VBlankIntrWait Trampoline at 0x200
        // MOV R0, #1
        // MOV R1, #1
        // SWI 0x04
        // BX LR
        base = 0x200;
        // MOV R0, #1
        bios[base+0] = 0x01; bios[base+1] = 0x00; bios[base+2] = 0xA0; bios[base+3] = 0xE3;
        // MOV R1, #1
        bios[base+4] = 0x01; bios[base+5] = 0x10; bios[base+6] = 0xA0; bios[base+7] = 0xE3;
        // SWI 0x04
        bios[base+8] = 0x04; bios[base+9] = 0x00; bios[base+10] = 0x00; bios[base+11] = 0xEF;
        // BX LR
        bios[base+12] = 0x1E; bios[base+13] = 0xFF; bios[base+14] = 0x2F; bios[base+15] = 0xE1;
        
        // Dummy IRQ Handler at 0x3FF0 (BX LR)
        // Used as default if game hasn't set one
        if (bios.size() > 0x3FF4) {
            bios[0x3FF0] = 0x1E;
            bios[0x3FF1] = 0xFF;
            bios[0x3FF2] = 0x2F;
            bios[0x3FF3] = 0xE1;
        }
    }

    void GBAMemory::Reset() {
        // Initialize WRAM to match real GBA hardware state after BIOS boot
        // Real hardware: BIOS does NOT clear all WRAM, leaving undefined values
        // Testing shows simple incremental pattern matches observed behavior
        
        // EWRAM: Initialize to 0 (safer for audio buffers that may be read before written)
        std::fill(wram_board.begin(), wram_board.end(), 0);
        
        // IWRAM: Initialize to 0 (safer for audio buffers that may be read before written)
        // Games like SMA2 enable audio DMA before filling buffers, so garbage here = noise
        std::fill(wram_chip.begin(), wram_chip.end(), 0);
        
        // Initialize User Interrupt Handler to Dummy Handler in BIOS (0x00003FF0)
        // This prevents crashes if game enables IRQs before setting handler
        if (wram_chip.size() >= 0x8000) {
            // 0x03007FFC is at offset 0x7FFC in wram_chip
            wram_chip[0x7FFC] = 0xF0;
            wram_chip[0x7FFD] = 0x3F;
            wram_chip[0x7FFE] = 0x00;
            wram_chip[0x7FFF] = 0x00;
        }
        
        // WORKAROUND: DKC audio engine expects jump table at 0x3001500
        // NOTE: Jump table is initialized AFTER DMA#1 clears IWRAM, not here
        // See ExecuteDMA for the actual initialization
        
        std::fill(io_regs.begin(), io_regs.end(), 0);
        std::fill(palette_ram.begin(), palette_ram.end(), 0);
        std::fill(vram.begin(), vram.end(), 0);
        std::fill(oam.begin(), oam.end(), 0);
        
        // Initialize KEYINPUT to 0x03FF (All Released)
        if (io_regs.size() > 0x131) {
            io_regs[0x130] = 0xFF;
            io_regs[0x131] = 0x03;
        }
        
        // Initialize SOUNDCNT_X (0x84) with Master Enable set
        // The BIOS enables sound on boot, so bit 7 should be set
        if (io_regs.size() > 0x85) {
            io_regs[0x84] = 0x80; // Master Enable = 1
        }
        
        // Initialize SOUNDBIAS (0x88) to proper default value
        // Real hardware has SOUNDBIAS = 0x200 after boot (PWM mode, bias = 0x200)
        if (io_regs.size() > 0x89) {
            io_regs[0x88] = 0x00;
            io_regs[0x89] = 0x02; // 0x200 = default bias
        }


        eepromState = EEPROMState::Idle;
        eepromBitCounter = 0;
        eepromBuffer = 0;
        eepromAddress = 0;
        eepromLatch = 0; // Initialize latch
        eepromWriteDelay = 0; // Reset write delay
        // saveTypeLocked = false; // Do NOT reset this, as it's set by LoadGamePak

        // Debug: Check EEPROM content
        if (eepromData.empty()) {
            eepromData.resize(8192, 0xFF);
        }
    }

    bool GBAMemory::Is4KbitEEPROM(const std::vector<uint8_t>& data) {
        // Deprecated: We now use DMA transfer length detection for accurate sizing.
        // Keeping this stub if we want to add generic header checks later.
        return false;
    }

    bool GBAMemory::ScanForEEPROMSize(const std::vector<uint8_t>& data) {
        // Preprocess the ROM code to determine EEPROM size (4Kbit vs 64Kbit)
        // We look for the DMA3CNT_L register address (0x040000DC) being loaded,
        // and then check for the transfer count (9 or 17) being set nearby.
        
        
        int score4k = 0;
        int score64k = 0;
        
        // Search for the literal 0x040000DC (DMA3CNT_L)
        const uint8_t targetBytes[] = {0xDC, 0x00, 0x00, 0x04};
        // Also search for the base 0x04000000
        const uint8_t baseBytes[] = {0x00, 0x00, 0x00, 0x04};
        
        for (size_t i = 0; i < data.size() - 4; i += 4) {
            bool foundLiteral = (data[i] == targetBytes[0] && data[i+1] == targetBytes[1] && 
                                 data[i+2] == targetBytes[2] && data[i+3] == targetBytes[3]);
            bool foundBase = (data[i] == baseBytes[0] && data[i+1] == baseBytes[1] && 
                              data[i+2] == baseBytes[2] && data[i+3] == baseBytes[3]);

            if (foundLiteral || foundBase) {
                // Scan a window of code before the literal
                size_t searchStart = (i > 1024) ? i - 1024 : 0;
                size_t searchEnd = i + 128; // Also look slightly after
                if (searchEnd > data.size()) searchEnd = data.size();
                
                // THUMB SCAN
                for (size_t pc = searchStart; pc < searchEnd; pc += 2) {
                    uint16_t instr = data[pc] | (data[pc+1] << 8);
                    
                    // LDR Rn, [PC, #imm] -> 0100 1xxx iiiiiiii (4800 - 4FFF)
                    if ((instr & 0xF800) == 0x4800) {
                        int imm = (instr & 0xFF) * 4;
                        size_t targetAddr = (pc & ~2) + 4 + imm;
                        
                        if (targetAddr == i) {
                            // Found an instruction loading the address!
                            // Now look nearby for MOV Rn, #9 or MOV Rn, #17
                            
                            // Scan small window around this instruction
                            size_t contextStart = (pc > 64) ? pc - 64 : 0;
                            size_t contextEnd = pc + 64;
                            if (contextEnd > data.size()) contextEnd = data.size();
                            
                            for (size_t j = contextStart; j < contextEnd; j += 2) {
                                uint16_t ctxInstr = data[j] | (data[j+1] << 8);
                                
                                // MOV Rn, #9 (0x2n09)
                                if ((ctxInstr & 0xF8FF) == 0x2009) score4k++;
                                // MOV Rn, #17 (0x2n11)
                                if ((ctxInstr & 0xF8FF) == 0x2011) score64k++;
                                
                                // Also check for LDR Rn, [PC, #imm] loading 9 or 17
                                if ((ctxInstr & 0xF800) == 0x4800) {
                                    int valImm = (ctxInstr & 0xFF) * 4;
                                    size_t valTarget = (j & ~2) + 4 + valImm;
                                    if (valTarget + 4 <= data.size()) {
                                        uint32_t val = data[valTarget] | (data[valTarget+1] << 8) | 
                                                       (data[valTarget+2] << 16) | (data[valTarget+3] << 24);
                                        if (val == 9) score4k++;
                                        if (val == 17) score64k++;
                                        // Check for 32-bit DMA control + count (0x8xxx0011)
                                        if ((val & 0xFFFF) == 9 && (val & 0x80000000)) score4k += 2;
                                        if ((val & 0xFFFF) == 17 && (val & 0x80000000)) score64k += 2;
                                    }
                                }
                            }
                        }
                    }
                }
                
                // ARM SCAN
                for (size_t pc = searchStart & ~3; pc < searchEnd; pc += 4) {
                    uint32_t instr = data[pc] | (data[pc+1] << 8) | (data[pc+2] << 16) | (data[pc+3] << 24);
                    
                    // LDR Rd, [PC, #offset] (E59Fxxxx)
                    if ((instr & 0xFFFF0000) == 0xE59F0000) {
                        int offset = instr & 0xFFF;
                        size_t targetAddr = pc + 8 + offset;
                        
                        if (targetAddr == i) {
                            // Found ARM LDR loading the address
                            size_t contextStart = (pc > 128) ? pc - 128 : 0;
                            size_t contextEnd = pc + 128;
                            if (contextEnd > data.size()) contextEnd = data.size();
                            
                            for (size_t j = contextStart & ~3; j < contextEnd; j += 4) {
                                uint32_t ctxInstr = data[j] | (data[j+1] << 8) | (data[j+2] << 16) | (data[j+3] << 24);
                                
                                // MOV Rd, #9 (E3A0x009)
                                if ((ctxInstr & 0xFFF000FF) == 0xE3A00009) score4k++;
                                // MOV Rd, #17 (E3A0x011)
                                if ((ctxInstr & 0xFFF000FF) == 0xE3A00011) score64k++;
                                
                                // LDR Rd, [PC, #offset] (E59Fxxxx) loading 9 or 17
                                if ((ctxInstr & 0xFFFF0000) == 0xE59F0000) {
                                    int valOffset = ctxInstr & 0xFFF;
                                    size_t valTarget = j + 8 + valOffset;
                                    if (valTarget + 4 <= data.size()) {
                                        uint32_t val = data[valTarget] | (data[valTarget+1] << 8) | 
                                                       (data[valTarget+2] << 16) | (data[valTarget+3] << 24);
                                        if (val == 9) score4k++;
                                        if (val == 17) score64k++;
                                        // Check for 32-bit DMA control + count (0x8xxx0011)
                                        if ((val & 0xFFFF) == 9 && (val & 0x80000000)) score4k += 2;
                                        if ((val & 0xFFFF) == 17 && (val & 0x80000000)) score64k += 2;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        
        
        if (score64k > score4k) return false; // 64Kbit
        if (score4k > score64k) return true;  // 4Kbit
        
        return false; // Default to 64Kbit if inconclusive
    }

    void GBAMemory::LoadGamePak(const std::vector<uint8_t>& data) {
        if (data.size() > rom.size()) {
            rom.resize(data.size());
        }
        std::copy(data.begin(), data.end(), rom.begin());
        
        SaveType saveType = SaveType::Auto;
        bool locked = false;

        // 1. Check GameDB Override
        if (data.size() >= 0xB0) {
            std::string detectedCode(reinterpret_cast<const char*>(&data[0xAC]), 4);
            this->gameCode = detectedCode;

            GameOverride override = GameDB::GetOverride(detectedCode);
            
            // Fallback: Check 0xB6 (Non-standard header seen in some SMA2 ROMs)
            if (override.saveType == SaveType::Auto && data.size() >= 0xBA) {
                 std::string altCode(reinterpret_cast<const char*>(&data[0xB6]), 4);
                 GameOverride altOverride = GameDB::GetOverride(altCode);
                 if (altOverride.saveType != SaveType::Auto) {
                     override = altOverride;
                     detectedCode = altCode;
                     this->gameCode = altCode;
                 }
            }

            if (override.saveType != SaveType::Auto) {
                saveType = override.saveType;
                locked = true;

                // Apply Patches
                for (const auto& patch : override.patches) {
                    uint32_t addr = patch.first;
                    uint32_t val = patch.second;
                    if (addr + 4 <= rom.size()) {
                        rom[addr] = val & 0xFF;
                        rom[addr+1] = (val >> 8) & 0xFF;
                        rom[addr+2] = (val >> 16) & 0xFF;
                        rom[addr+3] = (val >> 24) & 0xFF;
                    }
                }
            }
        }

        // 2. String Detection (if not overridden)
        if (saveType == SaveType::Auto) {
            saveType = GameDB::DetectSaveType(data);
            if (saveType != SaveType::Auto) {
            }
        }

        // 3. DMA Scan (Fallback for EEPROM size)
        if (saveType == SaveType::Auto) {
             bool is4k = ScanForEEPROMSize(data);
             saveType = is4k ? SaveType::EEPROM_4K : SaveType::EEPROM_64K;
        }

        // Apply Configuration
        isFlash = false;
        hasSRAM = false;
        eepromIs64Kbit = true;
        this->saveTypeLocked = locked;

        switch (saveType) {
            case SaveType::EEPROM_4K:
                eepromIs64Kbit = false;
                eepromData.resize(512, 0xFF);
                break;
            case SaveType::EEPROM_64K:
                eepromIs64Kbit = true;
                eepromData.resize(8192, 0xFF);
                break;
            case SaveType::SRAM:
                hasSRAM = true;
                sram.resize(32 * 1024, 0xFF);
                break;
            case SaveType::Flash512:
                isFlash = true;
                hasSRAM = true;
                sram.resize(64 * 1024, 0xFF);
                break;
            case SaveType::Flash1M:
                isFlash = true;
                hasSRAM = true;
                sram.resize(128 * 1024, 0xFF);
                break;
            default:
                // Default to 64K EEPROM
                eepromIs64Kbit = true;
                eepromData.resize(8192, 0xFF);
                break;
        }
    }

    void GBAMemory::LoadSave(const std::vector<uint8_t>& data) {
        if (!data.empty()) {
            eepromData = data;
            
            // Validate size against detected type
            if (eepromIs64Kbit) {
                // FIX: Enforce exact size. If previous save was Flash (64KB), this corrects it.
                if (eepromData.size() != 8192) {
                    eepromData.resize(8192);
                    // If size was wrong, the data is likely invalid (Flash format vs EEPROM). Wipe it to 0xFF (Erased).
                    std::fill(eepromData.begin(), eepromData.end(), 0xFF);
                }
            } else {
                // 4Kbit mode
                if (eepromData.size() < 512) {
                    eepromData.resize(512, 0xFF);
                }
            }
        } else {
            // Empty save data - this shouldn't happen but ensure we're initialized
            if (eepromData.empty()) {
                eepromData.resize(eepromIs64Kbit ? 8192 : 512, 0xFF);
            }
        }

        // Check if the loaded data is all zeros (which indicates a bad previous initialization)
        // Real EEPROM is erased to 0xFF, so if we see all zeros, it's likely from a bad emulator init
        bool allZeros = true;
        for (size_t i = 0; i < std::min(eepromData.size(), (size_t)64); ++i) {
            if (eepromData[i] != 0x00) {
                allZeros = false;
                break;
            }
        }

        // If the save appears to be all zeros (bad init), reset to 0xFF (proper erased state)
        if (allZeros && !eepromData.empty()) {
            std::fill(eepromData.begin(), eepromData.end(), 0xFF);
        }

    }

    std::vector<uint8_t> GBAMemory::GetSaveData() const {
        return eepromData;
    }
    
    void GBAMemory::SetSavePath(const std::string& path) {
        savePath = path;
    }
    
    void GBAMemory::FlushSave() {
        if (savePath.empty() || eepromData.empty()) {
            return;
        }
        
        std::ofstream file(savePath, std::ios::binary);
        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(eepromData.data()), eepromData.size());
            file.close();
        }
    }

    void GBAMemory::SetKeyInput(uint16_t value) {
        if (io_regs.size() > 0x131) {
            io_regs[0x130] = value & 0xFF;
            io_regs[0x131] = (value >> 8) & 0xFF;
        }
    }

    uint8_t GBAMemory::Read8(uint32_t address) {
        switch (address >> 24) {
            case 0x00: // BIOS
                if (address < bios.size()) {
                    return bios[address];
                }
                break;
            case 0x02: // WRAM (Board)
                return wram_board[address & 0x3FFFF];
            case 0x03: // WRAM (Chip)
            {
                uint32_t offset = address & 0x7FFF;
                uint8_t val = wram_chip[offset];
                // Debug: trace when read value from 0x3003378 is part of corruption pattern
                if (offset == 0x3378 && val != 0xA4 && val != 0xFC && val != 0xFF && val != 0x00) {
                    static int corruptReadCount = 0;
                    corruptReadCount++;
                    if (corruptReadCount <= 5) {
                        std::cout << "[CORRUPT READ 0x3003378] val=0x" << std::hex << (int)val 
                                  << " expected 0xA4 (part of 0xfffffca4)" << std::dec << std::endl;
                    }
                }
                return val;
            }
            case 0x04: // IO Registers
            {
                uint32_t offset = address & 0x3FF;
                uint8_t val = 0;
                if (offset < io_regs.size()) val = io_regs[offset];
                
                // SOUNDCNT_X (0x84) - Return proper status
                if (offset == 0x84) {
                    val = io_regs[0x84] & 0x80; // Only preserve master enable bit
                }
                
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
                // VRAM is 96KB (0x18000 bytes) which is NOT a power of 2
                uint32_t rawOffset = address & 0xFFFFFF;
                uint32_t offset = rawOffset % 0x18000;
                if (offset < vram.size()) return vram[offset];
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
                 // Implement ROM Mirroring (max 32MB space, mirrored every rom.size())
                 // Standard GBA ROMs are power of 2 size usually, but we handle arbitrary
                 uint32_t offset = address & 0x01FFFFFF;
                 if (!rom.empty()) {
                     uint8_t val = rom[offset % rom.size()];
                     // Debug: trace specific ROM reads (disabled)
                     // if (offset == 0x14EC) printf("[ReadROM] 0x14EC = 0x%02X\n", val);
                     return val;
                 }
                 break;
            }
            case 0x0D: // EEPROM
            {
                return 0;
            }
            case 0x0E: // SRAM/Flash
            {
                if (!hasSRAM) {
                    // Open Bus behavior for EEPROM carts reading SRAM region
                    // The SRAM slot has no chip - reads return open bus (address-dependent garbage)
                    // This is important for anti-piracy checks that detect SRAM presence
                    uint16_t busVal = address & 0xFFFF;
                    uint8_t val = (address & 1) ? (busVal >> 8) : (busVal & 0xFF);
                    return val;
                }

                if (isFlash && flashState == 3) { // ID Mode
                    uint32_t offset = address & 0xFFFF;
                    if (offset == 0) return 0xC2; // Maker: Macronix
                    if (offset == 1) return 0x1C; // Device: 512K (TODO: Handle 1Mbit 0x09)
                    return 0;
                }
                // Normal Read
                uint32_t offset = address & 0xFFFF;
                // Handle 1Mbit banking if needed (offset > 64K)
                // But standard addressing is 64K window.
                // If sram.size() > 64K, use flashBank.
                if (sram.size() > 65536) {
                    offset += (flashBank * 65536);
                }
                if (offset < sram.size()) return sram[offset];
                return 0xFF; // Open bus / erased
            }
        }
        return 0;
    }

    uint16_t GBAMemory::Read16(uint32_t address) {
        // EEPROM Handling
        // Allow reads from 0x08-0x0D if EEPROM is active
        // Also allow reads from 0x0D specifically for Ready/Busy polling even if Idle
        uint8_t region = (address >> 24);
        // FIX: Only allow EEPROM reads from 0x0D (Wait State 2 Mirror) to prevent hijacking code/data from 0x08 (Wait State 0)
        if (region == 0x0D /* || ((region >= 0x08 && region <= 0x0C) && eepromState != EEPROMState::Idle) */) {
             return ReadEEPROM();
        }

        uint16_t val = Read8(address) | (Read8(address + 1) << 8);

        // Timer Counters (Read from internal state)
        if ((address & 0xFF000000) == 0x04000000) {
             uint32_t offset = address & 0x3FF;
             
             if (offset >= 0x100 && offset <= 0x10F) {
                 int timerIdx = (offset - 0x100) / 4;
                 if ((offset % 4) == 0) {
                     return timerCounters[timerIdx];
                 }
             }
        }

        return val;
    }

    uint16_t GBAMemory::ReadInstruction16(uint32_t address) {
        // Direct ROM access for instruction fetch - bypass EEPROM and other checks
        if ((address >> 24) >= 0x08 && (address >> 24) <= 0x0C) {
            uint32_t offset = address & 0x01FFFFFF;
            if (!rom.empty()) {
                // Handle wrapping if needed, though usually not for code
                uint8_t b0 = rom[offset % rom.size()];
                uint8_t b1 = rom[(offset + 1) % rom.size()];
                return b0 | (b1 << 8);
            }
        }
        // Fallback
        return Read8(address) | (Read8(address + 1) << 8);
    }

    uint32_t GBAMemory::Read32(uint32_t address) {
        // EEPROM Handling - 32-bit read performs two 16-bit reads
        uint8_t region = (address >> 24);
        // FIX: Only allow EEPROM reads from 0x0D (Wait State 2 Mirror) to prevent hijacking code/data from 0x08 (Wait State 0)
        if (region == 0x0D /* || ((region >= 0x08 && region <= 0x0C) && eepromState != EEPROMState::Idle) */) {
            uint16_t low = ReadEEPROM();
            uint16_t high = ReadEEPROM();
            return low | (high << 16);
        }

        uint32_t val = Read8(address) | (Read8(address + 1) << 8) | 
                       (Read8(address + 2) << 16) | (Read8(address + 3) << 24);
        
        // Debug: trace Read32 from jump table area
        if ((address >> 24) == 0x03 && (address & 0x7FFF) == 0x3378) {
            static int read32_3378_count = 0;
            read32_3378_count++;
            if (val != 0xfffffca4 && read32_3378_count > 100 && read32_3378_count <= 110) {
                std::cout << "[READ32 0x3003378 MISMATCH] val=0x" << std::hex << val 
                          << " expected 0xfffffca4 count=" << std::dec << read32_3378_count << std::endl;
            }
        }
        
        return val;
    }

    uint32_t GBAMemory::ReadInstruction32(uint32_t address) {
        // Direct ROM access for instruction fetch - bypass EEPROM and other checks
        if ((address >> 24) >= 0x08 && (address >> 24) <= 0x0C) {
            uint32_t offset = address & 0x01FFFFFF;
            if (!rom.empty()) {
                uint8_t b0 = rom[offset % rom.size()];
                uint8_t b1 = rom[(offset + 1) % rom.size()];
                uint8_t b2 = rom[(offset + 2) % rom.size()];
                uint8_t b3 = rom[(offset + 3) % rom.size()];
                return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
            }
        }
        // Fallback
        return Read8(address) | (Read8(address + 1) << 8) | 
               (Read8(address + 2) << 16) | (Read8(address + 3) << 24);
    }

    // Internal write that bypasses the GBA 8-bit write quirks for video memory
    void GBAMemory::Write8Internal(uint32_t address, uint8_t value) {
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
                if (offset < io_regs.size()) {
                    io_regs[offset] = value;
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
                uint32_t rawOffset = address & 0xFFFFFF;
                uint32_t offset = rawOffset % 0x18000;
                
                // DEBUG: Trace writes to BG character area 0 (first 16KB: 0x0000-0x3FFF)
                static int vramCharWriteCount = 0;
                if (offset < 0x4000 && value != 0) {
                    vramCharWriteCount++;
                    if (vramCharWriteCount <= 20) {
                        std::cout << "[VRAM Char0 Write] offset=0x" << std::hex << offset 
                                  << " val=0x" << (int)value;
                        if (cpu) std::cout << " PC=0x" << cpu->GetRegister(15);
                        std::cout << std::dec << std::endl;
                    }
                }

                if (offset < vram.size()) vram[offset] = value;
                break;
            }
            case 0x07: // OAM
            {
                uint32_t offset = address & 0x3FF;
                if (offset < oam.size()) oam[offset] = value;
                break;
            }
        }
    }

    void GBAMemory::Write8(uint32_t address, uint8_t value) {
        // DEBUG: Trace writes to IWRAM literal pool area 0x3003460-0x3003480 (disabled)
        // if (address >= 0x03003460 && address < 0x03003480) {
        //     std::cout << "[POOL WRITE] 0x" << std::hex << address 
        //               << " = 0x" << (int)value;
        //     if (cpu) {
        //         std::cout << " PC=0x" << cpu->GetRegister(15);
        //     }
        //     std::cout << std::dec << std::endl;
        // }
        
        // Trace ALL writes to 0x3001500 area (disabled for performance)
        // if ((address >> 24) == 0x03 && (address & 0x7FFF) >= 0x1500 && (address & 0x7FFF) < 0x1508) {
        //     std::cout << "[MIXBUF] Write8 0x" << std::hex << address 
        //               << " = 0x" << (int)value;
        //     if (cpu) {
        //         std::cout << " PC=0x" << cpu->GetRegister(15);
        //     }
        //     std::cout << std::dec << std::endl;
        // }
        
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
                
                // Handle IF (Interrupt Request) - Write 1 to Clear
                if (offset == 0x202 || offset == 0x203) {
                    if (offset < io_regs.size()) {
                        io_regs[offset] &= ~value;
                    }
                } else {
                    // Protect DISPSTAT (0x04) Read-Only bits (0-2)
                    if (offset == 0x04) {
                        uint8_t currentVal = io_regs[offset];
                        uint8_t readOnlyMask = 0x07;
                        value = (value & ~readOnlyMask) | (currentVal & readOnlyMask);
                    }

                    // Handle DMA Enable
                    bool dmaLatchNeeded = false;
                    int dmaChannel = -1;
                    if (offset == 0xBB || offset == 0xC7 || offset == 0xD3 || offset == 0xDF) {
                        if (offset == 0xBB) dmaChannel = 0;
                        else if (offset == 0xC7) dmaChannel = 1;
                        else if (offset == 0xD3) dmaChannel = 2;
                        else if (offset == 0xDF) dmaChannel = 3;
                        
                        bool wasEnabled = (io_regs[offset] & 0x80) != 0;
                        bool willBeEnabled = (value & 0x80) != 0;
                        
                        if (!wasEnabled && willBeEnabled) {
                            dmaLatchNeeded = true;
                        }
                    }

                    if (offset < io_regs.size()) {
                        io_regs[offset] = value;
                    }
                    
                    // Handle DMA latch after io_regs is updated
                    if (dmaLatchNeeded && dmaChannel >= 0) {
                        uint32_t dmaBase = 0xB0 + (dmaChannel * 0xC);
                        dmaInternalSrc[dmaChannel] = io_regs[dmaBase] | (io_regs[dmaBase+1] << 8) | 
                                                  (io_regs[dmaBase+2] << 16) | (io_regs[dmaBase+3] << 24);
                        dmaInternalDst[dmaChannel] = io_regs[dmaBase+4] | (io_regs[dmaBase+5] << 8) | 
                                                  (io_regs[dmaBase+6] << 16) | (io_regs[dmaBase+7] << 24);
                        
                        // DEBUG: Trace DMA setup when dst is around 0x3001500
                        {
                            uint16_t ctrl = io_regs[dmaBase+10] | (io_regs[dmaBase+11] << 8);
                            uint16_t cnt = io_regs[dmaBase+8] | (io_regs[dmaBase+9] << 8);
                            uint32_t dst = dmaInternalDst[dmaChannel];
                            uint32_t src = dmaInternalSrc[dmaChannel];
                            if (dst == 0x3001500 || src == 0x3001500) {
                                // Also show the raw DAD register bytes
                                std::cout << "[DMA" << dmaChannel << " SETUP] "
                                          << "Src=0x" << std::hex << src
                                          << " Dst=0x" << dst
                                          << " (raw DAD: ";
                                for (int i = 4; i < 8; i++) {
                                    std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)io_regs[dmaBase+i];
                                    if (i < 7) std::cout << " ";
                                }
                                std::cout << ")"
                                          << " Cnt=0x" << cnt
                                          << " Ctrl=0x" << ctrl 
                                          << " (raw CNT_H: " << std::hex << std::setw(2) << std::setfill('0') << (int)io_regs[dmaBase+10]
                                          << " " << std::setw(2) << std::setfill('0') << (int)io_regs[dmaBase+11] << ")"
                                          << " DstCtrl=" << std::dec << ((ctrl >> 5) & 3) 
                                          << " SrcCtrl=" << ((ctrl >> 7) & 3)
                                          << " PC=0x" << std::hex;
                                if (cpu) std::cout << cpu->GetRegister(15);
                                std::cout << std::dec << std::endl;
                            }
                        }
                                  
                        uint16_t control = io_regs[dmaBase+10] | (io_regs[dmaBase+11] << 8);
                        int timing = (control >> 12) & 3;
                        if (timing == 0) PerformDMA(dmaChannel);
                    }
                }
                break;
            }
            case 0x05: // Palette RAM - 8-bit writes duplicate byte
            {
                uint32_t offset = address & 0x3FF;
                uint32_t alignedOffset = offset & ~1;
                if (alignedOffset + 1 < palette_ram.size()) {
                    palette_ram[alignedOffset] = value;
                    palette_ram[alignedOffset + 1] = value;
                }
                break;
            }
            case 0x06: // VRAM - 8-bit writes to BG VRAM duplicate, OBJ VRAM ignored
            {
                uint32_t rawOffset = address & 0xFFFFFF;
                uint32_t offset = rawOffset % 0x18000;
                
                if (offset >= 0x10000) {
                    break; // OBJ VRAM: 8-bit writes ignored
                }
                
                uint32_t alignedOffset = offset & ~1;
                if (alignedOffset + 1 < vram.size()) {
                    vram[alignedOffset] = value;
                    vram[alignedOffset + 1] = value;
                }
                break;
            }
            case 0x07: // OAM - 8-bit writes ignored
                break;
            case 0x0E: // SRAM/Flash
            {
                if (!hasSRAM) return; // Ignore writes if no SRAM/Flash present

                uint32_t offset = address & 0xFFFF;
                
                if (!isFlash) {
                    // SRAM Write
                    if (offset < sram.size()) {
                        sram[offset] = value;
                        FlushSave(); // Auto-flush for safety
                    }
                    return;
                }

                // Flash Command State Machine
                // Handle Reset (0xF0) at any time
                if (value == 0xF0) {
                    flashState = 0;
                    return;
                }

                switch (flashState) {
                    case 0: // Idle
                        if (offset == 0x5555 && value == 0xAA) flashState = 1;
                        break;
                    case 1: // Seen 0xAA
                        if (offset == 0x2AAA && value == 0x55) flashState = 2;
                        else flashState = 0; // Reset on error
                        break;
                    case 2: // Seen 0x55, Expecting Command
                        if (offset == 0x5555) {
                            if (value == 0x90) flashState = 3; // ID Mode
                            else if (value == 0x80) flashState = 5; // Erase Setup
                            else if (value == 0xA0) flashState = 8; // Program
                            else if (value == 0xB0) flashState = 9; // Bank Switch
                            else flashState = 0;
                        } else {
                            flashState = 0;
                        }
                        break;
                    case 3: // ID Mode
                        // Writes in ID mode might reset? 0xF0 handled above.
                        break;
                    case 5: // Erase Setup (Seen 0x80), Expecting 0xAA
                        if (offset == 0x5555 && value == 0xAA) flashState = 6;
                        else flashState = 0;
                        break;
                    case 6: // Erase Cmd 1 (Seen 0xAA), Expecting 0x55
                        if (offset == 0x2AAA && value == 0x55) flashState = 7;
                        else flashState = 0;
                        break;
                    case 7: // Erase Cmd 2 (Seen 0x55), Expecting Action
                        if (offset == 0x5555 && value == 0x10) {
                            // Chip Erase
                            std::fill(sram.begin(), sram.end(), 0xFF);
                            FlushSave();
                            flashState = 0;
                        } else if (value == 0x30) {
                            // Sector Erase (4KB)
                            // Address determines sector
                            uint32_t sectorBase = offset & 0xF000;
                            if (sram.size() > 65536) sectorBase += (flashBank * 65536);
                            
                            if (sectorBase < sram.size()) {
                                size_t end = std::min((size_t)sectorBase + 4096, sram.size());
                                std::fill(sram.begin() + sectorBase, sram.begin() + end, 0xFF);
                                FlushSave();
                            }
                            flashState = 0;
                        } else {
                            flashState = 0;
                        }
                        break;
                    case 8: // Program Byte
                    {
                        uint32_t target = offset;
                        if (sram.size() > 65536) target += (flashBank * 65536);
                        
                        if (target < sram.size()) {
                            sram[target] = value;
                            FlushSave();
                        }
                        flashState = 0;
                        break;
                    }
                    case 9: // Bank Switch
                        if (offset == 0) {
                            flashBank = value & 1;
                        }
                        flashState = 0;
                        break;
                }
                break;
            }
            case 0x0D: // EEPROM - ignore 8-bit writes
                break;
        }
    }

    void GBAMemory::Write16(uint32_t address, uint16_t value) {
        // EEPROM Handling - Restrict writes to 0x0D (Wait State 2) to avoid conflicts with ROM
        // Most games use 0x0D for EEPROM. Some might use 0x0C.
        uint8_t region = (address >> 24);
        if (region == 0x0D) {
            WriteEEPROM(value);
            return;
        }

        if ((address & 0xFF000000) == 0x04000000) {
            uint32_t offset = address & 0x3FF;
            
            // DISPSTAT Write Masking - preserve read-only bits
            if (offset == 0x04) {
                uint16_t currentVal = io_regs[offset] | (io_regs[offset+1] << 8);
                uint16_t readOnlyMask = 0x0007;
                value = (value & ~readOnlyMask) | (currentVal & readOnlyMask);
            }
            
            // SOUNDCNT_H (0x82) - DMA Sound Control
            if (offset == 0x82) {
                // Handle FIFO reset bits
                if (apu) {
                    if (value & 0x0800) apu->ResetFIFO_A();
                    if (value & 0x8000) apu->ResetFIFO_B();
                }
                value &= ~0x8800; // Clear reset bits
            }
            
            // SOUNDCNT_X (0x84) - preserve status bits
            if (offset == 0x84) {
                uint16_t currentVal = io_regs[offset] | (io_regs[offset+1] << 8);
                value = (value & 0x80) | (currentVal & 0x0F);
            }

            // Timer Control
            if (offset >= 0x100 && offset <= 0x10F) {
                int timerIdx = (offset - 0x100) / 4;
                
                if ((offset % 4) == 2) { // TMxCNT_H
                    uint16_t oldControl = io_regs[offset] | (io_regs[offset+1] << 8);
                    bool wasEnabled = oldControl & 0x80;
                    bool nowEnabled = value & 0x80;

                    if (!wasEnabled && nowEnabled) {
                        uint16_t reload = io_regs[offset-2] | (io_regs[offset-1] << 8);
                        timerCounters[timerIdx] = reload;
                        timerPrescalerCounters[timerIdx] = 0;
                    }
                }
            }
        }

        // For video memory, bypass 8-bit quirks
        // uint8_t region = (address >> 24) & 0xFF; // Already defined above
        if (region == 0x05 || region == 0x06 || region == 0x07) {
            Write8Internal(address, value & 0xFF);
            Write8Internal(address + 1, (value >> 8) & 0xFF);
        } else {
            Write8(address, value & 0xFF);
            Write8(address + 1, (value >> 8) & 0xFF);
        }
    }

    void GBAMemory::Write32(uint32_t address, uint32_t value) {
        // DEBUG: Trace writes to DMA#1 source area (0x3007ef0-0x3007fff)
        if ((address >> 24) == 0x03) {
            uint32_t offset = address & 0x7FFF;
            if (offset >= 0x7e00 && offset <= 0x7fff) {
                static int sourceAreaWriteCount = 0;
                sourceAreaWriteCount++;
                if (sourceAreaWriteCount <= 50) {
                    std::cout << "[DMA SRC AREA WRITE #" << sourceAreaWriteCount << "] addr=0x" << std::hex << address
                              << " val=0x" << value;
                    if (cpu) std::cout << " PC=0x" << cpu->GetRegister(15);
                    std::cout << std::dec << std::endl;
                }
            }
        }
        
        // Trace writes to 0x3001500 (jump table area being overwritten by audio)
        static bool inPerformDMA = false;  // Declared in PerformDMA
        if ((address >> 24) == 0x03 && (address & 0x7FFF) == 0x1500) {
            static int write1500Count = 0;
            write1500Count++;
            std::cout << "[WRITE 0x3001500 #" << write1500Count << "] = 0x" << std::hex << value;
            if (cpu) {
                std::cout << " PC=0x" << cpu->GetRegister(15);
                std::cout << " LR=0x" << cpu->GetRegister(14);
            }
            // Check if this is from a CPU instruction (non-DMA)
            std::cout << " (call stack unknown)";
            std::cout << std::dec << std::endl;
        }
        
        // Trace writes to 0x3001420 (polled address)
        // DKC audio engine writes 0xFFFFFFFF here then polls waiting for audio init to complete
        // Only intercept writes from ROM code (0x08xxxxxx), not from audio engine in IWRAM (0x03xxxxxx)
        if ((address >> 24) == 0x03 && (address & 0x7FFF) == 0x1420 && gameCode == "A5NE") {
            if (value == 0xFFFFFFFF && cpu) {
                uint32_t pc = cpu->GetRegister(15);
                // Only intercept if write is from ROM (game init), not from audio engine
                if ((pc >> 24) == 0x08) {
                    std::cout << "[WRITE 0x3001420] Intercepted init flag from ROM, setting to 0 (instant init)";
                    std::cout << " PC=0x" << std::hex << pc;
                    std::cout << std::dec << std::endl;
                    value = 0;  // Pretend init completed instantly
                }
            }
        }
        
        // Trace writes to 0x3003378 (jump table that gets corrupted)
        if ((address >> 24) == 0x03 && (address & 0x7FFF) >= 0x3370 && (address & 0x7FFF) <= 0x33a0) {
            static int write3378Count = 0;
            write3378Count++;
            if (write3378Count <= 50 || (write3378Count > 20 && value != 0 && (value >> 28) != 0xF)) {
                std::cout << "[WRITE 0x3003378 area #" << write3378Count << "] addr=0x" 
                          << std::hex << address << " val=0x" << value;
                if (cpu) {
                    std::cout << " PC=0x" << cpu->GetRegister(15);
                }
                std::cout << std::dec << std::endl;
            }
        }
        
        // EEPROM Handling - 32-bit write performs two 16-bit writes
        // Restrict writes to 0x0D
        uint8_t region = (address >> 24);
        if (region == 0x0D) {
            WriteEEPROM(value & 0xFFFF);
            WriteEEPROM(value >> 16);
            return;
        }

        // Sound FIFO writes (FIFO_A = 0x40000A0, FIFO_B = 0x40000A4)
        if (address == 0x040000A0) {
            static int fifoACount = 0;
            fifoACount++;
            if (fifoACount <= 20 || (fifoACount % 10000 == 0)) {
                std::cout << "[FIFO_A Write #" << fifoACount << "] val=0x" << std::hex << value;
                if (cpu) std::cout << " PC=0x" << cpu->GetRegister(15) << " LR=0x" << cpu->GetRegister(14);
                std::cout << std::dec << std::endl;
            }
            if (apu) apu->WriteFIFO_A(value);
            return;
        }
        if (address == 0x040000A4) {
            static int fifoBCount = 0;
            fifoBCount++;
            if (fifoBCount <= 10 || (fifoBCount % 10000 == 0)) {
                std::cout << "[FIFO_B Write #" << fifoBCount << "] val=0x" << std::hex << value;
                if (cpu) std::cout << " PC=0x" << cpu->GetRegister(15);
                std::cout << std::dec << std::endl;
            }
            if (apu) apu->WriteFIFO_B(value);
            return;
        }

        // For VRAM, Palette, OAM - write directly without 8-bit quirk
        // uint8_t region = (address >> 24) & 0xFF; // Already defined above
        if (region == 0x05 || region == 0x06 || region == 0x07) {
            Write8Internal(address, value & 0xFF);
            Write8Internal(address + 1, (value >> 8) & 0xFF);
            Write8Internal(address + 2, (value >> 16) & 0xFF);
            Write8Internal(address + 3, (value >> 24) & 0xFF);
        } else {
            Write8(address, value & 0xFF);
            Write8(address + 1, (value >> 8) & 0xFF);
            Write8(address + 2, (value >> 16) & 0xFF);
            Write8(address + 3, (value >> 24) & 0xFF);
        }

        // Timer control via Write32
        if ((address & 0xFF000000) == 0x04000000) {
            uint32_t offset = address & 0x3FF;
            
            if (offset >= 0x100 && offset <= 0x10C) {
                int timerIdx = (offset - 0x100) / 4;
                uint16_t controlVal = (value >> 16) & 0xFFFF;
                
                uint16_t oldControl = io_regs[offset + 2] | (io_regs[offset + 3] << 8);
                bool wasEnabled = oldControl & 0x80;
                bool nowEnabled = controlVal & 0x80;
                
                if (!wasEnabled && nowEnabled) {
                    uint16_t reload = value & 0xFFFF;
                    timerCounters[timerIdx] = reload;
                    timerPrescalerCounters[timerIdx] = 0;
                }
            }
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
        static int dmaSeq = 0;
        static bool inImmediateDMA = false;  // Only guard immediate DMAs
        dmaSeq++;
        
        uint32_t baseOffset = 0xB0 + (channel * 0xC);
        
        // CNT_H (Control) - 16 bit
        uint16_t control = io_regs[baseOffset+10] | (io_regs[baseOffset+11] << 8);
        
        // Decode timing first
        int timing = (control >> 12) & 3;
        
        // Only guard immediate timing (timing=0) DMAs from recursion
        // Sound DMAs (timing=3) must be allowed to happen for audio to work
        if (timing == 0 && inImmediateDMA) {
            std::cout << "[DMA SKIP] DMA#" << dmaSeq << " ch" << channel << " timing=0 skipped (recursive)" << std::endl;
            return;
        }
        
        bool wasInImmediate = inImmediateDMA;
        if (timing == 0) {
            inImmediateDMA = true;
        }
        
        // CNT_L (Count) - 16 bit
        uint32_t count = io_regs[baseOffset+8] | (io_regs[baseOffset+9] << 8);
        
        // Decode Control
        bool is32Bit = (control >> 10) & 1;
        int destCtrl = (control >> 5) & 3;
        int srcCtrl = (control >> 7) & 3;
        
        uint32_t dst = dmaInternalDst[channel];
        uint32_t src = dmaInternalSrc[channel];
        
        // DEBUG: Trace DMA targeting IWRAM
        if ((dst & 0xFF000000) == 0x03000000) {
            std::cout << "[DMA#" << std::dec << dmaSeq << " ch" << channel << " to IWRAM] "
                      << "Dst=0x" << std::hex << dst 
                      << " Src=0x" << src 
                      << " Count=" << std::dec << count
                      << " 32bit=" << is32Bit
                      << " srcCtrl=" << srcCtrl
                      << " destCtrl=" << destCtrl
                      << " timing=" << timing
                      << " PC=0x" << std::hex;
            if (cpu) std::cout << cpu->GetRegister(15);
            std::cout << std::dec << std::endl;
        }
        
        // DEBUG: Trace DMA targeting VRAM (disabled)
        // if ((dst & 0xFF000000) == 0x06000000 && (dst & 0xFFFF0000) == 0x06000000) {
        //     uint32_t srcVal = is32Bit ? Read32(src) : Read16(src);
        //     std::cout << "[DMA#" << std::dec << dmaSeq << " ch" << channel << " to BG_VRAM] Src=0x" << std::hex << src
        //               << " Dst=0x" << dst
        //               << " Count=" << std::dec << count
        //               << " 32bit=" << is32Bit
        //               << " srcCtrl=" << srcCtrl
        //               << " srcVal=0x" << std::hex << srcVal << std::dec << std::endl;
        // }
        
        bool repeat = (control >> 9) & 1;
        bool irq = (control >> 14) & 1;
        
        uint32_t currentSrc = dmaInternalSrc[channel];
        uint32_t currentDst = dmaInternalDst[channel];

        // EEPROM Size Detection via DMA Count
        // 4Kbit EEPROM uses 6-bit address -> 9 bits total (2 cmd + 6 addr + 1 stop)
        // 64Kbit EEPROM uses 14-bit address -> 17 bits total (2 cmd + 14 addr + 1 stop)
        // Games use DMA to bit-bang these requests.
        if (currentDst >= 0x0D000000 && currentDst <= 0x0DFFFFFF) {
            if (!saveTypeLocked) {
                if (count == 9) {
                    if (eepromIs64Kbit) {
                        eepromIs64Kbit = false;
                        if (eepromData.size() != 512) {
                            eepromData.resize(512, 0xFF);
                        }
                    }
                } else if (count == 17) {
                    if (!eepromIs64Kbit) {
                        eepromIs64Kbit = true;
                        if (eepromData.size() < 8192) {
                            eepromData.resize(8192, 0xFF);
                        }
                    }
                }
            } else {
            }
        }

        if (currentSrc >= 0x0D000000 && currentSrc < 0x0E000000) {
        }
        
        if (currentDst >= 0x0D000000 && currentDst < 0x0E000000) {
        }
        
        // For sound DMA (timing mode 3), always transfer 4 words (16 bytes)
        if (timing == 3) {
            count = 4;
            is32Bit = true;
        }

        // DMA count register sizes differ:
        // - DMA0, DMA1, DMA2: 14-bit count (max 0x4000)
        // - DMA3: 16-bit count (max 0x10000)
        if (channel < 3) {
            count &= 0x3FFF;  // Mask to 14 bits for DMA0-2
        }
        
        if (count == 0) {
            count = (channel == 3) ? 0x10000 : 0x4000;
        }
        
        // DEBUG: Post-mask trace for DMA to 0x3001500
        if ((currentDst & 0x7FFF) == 0x1500 && (currentDst >> 24) == 0x03) {
            uint32_t currentVal = Read32(0x3001500);
            uint32_t firstVal = Read32(currentSrc);
            std::cout << "[DMA#" << std::dec << dmaSeq << " ch" << channel << " EXECUTE 0x3001500] "
                      << "Count(masked)=" << count
                      << " 32bit=" << is32Bit
                      << " FirstSrcValue=0x" << std::hex << firstVal
                      << " destCtrl=" << std::dec << destCtrl
                      << " BEFORE=0x" << std::hex << currentVal
                      << std::endl;
        }
        
        // WORKAROUND: DKC sound engine sets destCtrl=2 (Fixed) for DMA to IWRAM.
        // With Fixed destination, all values write to the same address repeatedly.
        // For large counts, this is audio streaming - we skip the actual writes
        // to avoid corrupting whatever value was there before.
        // The game expects the pre-existing value at the destination to remain.
        bool dstIsIWRAM = (currentDst >> 24) == 0x03;
        if (destCtrl == 2 && dstIsIWRAM && count > 100) {
            std::cout << "[DMA SKIP] destCtrl=2 IWRAM 0x" << std::hex << currentDst 
                      << " count=" << std::dec << count << " (Fixed dest - preserving existing value)" << std::endl;
            // Update timing as if full DMA happened
            int step = is32Bit ? 4 : 2;
            int totalCycles = 2 + count * (is32Bit ? 4 : 2);
            if (srcCtrl == 0) currentSrc += count * step;
            else if (srcCtrl == 1) currentSrc -= count * step;
            dmaInternalSrc[channel] = currentSrc;
            lastDMACycles += totalCycles;
            UpdateTimers(totalCycles);
            if (apu) apu->Update(totalCycles);
            if (ppu) ppu->Update(totalCycles);
            inImmediateDMA = wasInImmediate;
            io_regs[baseOffset+10] &= ~0x80;
            io_regs[baseOffset+11] &= ~0x80;
            return;
        }
        
        int step = is32Bit ? 4 : 2;
        int totalCycles = 2; // DMA Setup Overhead (approx)
        
        for (uint32_t i = 0; i < count; ++i) {
            if (is32Bit) {
                uint32_t val = Read32(currentSrc);
                Write32(currentDst, val);
                totalCycles += 4; // 32-bit transfer approx cycles
            } else {
                uint16_t val = Read16(currentSrc);
                Write16(currentDst, val);
                totalCycles += 2; // 16-bit transfer approx cycles
            }
            
            // Update Source Address
            if (srcCtrl == 0) currentSrc += step;
            else if (srcCtrl == 1) currentSrc -= step;
            
            // Update Dest Address (for sound DMA, dest is fixed - FIFO register)
            if (timing != 3) {
                if (destCtrl == 0 || destCtrl == 3) currentDst += step;
                else if (destCtrl == 1) currentDst -= step;
                // Fixed (2) -> No change
            }
            // For sound DMA (timing 3), destination is always fixed to FIFO
        }

        // Update system state to reflect DMA duration
        // This is crucial for games that check timers during DMA or expect delays
        lastDMACycles += totalCycles;
        UpdateTimers(totalCycles);
        if (apu) apu->Update(totalCycles);
        if (ppu) ppu->Update(totalCycles);
        
        // Save updated internal addresses
        dmaInternalSrc[channel] = currentSrc;
        // For repeat DMA with destCtrl=3 (Inc/Reload), reload destination
        if (repeat && destCtrl == 3) {
            // Reload destination from IO regs
            dmaInternalDst[channel] = io_regs[baseOffset+4] | (io_regs[baseOffset+5] << 8) | 
                                      (io_regs[baseOffset+6] << 16) | (io_regs[baseOffset+7] << 24);
        } else {
            dmaInternalDst[channel] = currentDst;
        }
        
        // DEBUG: Check jump table after DMA#1 (channel 3, dst starts at 0x3000000)
        if (channel == 3 && dst == 0x3000000 && gameCode == "A5NE") {
            // DMA#1 clears/fills IWRAM by writing the same value (from 0x3007ef0) everywhere
            // Now we need to initialize the audio engine area that the game expects
            std::cout << "[After DMA#1] Initializing audio engine area for DKC" << std::endl;
            
            // Clear entire audio engine area 0x3001400-0x30016FF to 0
            for (int i = 0x1400; i < 0x1700; i++) {
                wram_chip[i] = 0;
            }
            
            // Write audio init stub at 0x30013E0 (before the audio engine data area)
            // This stub clears the "not ready" flag at 0x3001420 and returns
            // ARM code:
            //   0x30013E0: LDR R12, [PC, #8]    ; Load 0x3001420 from literal pool at 0x30013F0
            //   0x30013E4: MOV R0, #0           ; R0 = 0
            //   0x30013E8: STR R0, [R12]        ; Store 0 at 0x3001420
            //   0x30013EC: BX LR                ; Return
            //   0x30013F0: .word 0x03001420     ; Literal pool
            
            // LDR R12, [PC, #8]  = 0xE59FC008
            wram_chip[0x13E0] = 0x08;
            wram_chip[0x13E1] = 0xC0;
            wram_chip[0x13E2] = 0x9F;
            wram_chip[0x13E3] = 0xE5;
            
            // MOV R0, #0 = 0xE3A00000
            wram_chip[0x13E4] = 0x00;
            wram_chip[0x13E5] = 0x00;
            wram_chip[0x13E6] = 0xA0;
            wram_chip[0x13E7] = 0xE3;
            
            // STR R0, [R12] = 0xE58C0000
            wram_chip[0x13E8] = 0x00;
            wram_chip[0x13E9] = 0x00;
            wram_chip[0x13EA] = 0x8C;
            wram_chip[0x13EB] = 0xE5;
            
            // BX LR = 0xE12FFF1E
            wram_chip[0x13EC] = 0x1E;
            wram_chip[0x13ED] = 0xFF;
            wram_chip[0x13EE] = 0x2F;
            wram_chip[0x13EF] = 0xE1;
            
            // Literal pool: 0x03001420
            wram_chip[0x13F0] = 0x20;
            wram_chip[0x13F1] = 0x14;
            wram_chip[0x13F2] = 0x00;
            wram_chip[0x13F3] = 0x03;
            
            // Jump table at 0x3001500-0x16FF: 128 entries (512 bytes), all point to stub at 0x30013E0
            for (int i = 0; i < 128; i++) {
                uint32_t offset = 0x1500 + i * 4;
                wram_chip[offset + 0] = 0xE0;  // 0x30013E0
                wram_chip[offset + 1] = 0x13;
                wram_chip[offset + 2] = 0x00;
                wram_chip[offset + 3] = 0x03;
            }
            
            uint32_t jumpTableVal = Read32(0x3001500);
            uint32_t stubVal = Read32(0x30013E0);  // Stub is at 0x30013E0, not 0x3001400
            uint32_t polledAddr = Read32(0x3001420);
            std::cout << "[After Init] JumpTable[0]=0x" << std::hex << jumpTableVal 
                      << " Stub@0x30013E0=0x" << stubVal 
                      << " PolledAddr[0x3001420]=0x" << polledAddr << std::dec << std::endl;
        }
        
        // Trigger IRQ
        if (irq) {
            uint16_t if_reg = io_regs[0x202] | (io_regs[0x203] << 8);
            if_reg |= (1 << (8 + channel)); // DMA0=Bit8, DMA1=Bit9, DMA2=Bit10, DMA3=Bit11
            io_regs[0x202] = if_reg & 0xFF;
            io_regs[0x203] = (if_reg >> 8) & 0xFF;
        }

        if (!repeat) {
            // Clear Enable Bit
            io_regs[baseOffset+11] &= 0x7F; // Clear Bit 15 of CNT_H (High byte)
        }
        
        inImmediateDMA = wasInImmediate;
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
                        
                        // Notify APU of timer overflow (for sound sample consumption)
                        if (apu && (i == 0 || i == 1)) {
                            apu->OnTimerOverflow(i);
                        }
                        
                        // IRQ
                        if (control & 0x40) {
                            uint16_t if_reg = io_regs[0x202] | (io_regs[0x203] << 8);
                            if_reg |= (1 << (3 + i));
                            io_regs[0x202] = if_reg & 0xFF;
                            io_regs[0x203] = (if_reg >> 8) & 0xFF;
                        }
                        
                        // Sound DMA trigger (Timer 0 and Timer 1 only)
                        if (i == 0 || i == 1) {
                            uint16_t soundcntH = io_regs[0x82] | (io_regs[0x83] << 8);
                            int fifoATimer = (soundcntH >> 10) & 1;
                            int fifoBTimer = (soundcntH >> 14) & 1;
                            
                            for (int dma = 1; dma <= 2; dma++) {
                                uint32_t dmaBase = 0xB0 + (dma * 0xC);
                                uint16_t dmaControl = io_regs[dmaBase+10] | (io_regs[dmaBase+11] << 8);
                                
                                if (dmaControl & 0x8000) {
                                    int dmaTiming = (dmaControl >> 12) & 3;
                                    if (dmaTiming == 3) {
                                        uint32_t dmaDest = io_regs[dmaBase+4] | (io_regs[dmaBase+5] << 8) | 
                                                          (io_regs[dmaBase+6] << 16) | (io_regs[dmaBase+7] << 24);
                                        
                                        bool isFifoA = (dmaDest == 0x040000A0);
                                        bool isFifoB = (dmaDest == 0x040000A4);
                                        
                                        if ((isFifoA && fifoATimer == i) || (isFifoB && fifoBTimer == i)) {
                                            PerformDMA(dma);
                                        }
                                    }
                                }
                            }
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
        uint16_t ret = 1; // Default to Ready
        
        if (eepromWriteDelay > 0) {
            return 0; // Busy
        }

        if (eepromState == EEPROMState::ReadDummy) {
            ret = 0;
            eepromBitCounter++;
            if (eepromBitCounter >= 4) { // Standard 4 dummy bits
                eepromState = EEPROMState::ReadData;
                eepromBitCounter = 0;
            }
        } else if (eepromState == EEPROMState::ReadData) {
            int bitIndex = 63 - eepromBitCounter;
            
            ret = (eepromBuffer >> bitIndex) & 1;
            
            eepromBitCounter++;
            if (eepromBitCounter >= 64) {
                eepromState = EEPROMState::Idle;
                eepromBitCounter = 0;
            }
        } else {
            // Active but not outputting data (e.g. receiving address) or Idle
            // The GBA data bus is pulled up (High-Z) when not driven by the EEPROM
            ret = 1;
        }
        
        return ret;
    }

    void GBAMemory::WriteEEPROM(uint16_t value) {
        if (eepromWriteDelay > 0) {
            return;
        }

        uint8_t bit = value & 1;
        eepromLatch = bit; // Update latch
        
        switch (eepromState) {
            case EEPROMState::Idle:
                if (bit == 1) {
                    eepromState = EEPROMState::ReadCommand;
                }
                break;
                
            case EEPROMState::ReadCommand:
                if (bit == 1) {
                    eepromState = EEPROMState::ReadAddress; // Command 11 = READ
                    eepromBitCounter = 0;
                    eepromAddress = 0;
                } else {
                    eepromState = EEPROMState::WriteAddress; // Command 10 = WRITE
                    eepromBitCounter = 0;
                    eepromAddress = 0;
                }
                break;
                
            case EEPROMState::ReadAddress:
                eepromAddress = (eepromAddress << 1) | bit;
                eepromBitCounter++;
                
                if (eepromBitCounter >= (eepromIs64Kbit ? 14 : 6)) {
                    uint32_t blockCount = eepromIs64Kbit ? 1024 : 64;
                    eepromAddress = eepromAddress % blockCount;
                    
                    // Prepare data buffer immediately
                    uint32_t offset = eepromAddress * 8;
                    eepromBuffer = 0;
                    if (offset + 7 < eepromData.size()) {
                        for (int i = 0; i < 8; ++i) {
                            eepromBuffer |= ((uint64_t)eepromData[offset + i] << (56 - i * 8));
                        }
                    } else {
                        eepromBuffer = 0xFFFFFFFFFFFFFFFFULL;
                    }

                    eepromState = EEPROMState::ReadStopBit;
                    eepromBitCounter = 0;
                }
                break;

            case EEPROMState::ReadStopBit:
                // Expecting a '0' bit to terminate the read request
                if (bit != 0) {
                    // SMA2 sends Stop Bit 1.
                    // This violates the standard, but the game still expects dummy bits (DMA count is 68).
                    // If we skip dummy bits, the data is shifted and corrupted.
                }
                
                // Always proceed to ReadDummy
                eepromState = EEPROMState::ReadDummy;
                eepromBitCounter = 0;
                break;
                
            case EEPROMState::WriteAddress:
                eepromAddress = (eepromAddress << 1) | bit;
                eepromBitCounter++;
                if (eepromBitCounter >= (eepromIs64Kbit ? 14 : 6)) {
                    eepromAddress &= eepromIs64Kbit ? 0x3FF : 0x3F;
                    eepromState = EEPROMState::WriteData;
                    eepromBitCounter = 0;
                    eepromBuffer = 0;
                }
                break;
                
            case EEPROMState::WriteData:
                eepromBuffer = (eepromBuffer << 1) | bit;
                eepromBitCounter++;
                if (eepromBitCounter >= 64) {
                    eepromState = EEPROMState::WriteTermination;
                    eepromBitCounter = 0;
                }
                break;
                
            case EEPROMState::WriteTermination:
                // Expecting a '0' bit to terminate the write command
                
                if (bit == 0) {
                    // Valid Stop Bit - Commit Write
                    uint32_t offset = eepromAddress * 8;
                    
                    if (offset + 7 < eepromData.size()) {
                        for (int i = 0; i < 8; ++i) {
                            uint8_t byteVal = (eepromBuffer >> (56 - i * 8)) & 0xFF;
                            eepromData[offset + i] = byteVal;
                        }
                    }
                    FlushSave();
                    // Set busy delay (Standard EEPROM write time is ~10ms)
                    // 16.78MHz clock -> ~167,800 cycles for 10ms.
                    // SMA2 might check for a Busy -> Ready transition.
                    // Setting a realistic delay to satisfy hardware checks.
                    // INCREASED: 1000 was too short. Real hardware is ~100k.
                    // Using 100,000 cycles (approx 6ms).
                    // REDUCED: 100,000 causes interrupt storms in some games (SMA2).
                    // Reducing to 2000 to unblock the game while still providing some delay.
                    eepromWriteDelay = 2000; 
                } else {
                }
                
                eepromState = EEPROMState::Idle;
                break;
                
            case EEPROMState::ReadDummy:
            case EEPROMState::ReadData:
                // During read phases, writes are for DMA clocking - ignored
                break;
                
            default:
                eepromState = EEPROMState::Idle;
                break;
        }
    }

}
