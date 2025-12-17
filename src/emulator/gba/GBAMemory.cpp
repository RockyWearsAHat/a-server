#include <emulator/gba/GBAMemory.h>
#include <emulator/gba/ARM7TDMI.h>
#include <emulator/gba/APU.h>
#include <emulator/gba/PPU.h>
#include <emulator/gba/GameDB.h>
#include <emulator/gba/IORegs.h>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace AIO::Emulator::GBA {

    GBAMemory::GBAMemory() {
        // Initialize memory vectors with correct sizes (GBATEK)
        bios.resize(MemoryMap::BIOS_SIZE, 0);
        wram_board.resize(MemoryMap::WRAM_BOARD_SIZE);
        wram_chip.resize(MemoryMap::WRAM_CHIP_SIZE);
        io_regs.resize(MemoryMap::IO_REG_SIZE);
        palette_ram.resize(MemoryMap::PALETTE_SIZE);
        vram.resize(MemoryMap::VRAM_SIZE);
        oam.resize(MemoryMap::OAM_SIZE);
        // ROM and SRAM sizes depend on the loaded game, but we can set defaults
        rom.resize(MemoryMap::ROM_MAX_SIZE);
        sram.resize(SaveTypes::SRAM_SIZE);
        // Default EEPROM state is erased (0xFF); game-specific init happens in LoadSave
        eepromData.resize(EEPROM::SIZE_64K, EEPROM::ERASED_VALUE);
        
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
        // We use DirectBoot mode (start at 0x08000000) but initialize
        // hardware state as if the BIOS had run
        
        // Fill BIOS with NOP instructions (0xE320F000 = ARM NOP)
        // Real BIOS code is not executed, but region must be readable
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
        // CRITICAL: User handler is responsible for clearing IF register.
        // BIOS does NOT clear IF automatically - that's the game's job.
        
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
        
        // NOTE: IRQ trampoline at 0x180-0x1DC must NOT be overwritten!
        // All BIOS functions should be called via SWI, handled by ExecuteSWI().
        // Direct BIOS function calls are not supported in HLE mode.
    }

    void GBAMemory::Reset() {
        // Initialize WRAM to match real GBA hardware state after BIOS boot
        // Real hardware: BIOS does NOT clear all WRAM, leaving undefined values
        // Testing shows simple incremental pattern matches observed behavior
        
        // EWRAM: Initialize to 0 (safer for audio buffers that may be read before written)
        std::fill(wram_board.begin(), wram_board.end(), 0);
        
        // IWRAM: Initialize to 0 BUT preserve BIOS-managed regions.
        // For HLE stability, keep the IRQ stack region deterministic (0-filled).
        std::fill(wram_chip.begin(), wram_chip.end(), 0);
        
        // BIOS HLE: Initialize IRQ stack region (0x03007FA0-0x03007FDF = 64 bytes)
        // Real BIOS reserves this for IRQ mode stack.
        if (wram_chip.size() >= 0x8000) {
            
            // Initialize User Interrupt Handler to Dummy Handler in BIOS (0x00003FF0)
            // 0x03007FFC points to game's IRQ handler (real games set this)
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
        
        // Initialize display control: Mode 0 with BG0 enabled so something is visible before the game configures it.
        if (io_regs.size() > IORegs::DISPCNT + 1) {
            uint16_t dispcnt = 0x0100; // Mode 0 (bits 0-2 = 0), BG0 enable (bit 8 = 1)
            io_regs[IORegs::DISPCNT] = dispcnt & 0xFF;
            io_regs[IORegs::DISPCNT + 1] = (dispcnt >> 8) & 0xFF;
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
        
        // Initialize DMA Registers to Safe Defaults
        // All DMA channels should be disabled (Enable bit = 0) on boot
        // DMA3 specifically: initialize control register (0x0DE) to 0x0000
        // This prevents any accidental DMA transfers on boot
        for (int i = 0; i < 4; ++i) {
            uint32_t cntOffset = IORegs::DMA0CNT_H + i * IORegs::DMA_CHANNEL_SIZE;
            if (io_regs.size() > cntOffset + 1) {
                io_regs[cntOffset] = 0x00;
                io_regs[cntOffset + 1] = 0x00;
            }
        }
        
        // Initialize Interrupt Enable (IE) Register
        // CRITICAL: Leave interrupts DISABLED on boot. Real BIOS leaves IE=0 and IME=0.
        // Games explicitly enable specific interrupts after installing their handler.
        // Enabling VBlank prematurely causes IRQ storm before game handler is ready.
        if (io_regs.size() > IORegs::IE + 1) {
            io_regs[IORegs::IE] = 0x00;
            io_regs[IORegs::IE + 1] = 0x00;
        }
        
        // Initialize Master Interrupt Enable (IME) Register
        // CRITICAL: Leave IME DISABLED on boot (IME=0). Real BIOS does NOT enable global IRQs.
        // Games enable IME after setting up their interrupt handler.
        if (io_regs.size() > IORegs::IME + 1) {
            io_regs[IORegs::IME] = 0x00;
            io_regs[IORegs::IME + 1] = 0x00;
        }


        eepromState = EEPROMState::Idle;
        eepromBitCounter = 0;
        eepromBuffer = 0;
        eepromAddress = 0;
        eepromLatch = 0; // Initialize latch
        eepromWriteDelay = 0; // Reset write delay
        // saveTypeLocked = false; // Do NOT reset this, as it's set by LoadGamePak

        // BIOS HLE: Initialize critical system state that real BIOS sets up
        // Many games poll specific IWRAM addresses waiting for BIOS background tasks to complete
        // Without full BIOS emulation, we must pre-initialize these to unblock boot sequences
        
        // System-ready flags: Games check various addresses for non-zero to confirm init complete
        // Common addresses: 0x3002b64, 0x3007ff8 (BIOS_IF), 0x3007ffc (IRQ handler)
        // Strategy: Set multiple known init flags to bypass common wait loops
        if (wram_chip.size() >= 0x8000) {
            // 0x3002b64: System init flag (SMA2, Pokemon, others)
            // Set to 0xABCD as a distinctive non-zero marker that games unlikely to explicitly set
            wram_chip[0x2b64] = 0xCD;
            wram_chip[0x2b65] = 0xAB;
            
            // 0x3007FF8: BIOS_IF (interrupt acknowledge from BIOS)
            wram_chip[0x7FF8] = 0x00;
            wram_chip[0x7FF9] = 0x00;
            wram_chip[0x7FFA] = 0x00;
            wram_chip[0x7FFB] = 0x00;
            
            // 0x3007FFC: User IRQ handler (already set above to 0x3FF0)
            // 0x3007FF4: Temp storage for triggered interrupts (used by BIOS IRQ dispatcher)
            wram_chip[0x7FF4] = 0x00;
            wram_chip[0x7FF5] = 0x00;
        }

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
        
        // GBA BIOS Header Validation - Required for games to boot correctly!
        // The real BIOS validates the header checksum and sets a flag in IWRAM.
        // If validation fails, games detect this as piracy/invalid cartridge.
        if (data.size() >= 0xBE) {
            // Calculate complement checksum (offset 0xA0-0xBC)
            uint8_t chk = 0;
            for (uint32_t i = 0xA0; i <= 0xBC; i++) {
                chk = (chk - data[i]) & 0xFF;
            }
            chk = (chk - 0x19) & 0xFF;
            
            // Check against stored checksum at 0xBD
            if (chk == data[0xBD]) {
                // Header valid - Set BIOS validation flag in IWRAM
                // Real BIOS writes 01h to 0x03007FFA after successful validation
                std::cout << "[GBAMemory] ROM header checksum valid (0x" << std::hex << (int)chk << std::dec << ")" << std::endl;
                if (wram_chip.size() >= 0x7FFB) {
                    wram_chip[0x7FFA] = 0x01; // Header validated
                }
            } else {
                std::cerr << "[GBAMemory] WARNING: ROM header checksum mismatch!" << std::endl;
                std::cerr << "[GBAMemory] Expected: 0x" << std::hex << (int)data[0xBD] 
                          << ", Calculated: 0x" << (int)chk << std::dec << std::endl;
                // Set flag to 0 (validation failed)
                if (wram_chip.size() >= 0x7FFB) {
                    wram_chip[0x7FFA] = 0x00; // Header validation failed
                }
            }
        }
        
        SaveType saveType = SaveType::Auto;
        bool locked = false;

        // Note: Detailed save type detection is now handled by ROMMetadataAnalyzer
        // in GBA::LoadROM() which runs AFTER LoadGamePak and calls SetSaveType().
        // Here we do minimal detection to ensure the save system is initialized properly.

        // Store game code for reference
        if (data.size() >= 0xB0) {
            std::string detectedCode(reinterpret_cast<const char*>(&data[0xAC]), 4);
            this->gameCode = detectedCode;
        }

        // Fallback: DMA Scan for EEPROM size detection (if string markers not found)
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
                eepromData.resize(EEPROM::SIZE_4K, EEPROM::ERASED_VALUE);
                break;
            case SaveType::EEPROM_64K:
                eepromIs64Kbit = true;
                eepromData.resize(EEPROM::SIZE_64K, EEPROM::ERASED_VALUE);
                break;
            case SaveType::SRAM:
                hasSRAM = true;
                sram.resize(SaveTypes::SRAM_SIZE, EEPROM::ERASED_VALUE);
                break;
            case SaveType::Flash512:
                isFlash = true;
                hasSRAM = true;
                sram.resize(SaveTypes::FLASH_512K_SIZE, EEPROM::ERASED_VALUE);
                break;
            case SaveType::Flash1M:
                isFlash = true;
                hasSRAM = true;
                sram.resize(SaveTypes::FLASH_1M_SIZE, EEPROM::ERASED_VALUE);
                break;
            default:
                // Default to 64K EEPROM (GBATEK)
                eepromIs64Kbit = true;
                eepromData.resize(EEPROM::SIZE_64K, EEPROM::ERASED_VALUE);
                break;
        }
    }

    void GBAMemory::LoadSave(const std::vector<uint8_t>& data) {
        std::cout << "[LoadSave] Called with " << data.size() << " bytes" << std::endl;
        
        // DEBUG: Verify data parameter at entry
        if (!data.empty() && data.size() >= 0x28) {
            std::cout << "[LoadSave DEBUG ENTRY] data[0x10-0x27]: " << std::hex;
            for (size_t i = 0x10; i < 0x28; i++) {
                std::cout << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
            }
            std::cout << std::dec << std::endl;
        }
        
        const size_t targetSize = eepromIs64Kbit ? 8192 : 512;
        if (!data.empty()) {
            std::cout << "[LoadSave] Loading save data. First 16 bytes: " << std::hex;
            for (size_t i = 0; i < std::min(data.size(), size_t(16)); i++) {
                std::cout << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
            }
            std::cout << std::dec << std::endl;
            
            eepromData = data;
            std::cout << "[LoadSave] eepromData.size() after assignment: " << eepromData.size() << std::endl;
            
            // Validate size against detected type
            if (eepromData.size() != targetSize) {
                std::cout << "[LoadSave] Size mismatch: got " << eepromData.size() << ", expected " << targetSize << ", resizing and clearing..." << std::endl;
                // Ensure stale data is not kept when sizes differ
                eepromData.assign(targetSize, 0xFF);
                // Copy as much as fits from the incoming data
                for (size_t i = 0; i < std::min(data.size(), targetSize); ++i) {
                    eepromData[i] = data[i];
                }
            }

            // Detect obviously blank/erased saves and initialize a clean image
            bool allFF = std::all_of(eepromData.begin(), eepromData.end(), [](uint8_t b){ return b == 0xFF; });
            if (allFF) {
                std::cout << "[LoadSave] Save is fully erased (all 0xFF). Initializing fresh EEPROM image." << std::endl;
                eepromData.assign(targetSize, 0xFF);
                FlushSave();
            }
            
            std::cout << "[LoadSave] Final eepromData.size(): " << eepromData.size() << std::endl;
            std::cout << "[LoadSave] First 16 bytes after load: " << std::hex;
            for (size_t i = 0; i < std::min(eepromData.size(), size_t(16)); i++) {
                std::cout << std::setw(2) << std::setfill('0') << (int)eepromData[i] << " ";
            }
            std::cout << std::dec << std::endl;
            
            // DEBUG: Print first 64 bytes to verify array integrity
            std::cout << "[LoadSave DEBUG] First 64 bytes of eepromData:" << std::endl;
            for (size_t i = 0; i < std::min(eepromData.size(), size_t(64)); i += 8) {
                std::cout << "  [0x" << std::hex << std::setw(2) << std::setfill('0') << i << "] ";
                for (size_t j = 0; j < 8 && (i+j) < eepromData.size(); j++) {
                    std::cout << std::setw(2) << std::setfill('0') << (int)eepromData[i+j] << " ";
                }
                std::cout << std::dec << std::endl;
            }
        } else {
            std::cout << "[LoadSave] Empty save data - initializing fresh EEPROM (filled with 0xFF)" << std::endl;
            // Always reset to a clean erased image to avoid stale in-memory contents
            eepromData.assign(targetSize, 0xFF);
            FlushSave();
        }
    }

    std::vector<uint8_t> GBAMemory::GetSaveData() const {
        return eepromData;
    }
    
    void GBAMemory::SetSavePath(const std::string& path) {
        savePath = path;
    }

    void GBAMemory::SetSaveType(SaveType type) {
        std::cout << "[GBAMemory] Configuring save type: ";
        
        // Store current save data before resizing
        std::vector<uint8_t> existingData = eepromData;
        std::cout << "[SetSaveType] Existing eepromData.size() = " << existingData.size() << std::endl;
        
        if (!existingData.empty()) {
            std::cout << "[SetSaveType] Existing first 16 bytes: " << std::hex;
            for (size_t i = 0; i < std::min(existingData.size(), size_t(16)); i++) {
                std::cout << std::setw(2) << std::setfill('0') << (int)existingData[i] << " ";
            }
            std::cout << std::dec << std::endl;
        }
        
        switch (type) {
            case SaveType::SRAM:
                std::cout << "SRAM" << std::endl;
                hasSRAM = true;
                isFlash = false;
                if (eepromData.size() != 32768) {
                    eepromData.resize(32768, 0xFF); // 32KB SRAM
                    // Preserve existing data if we had any
                    if (!existingData.empty()) {
                        size_t copySize = std::min(existingData.size(), eepromData.size());
                        std::copy(existingData.begin(), existingData.begin() + copySize, eepromData.begin());
                    }
                }
                break;
            case SaveType::Flash512:
                std::cout << "Flash 512K" << std::endl;
                hasSRAM = true;
                isFlash = true;
                flashBank = 0;
                if (eepromData.size() != 65536) {
                    eepromData.resize(65536, 0xFF); // 512K Flash
                    if (!existingData.empty()) {
                        size_t copySize = std::min(existingData.size(), eepromData.size());
                        std::copy(existingData.begin(), existingData.begin() + copySize, eepromData.begin());
                    }
                }
                break;
            case SaveType::Flash1M:
                std::cout << "Flash 1M" << std::endl;
                hasSRAM = true;
                isFlash = true;
                flashBank = 0;
                if (eepromData.size() != 131072) {
                    eepromData.resize(131072, 0xFF); // 1M Flash (both banks)
                    if (!existingData.empty()) {
                        size_t copySize = std::min(existingData.size(), eepromData.size());
                        std::copy(existingData.begin(), existingData.begin() + copySize, eepromData.begin());
                    }
                }
                break;
            case SaveType::EEPROM_4K:
                std::cout << "EEPROM 4K" << std::endl;
                hasSRAM = false;
                isFlash = false;
                eepromIs64Kbit = false;
                if (eepromData.size() != 512) {
                    eepromData.resize(512, 0xFF); // 4Kbit EEPROM
                    if (!existingData.empty()) {
                        size_t copySize = std::min(existingData.size(), eepromData.size());
                        std::copy(existingData.begin(), existingData.begin() + copySize, eepromData.begin());
                    }
                }
                break;
            case SaveType::EEPROM_64K:
                std::cout << "EEPROM 64K" << std::endl;
                hasSRAM = false;
                isFlash = false;
                eepromIs64Kbit = true;
                if (eepromData.size() != 8192) {
                    std::cout << "[SetSaveType] Resizing from " << eepromData.size() << " to 8192" << std::endl;
                    eepromData.resize(8192, 0xFF); // 64Kbit EEPROM
                    if (!existingData.empty()) {
                        size_t copySize = std::min(existingData.size(), eepromData.size());
                        std::cout << "[SetSaveType] Copying " << copySize << " bytes from existingData" << std::endl;
                        std::copy(existingData.begin(), existingData.begin() + copySize, eepromData.begin());
                    }
                } else {
                    std::cout << "[SetSaveType] Size already correct (8192), no resize needed" << std::endl;
                }
                break;
            case SaveType::Auto:
                std::cout << "Auto-detect (no change)" << std::endl;
                break;
            default:
                std::cout << "Unknown" << std::endl;
        }
        
        std::cout << "[SetSaveType] Final eepromData.size() = " << eepromData.size() << std::endl;
        if (!eepromData.empty()) {
            std::cout << "[SetSaveType] Final first 16 bytes: " << std::hex;
            for (size_t i = 0; i < std::min(eepromData.size(), size_t(16)); i++) {
                std::cout << std::setw(2) << std::setfill('0') << (int)eepromData[i] << " ";
            }
            std::cout << std::dec << std::endl;
        }
        
        saveTypeLocked = true; // Prevent further dynamic detection
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
        if (io_regs.size() > IORegs::KEYINPUT + 1) {
            io_regs[IORegs::KEYINPUT] = value & 0xFF;
            io_regs[IORegs::KEYINPUT + 1] = (value >> 8) & 0xFF;
        }
    }

    int GBAMemory::GetAccessCycles(uint32_t address, int accessSize) const {
        // GBA Memory Access Timing (GBATEK)
        // Returns cycles for the given access size (1=8bit, 2=16bit, 4=32bit)
        uint8_t region = (address >> 24);
        
        switch (region) {
            case 0x00: // BIOS (16-bit bus)
                return (accessSize == 4) ? 2 : 1;
            
            case 0x02: // EWRAM (16-bit bus, 2 wait states)
                return (accessSize == 4) ? 6 : 3;
            
            case 0x03: // IWRAM (32-bit bus, 0 wait states)
                return 1;
            
            case 0x04: // I/O (16-bit bus, 0 wait states)
                return 1;
            
            case 0x05: // Palette RAM (16-bit bus, 0 wait states)
                return (accessSize == 4) ? 2 : 1;
            
            case 0x06: // VRAM (16-bit bus, 0 wait states)
                return (accessSize == 4) ? 2 : 1;
            
            case 0x07: // OAM (32-bit bus, 0 wait states)
                return 1;
            
            case 0x08: // ROM Wait State 0 (16-bit bus, default 4+2 wait)
            case 0x09:
                return (accessSize == 4) ? 8 : 5; // Non-sequential + sequential
            
            case 0x0A: // ROM Wait State 1 (16-bit bus, default 4+4 wait)
            case 0x0B:
                return (accessSize == 4) ? 10 : 6;
            
            case 0x0C: // ROM Wait State 2 (16-bit bus, default 4+8 wait)
            case 0x0D: // Also used for EEPROM
                return (accessSize == 4) ? 14 : 9;
            
            case 0x0E: // SRAM/Flash (8-bit bus, 4+4 wait)
                return 5;
            
            default:
                return 1;
        }
    }

    uint8_t GBAMemory::Read8(uint32_t address) {
        uint8_t region = (address >> 24);
        switch (region) {
            case 0x00: // BIOS (GBATEK: 0x00000000-0x00003FFF)
                if (address < bios.size()) {
                    return bios[address];
                }
                break;
            case 0x02: // WRAM (Board) (GBATEK: 0x02000000-0x0203FFFF)
                return wram_board[address & MemoryMap::WRAM_BOARD_MASK];
            case 0x03: // WRAM (Chip) (GBATEK: 0x03000000-0x03007FFF)
                return wram_chip[address & MemoryMap::WRAM_CHIP_MASK];
            case 0x04: // IO Registers (GBATEK: 0x04000000-0x040003FF)
            {
                uint32_t offset = address & MemoryMap::IO_REG_MASK;
                uint8_t val = 0;
                if (offset < io_regs.size()) val = io_regs[offset];
                
                // SOUNDCNT_X (0x84) - Return proper status
                if (offset == IORegs::SOUNDCNT_X) {
                    val = io_regs[IORegs::SOUNDCNT_X] & 0x80; // Only preserve master enable bit
                }
                
                // DEBUG: Log reads from DMA3 registers during EEPROM loop
                if (offset >= 0xD4 && offset <= 0xDF) {
                    // DMA3 region - these are likely EEPROM validation reads
                    // Just log to help trace the issue (disabled in final version)
                }
                
                return val;
            }
            case 0x05: // Palette RAM (GBATEK: 0x05000000-0x050003FF)
            {
                uint32_t offset = address & MemoryMap::PALETTE_MASK;
                if (offset < palette_ram.size()) return palette_ram[offset];
                break;
            }
            case 0x06: // VRAM (GBATEK: 0x06000000-0x06017FFF)
            {
                // VRAM is 96KB (0x18000 bytes) which is NOT a power of 2
                uint32_t rawOffset = address & 0xFFFFFF;
                uint32_t offset = rawOffset % MemoryMap::VRAM_ACTUAL_SIZE;
                if (offset < vram.size()) return vram[offset];
                break;
            }
            case 0x07: // OAM (GBATEK: 0x07000000-0x070003FF)
            {
                uint32_t offset = address & MemoryMap::OAM_MASK;
                if (offset < oam.size()) return oam[offset];
                break;
            }
            case 0x08: // Game Pak ROM (GBATEK: 0x08000000-0x0DFFFFFF)
            case 0x09:
            case 0x0A:
            case 0x0B:
            case 0x0C:
            {
                 // Universal ROM Mirroring (max 32MB space, mirrored every rom.size())
                 uint32_t offset = address & MemoryMap::ROM_MIRROR_MASK;
                 if (!rom.empty()) {
                     return rom[offset % rom.size()];
                 }
                 break;
            }
            case 0x0D: // EEPROM (GBATEK: 0x0D000000)
            {
                // Route 8-bit reads through the EEPROM state machine so the
                // serial line returns READY/BUSY bits instead of zero.
                // Even though games typically use 16-bit DMA, some titles may
                // poll with byte reads during the protocol handshake.
                return ReadEEPROM() & 0xFF;
            }
            case 0x0E: // SRAM/Flash (GBATEK: 0x0E000000-0x0E00FFFF)
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
                    if (offset == 0) return SaveTypes::FLASH_MAKER_ID;     // Maker: Macronix
                    if (offset == 1) return SaveTypes::FLASH_DEVICE_512K; // Device: 512K (TODO: Handle 1Mbit)
                    return 0;
                }
                // Normal Read
                uint32_t offset = address & 0xFFFF;
                // Handle 1Mbit banking if needed (offset > 64K)
                // But standard addressing is 64K window.
                // If sram.size() > 64K, use flashBank.
                if (sram.size() > SaveTypes::FLASH_512K_SIZE) {
                    offset += (flashBank * SaveTypes::FLASH_512K_SIZE);
                }
                if (offset < sram.size()) return sram[offset];
                return EEPROM::ERASED_VALUE; // Open bus / erased
            }
        }
        return 0;
    }

    uint16_t GBAMemory::Read16(uint32_t address) {
           // Universal EEPROM Handling: Only allow reads from 0x0D region
           uint8_t region = (address >> 24);
           if (region == 0x0D) {
               return ReadEEPROM();
           }

        uint16_t val = Read8(address) | (Read8(address + 1) << 8);

        // Timer Counters (Read from internal state)
        if ((address & 0xFF000000) == IORegs::BASE) {
             uint32_t offset = address & MemoryMap::IO_REG_MASK;
             
             if (offset >= IORegs::TM0CNT_L && offset <= IORegs::TM3CNT_H) {
                 int timerIdx = (offset - IORegs::TM0CNT_L) / IORegs::TIMER_CHANNEL_SIZE;
                 if ((offset % IORegs::TIMER_CHANNEL_SIZE) == 0) {
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
        // Universal EEPROM Handling - 32-bit read performs two 16-bit reads from 0x0D region only
        uint8_t region = (address >> 24);
        if (region == 0x0D) {
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
        uint8_t region = (address >> 24);
        switch (region) {
            case 0x02: // WRAM (Board) (GBATEK: 0x02000000-0x0203FFFF)
                wram_board[address & MemoryMap::WRAM_BOARD_MASK] = value;
                break;
            case 0x03: // WRAM (Chip) (GBATEK: 0x03000000-0x03007FFF)
            {
                uint32_t offset = address & MemoryMap::WRAM_CHIP_MASK;
                
                // BIOS HLE: Log writes to system-ready flag to see who clears it
                static int writeLogCount = 0;
                if ((offset == 0x2b64 || offset == 0x2b65) && writeLogCount < 20) {
                    std::cout << "[BIOS HLE] WRITE to 0x3002b6" << ((offset == 0x2b64) ? "4" : "5") 
                              << " = 0x" << std::hex << (int)value << " PC=0x" << (cpu ? cpu->GetRegister(15) : 0) 
                              << std::dec << " (write #" << writeLogCount << ")" << std::endl;
                    writeLogCount++;
                }
                
                // BIOS HLE: Protect system-ready flag from game's own init clearing it
                // Real BIOS runs background tasks that continuously set this flag
                // Without BIOS, prevent game from clearing it so boot can proceed
                static int writeCount = 0;
                static int protectCount = 0;
                if (offset == 0x2b64 || offset == 0x2b65) {
                    writeCount++;
                    if (value == 0 && writeCount < 100) {
                        protectCount++;
                        if (protectCount < 10) {
                            std::cout << "[BIOS HLE] Blocked write to system flag 0x3002b64+offset=" << (offset-0x2b64) 
                                      << " value=0 (protection #" << protectCount << ")" << std::endl;
                        }
                        return; // Ignore first 100 writes that try to clear the flag
                    }
                }
                
                wram_chip[offset] = value;
                break;
            }
            case 0x04: // IO Registers (GBATEK: 0x04000000-0x040003FF)
            {
                uint32_t offset = address & MemoryMap::IO_REG_MASK;
                if (offset < io_regs.size()) {
                    io_regs[offset] = value;
                }
                break;
            }
            case 0x05: // Palette RAM (GBATEK: 0x05000000-0x050003FF)
            {
                uint32_t offset = address & MemoryMap::PALETTE_MASK;
                
                static int paletteWriteCount = 0;
                if (paletteWriteCount < 50) {
                    paletteWriteCount++;
                    std::cout << "[PALETTE WRITE] offset=0x" << std::hex << offset 
                              << " val=0x" << (int)value;
                    if (cpu) std::cout << " PC=0x" << cpu->GetRegister(15);
                    std::cout << std::dec << std::endl;
                }
                
                if (offset < palette_ram.size()) palette_ram[offset] = value;
                break;
            }
            case 0x06: // VRAM (GBATEK: 0x06000000-0x06017FFF)
            {
                uint32_t rawOffset = address & 0xFFFFFF;
                uint32_t offset = rawOffset % MemoryMap::VRAM_ACTUAL_SIZE;
                
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
                    // DISABLED: Too verbose during boot
                    // if (offset < 0x4000 && value != 0) {
                    //     vramCharWriteCount++;
                    //     if (vramCharWriteCount <= 20) {
                    //         std::cout << "[VRAM Char0 Write] offset=0x" << std::hex << offset 
                    //                   << " val=0x" << (int)value;
                    //         if (cpu) std::cout << " PC=0x" << cpu->GetRegister(15);
                    //         std::cout << std::dec << std::endl;
                    //     }
                    // }

                if (offset < vram.size()) vram[offset] = value;
                break;
            }
            case 0x07: // OAM (GBATEK: 0x07000000-0x070003FF)
            {
                uint32_t offset = address & MemoryMap::OAM_MASK;
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
            {
                uint32_t offset = address & 0x7FFF;
                
                // BIOS HLE: Permanently protect system-ready flag from being cleared
                // Real BIOS sets this after initialization; games poll it to detect boot complete
                if ((offset == 0x2b64 || offset == 0x2b65) && value == 0) {
                    return; // Block all zero-writes permanently
                }
                
                wram_chip[offset] = value;
                break;
            }
            case 0x04: // IO Registers (GBATEK: 0x04000000-0x040003FF)
            {
                uint32_t offset = address & MemoryMap::IO_REG_MASK;
                
                // Handle IF (Interrupt Request) - Write 1 to Clear (GBATEK)
                if (offset == IORegs::IF || offset == IORegs::IF + 1) {
                    if (offset < io_regs.size()) {
                        io_regs[offset] &= ~value;
                    }
                } else {
                    // Protect DISPSTAT (0x04) Read-Only bits (0-2) (GBATEK)
                    if (offset == IORegs::DISPSTAT) {
                        uint8_t currentVal = io_regs[offset];
                        uint8_t readOnlyMask = 0x07;
                        value = (value & ~readOnlyMask) | (currentVal & readOnlyMask);
                    }

                    // Handle DMA Enable (GBATEK: DMA0-3 CNT_H enable bit)
                    bool dmaLatchNeeded = false;
                    int dmaChannel = -1;
                    if (offset == IORegs::DMA0CNT_H + 1 || 
                        offset == IORegs::DMA1CNT_H + 1 || 
                        offset == IORegs::DMA2CNT_H + 1 || 
                        offset == IORegs::DMA3CNT_H + 1) {
                        if (offset == IORegs::DMA0CNT_H + 1) dmaChannel = 0;
                        else if (offset == IORegs::DMA1CNT_H + 1) dmaChannel = 1;
                        else if (offset == IORegs::DMA2CNT_H + 1) dmaChannel = 2;
                        else if (offset == IORegs::DMA3CNT_H + 1) dmaChannel = 3;
                        
                        // NOTE: Write8 to the high byte (offset+1) means value is only a single byte
                        // Bit 15 (Enable) is bit 7 of the high byte!
                        bool wasEnabled = (io_regs[offset] & 0x80) != 0;  // Bit 7 of high byte = bit 15 of full 16-bit
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
                        uint32_t dmaBase = IORegs::DMA0SAD + (dmaChannel * IORegs::DMA_CHANNEL_SIZE);
                        dmaInternalSrc[dmaChannel] = io_regs[dmaBase] | (io_regs[dmaBase+1] << 8) | 
                                                  (io_regs[dmaBase+2] << 16) | (io_regs[dmaBase+3] << 24);
                        dmaInternalDst[dmaChannel] = io_regs[dmaBase+4] | (io_regs[dmaBase+5] << 8) | 
                                                  (io_regs[dmaBase+6] << 16) | (io_regs[dmaBase+7] << 24);

                        static int dmaStartLogs[4] = {0,0,0,0};
                        if (verboseLogs && dmaStartLogs[dmaChannel] < 8) {
                            uint16_t ctrl = io_regs[dmaBase+10] | (io_regs[dmaBase+11] << 8);
                            uint16_t cnt = io_regs[dmaBase+8] | (io_regs[dmaBase+9] << 8);
                            dmaStartLogs[dmaChannel]++;
                            std::cout << "[DMA" << dmaChannel << " START] Src=0x" << std::hex << dmaInternalSrc[dmaChannel]
                                      << " Dst=0x" << dmaInternalDst[dmaChannel]
                                      << " Cnt=0x" << cnt
                                      << " Ctrl=0x" << ctrl
                                      << " timing=" << std::dec << ((ctrl >> 12) & 3)
                                      << " repeat=" << ((ctrl >> 9) & 1)
                                      << " width=" << (((ctrl & DMAControl::TRANSFER_32BIT)!=0)?32:16)
                                      << (cpu?" PC=0x":"") << std::hex;
                            if (cpu) std::cout << cpu->GetRegister(15);
                            std::cout << std::dec << std::endl;
                        }
                        
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

            // Instrument IE/IME writes to confirm interrupt enablement timing
            if (offset == IORegs::IE || offset == IORegs::IE + 1 || offset == IORegs::IME || offset == IORegs::IME + 1) {
                static std::ofstream irqLog("/tmp/irq_regs.log", std::ios::app);
                static int irqLogCount = 0;
                if (irqLogCount < 400) {
                    irqLog << "[IRQ REG WRITE16 #" << irqLogCount << "] offset=0x" << std::hex << offset
                           << " val=0x" << value << " PC=0x" << cpu->GetRegister(15) << std::dec << "\n";
                    irqLogCount++;
                    if (irqLogCount % 50 == 0) irqLog << std::flush;
                }
            }
            
            // Log early display configuration writes (limited to keep noise down)
            static int dispcntLogs = 0;
            static int bgcntLogs = 0;
            static int bghofsLogs = 0;
            static int bgvofsLogs = 0;

            auto logReg = [&](const char* name) {
                std::cout << "[IO] " << name << " write16: 0x" << std::hex << value;
                if (cpu) std::cout << " PC=0x" << cpu->GetRegister(15);
                std::cout << std::dec << std::endl;
            };

            if (offset == IORegs::DISPCNT && dispcntLogs < 8) {
                dispcntLogs++;
                logReg("DISPCNT");
            }
            if ((offset >= 0x08 && offset <= 0x0E) && bgcntLogs < 16) {
                // BG0CNT, BG1CNT, BG2CNT, BG3CNT
                bgcntLogs++;
                std::ostringstream name;
                name << "BG" << ((offset - 0x08) / 2) << "CNT";
                logReg(name.str().c_str());
            }
            if ((offset == 0x10 || offset == 0x12 || offset == 0x14 || offset == 0x16) && bghofsLogs < 16) {
                // BGxHOFS
                bghofsLogs++;
                std::ostringstream name;
                name << "BG" << ((offset - 0x10) / 2) << "HOFS";
                logReg(name.str().c_str());
            }
            if ((offset == 0x11 || offset == 0x13 || offset == 0x15 || offset == 0x17) && bgvofsLogs < 16) {
                // BGxVOFS
                bgvofsLogs++;
                std::ostringstream name;
                name << "BG" << ((offset - 0x11) / 2) << "VOFS";
                logReg(name.str().c_str());
            }
            
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
            
            // SOUNDCNT_X - preserve status bits
            if (offset == IORegs::SOUNDCNT_X) {
                uint16_t currentVal = io_regs[offset] | (io_regs[offset+1] << 8);
                value = (value & 0x80) | (currentVal & 0x0F);
            }

            
            // Timer Control (GBATEK timers 0-3)
            if (offset >= IORegs::TM0CNT_L && offset <= IORegs::TM3CNT_H) {
                int timerIdx = (offset - IORegs::TM0CNT_L) / IORegs::TIMER_CHANNEL_SIZE;
                
                if ((offset % IORegs::TIMER_CHANNEL_SIZE) == 2) { // TMxCNT_H
                    uint16_t oldControl = io_regs[offset] | (io_regs[offset+1] << 8);
                    bool wasEnabled = oldControl & TimerControl::ENABLE;
                    bool nowEnabled = value & TimerControl::ENABLE;

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
        // Instrument IE/IME writes done via 32-bit access
        if ((address & 0xFF000000) == 0x04000000) {
            uint32_t offset = address & 0x3FF;
            if ((offset == IORegs::IE) || (offset == IORegs::IE + 1) ||
                (offset == IORegs::IME) || (offset == IORegs::IME + 1) ||
                (offset == IORegs::IE - 2) || (offset == IORegs::IME - 2)) {
                static std::ofstream irqLog("/tmp/irq_regs.log", std::ios::app);
                static int irqLog32 = 0;
                if (irqLog32 < 400) {
                    irqLog << "[IRQ REG WRITE32 #" << irqLog32 << "] offset=0x" << std::hex << offset
                           << " val=0x" << value << " PC=0x" << cpu->GetRegister(15)
                           << std::dec << "\n";
                    irqLog32++;
                    if (irqLog32 % 50 == 0) irqLog << std::flush;
                }
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

        // Instrument key IO register writes to diagnose display/IRQ state
        if ((address & 0xFF000000) == IORegs::BASE) {
            uint32_t offset = address & MemoryMap::IO_REG_MASK;
            switch (offset) {
                case IORegs::DISPCNT:
                    std::cout << "[IO] DISPCNT write32: 0x" << std::hex << value << std::dec << std::endl;
                    break;
                case IORegs::DISPSTAT:
                    std::cout << "[IO] DISPSTAT write32: 0x" << std::hex << value << std::dec << std::endl;
                    break;
                case IORegs::VCOUNT:
                    std::cout << "[IO] VCOUNT write32: 0x" << std::hex << value << std::dec << std::endl;
                    break;
                case IORegs::IE:
                    std::cout << "[IO] IE write32: 0x" << std::hex << value << std::dec << std::endl;
                    break;
                case IORegs::IF:
                    std::cout << "[IO] IF write32: 0x" << std::hex << value << std::dec << std::endl;
                    break;
                case IORegs::IME:
                    std::cout << "[IO] IME write32: 0x" << std::hex << value << std::dec << std::endl;
                    break;
                default:
                    break;
            }
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
            uint32_t baseOffset = IORegs::DMA0SAD + (i * IORegs::DMA_CHANNEL_SIZE);
            uint16_t control = io_regs[baseOffset + 10] | (io_regs[baseOffset + 11] << 8);
            
            if (control & DMAControl::ENABLE) {
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
        
        // DEBUG: Log first 10 DMA calls unconditionally
        static int dmaDebugCount = 0;
        if (dmaDebugCount < 10) {
            std::ofstream df("/tmp/gba_dma_debug.txt", std::ios::app);
            df << "[DMA#" << dmaSeq << " ch" << channel << " ENTER]" << std::endl;
            df.close();
            dmaDebugCount++;
        }
        
        uint32_t baseOffset = IORegs::DMA0SAD + (channel * IORegs::DMA_CHANNEL_SIZE);
        
        // CNT_H (Control) - 16 bit
        uint16_t control = io_regs[baseOffset+10] | (io_regs[baseOffset+11] << 8);
        
        // Decode timing first
        int timing = (control & DMAControl::START_TIMING_MASK) >> 12;
        
        // Only guard immediate timing (timing=0) DMAs from recursion
        // Sound DMAs (timing=3) must be allowed to happen for audio to work
        if (timing == 0 && inImmediateDMA) {
            return;
        }
        
        bool wasInImmediate = inImmediateDMA;
        if (timing == 0) {
            inImmediateDMA = true;
        }
        
        // CNT_L (Count) - 16 bit
        uint32_t count = io_regs[baseOffset+8] | (io_regs[baseOffset+9] << 8);
        bool repeat = (control >> 9) & 1;
        
        // Decode Control
        bool is32Bit = (control & DMAControl::TRANSFER_32BIT) != 0;
        int destCtrl = (control & DMAControl::DEST_ADDR_CONTROL_MASK) >> 5;
        int srcCtrl = (control & DMAControl::SRC_ADDR_CONTROL_MASK) >> 7;
        
        uint32_t dst = dmaInternalDst[channel];
        uint32_t src = dmaInternalSrc[channel];
        
        // DEBUG: Trace DMA targeting IWRAM
        if (verboseLogs && (dst & 0xFF000000) == 0x03000000) {
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
        
        // EEPROM instrumentation: log typical EEPROM-related DMA3 bursts
        if (channel == 3) {
            bool srcIsEEPROM = (currentSrc >= 0x0D000000 && currentSrc < 0x0E000000);
            bool dstIsEEPROM = (currentDst >= 0x0D000000 && currentDst < 0x0E000000);
            if (srcIsEEPROM || dstIsEEPROM) {
                std::cout << "[DMA3 START] Src=0x" << std::hex << currentSrc << " Dst=0x" << currentDst
                          << " Count=0x" << count << " Ctrl=0x" << control
                          << " timing=" << std::dec << ((control >> 12) & 3)
                          << " repeat=" << ((control >> 9) & 1)
                          << " width=" << (((control & DMAControl::TRANSFER_32BIT) != 0) ? 32 : 16)
                          << " PC=0x" << std::hex << (cpu ? cpu->GetRegister(15) : 0) << std::dec << std::endl;
            }
            if (srcIsEEPROM && count >= 68 && (control & DMAControl::TRANSFER_32BIT) == 0) {
                // eepromAddress is already a block number (not a byte offset)
                // std::cout << " First16=";
                // for (int i = 0; i < 16 && (base + i) < (int)eepromData.size(); ++i) {
                //     std::cout << std::hex << (int)eepromData[base + i];
                // }
                // std::cout << std::dec << std::endl;
            }
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
            io_regs[baseOffset+10] &= ~(DMAControl::ENABLE & 0xFF);
            io_regs[baseOffset+11] &= ~(DMAControl::ENABLE >> 8);
            return;
        }
        
        int step = is32Bit ? 4 : 2;
        int totalCycles = 2; // DMA Setup Overhead (approx)
        
        // EEPROM DMA Read Support
        // Games use DMA to clock EEPROM reads bit-by-bit via the serial interface.
        // OPTIMIZATION: For EEPROM reads, instantly complete the state machine to avoid
        // slow bit-by-bit protocol (which would require 64+ DMA transfers per block)
        bool srcIsEEPROM = (currentSrc >= 0x0D000000 && currentSrc < 0x0E000000);
        bool dstIsEEPROM = (currentDst >= 0x0D000000 && currentDst < 0x0E000000);
        
        // Debug: Log EEPROM check
        // if (srcIsEEPROM || dstIsEEPROM) {
        //     std::cout << "[EEPROM DMA CHECK] src=0x" << std::hex << currentSrc 
        //               << " dst=0x" << currentDst 
        //               << " count=" << std::dec << count 
        //               << " srcIsEEPROM=" << srcIsEEPROM 
        //               << " dstIsEEPROM=" << dstIsEEPROM 
        //               << " count>=68=" << (count >= 68) << std::endl;
        // }
        
        // DEBUG: Always log when we check fast-path condition
        if (srcIsEEPROM || dstIsEEPROM) {
            std::ofstream debugFile("/tmp/eeprom_debug.txt", std::ios::app);
            debugFile << "[DMA CHECK] srcEEP=" << srcIsEEPROM << " dstEEP=" << dstIsEEPROM << " count=" << count << " >= 68? " << (count >= 68) << std::endl;
            debugFile.close();
        }
        
        // Fast-path for EEPROM reads - only if buffer already prepared AND validated
        bool startingAtDataPhase = (eepromState == EEPROMState::ReadData);
        bool inReadSequence = (eepromState == EEPROMState::ReadDummy || eepromState == EEPROMState::ReadData);
        // CRITICAL: Only fast-path if buffer is valid for THIS transaction (set after address+stop bit)
        bool canFastPath = srcIsEEPROM && inReadSequence && eepromBufferValid && count >= 4;

        // Fast-path validation (silent)

        if (canFastPath) {
            if (verboseLogs) {
                std::cout << "[EEPROM FAST-PATH] Activating for count=" << count << " src=0x" << std::hex << currentSrc << std::dec << std::endl;
            }
            
            // Save initial destination for logging
            uint32_t initialDst = currentDst;
            
            // Force completion of read sequence
            if (!startingAtDataPhase) {
                eepromState = EEPROMState::ReadDummy;
                eepromBitCounter = 0;
            }
            
            // Return all bits - each 16-bit word contains a single bit (0 or 1)
            // Game accumulates these into bytes/words in its own code
            uint64_t debugBits = 0;
            for (uint32_t i = 0; i < count; ++i) {
                uint16_t bit;
                if (eepromState == EEPROMState::ReadDummy) {
                    bit = EEPROMConsts::BUSY_LOW;
                    eepromBitCounter++;
                    if (eepromBitCounter >= EEPROMConsts::DUMMY_BITS) {
                        eepromState = EEPROMState::ReadData;
                        eepromBitCounter = 0;
                    }
                } else { // ReadData
                    int bitIndex = (EEPROMConsts::DATA_BITS - 1) - eepromBitCounter;
                    bit = (eepromBuffer >> bitIndex) & 1;
                    if (i >= 4 && i < 68) { // After dummy bits, during data phase
                        debugBits = (debugBits << 1) | bit;
                    }
                    eepromBitCounter++;
                    if (eepromBitCounter >= EEPROMConsts::DATA_BITS) {
                        eepromState = EEPROMState::Idle;
                        eepromBitCounter = 0;
                        eepromBufferValid = false; // Invalidate buffer after read completes
                    }
                }
                
                // Each DMA transfer receives a single bit value (0 or 1) as a 16-bit word
                // The game's code will shift and accumulate these bits
                // Direct write to target memory (WRAM/EWRAM) without invoking full Write16 cost
                uint8_t dstRegion = currentDst >> 24;
                if (dstRegion == 0x02) {
                    uint32_t off = currentDst & MemoryMap::WRAM_BOARD_MASK;
                    if (off + 1 < wram_board.size()) {
                        wram_board[off] = bit & 0xFF;
                        wram_board[off + 1] = (bit >> 8) & 0xFF;
                    }
                } else if (dstRegion == 0x03) {
                    uint32_t off = currentDst & MemoryMap::WRAM_CHIP_MASK;
                    if (off + 1 < wram_chip.size()) {
                        wram_chip[off] = bit & 0xFF;
                        wram_chip[off + 1] = (bit >> 8) & 0xFF;
                    }
                } else {
                    Write16(currentDst, bit);
                }
                
                // Update destination address
                if (destCtrl == 0 || destCtrl == 3) {
                    currentDst += 2;
                } else if (destCtrl == 1) {
                    currentDst -= 2;
                }
            }
            totalCycles += count * 2;
            
            // DEBUG: Log what bits were transferred
            if (verboseLogs && eepromAddress == 2) { // Only log block 2 (the header block)
                std::cout << "[EEPROM FAST-PATH] Block 2 read: debugBits=0x" << std::hex << debugBits << std::dec << std::endl;
            }
            

            
            // ALWAYS log fast-path completion for debugging
            std::ofstream debugFile("/tmp/eeprom_debug.txt", std::ios::app);
            debugFile << "[FAST-PATH] Block=" << eepromAddress << " Count=" << count << " Buffer=0x" << std::hex << eepromBuffer << std::dec << std::endl;
            debugFile << "  Dst=0x" << std::hex << initialDst << " First 16 words:";
            uint32_t dumpOffset = initialDst - 0x03000000;
            for (int i = 0; i < 16 && i < (int)count && dumpOffset + i*2 + 1 < wram_chip.size(); ++i) {
                uint16_t word = wram_chip[dumpOffset + i*2] | (wram_chip[dumpOffset + i*2 + 1] << 8);
                if (i % 8 == 0) debugFile << std::endl << "    ";
                debugFile << std::setw(4) << std::setfill('0') << word << " ";
            }
            debugFile << std::dec << std::endl;
            debugFile.close();
            
            if (verboseLogs) {
                std::cout << "[EEPROM FAST-PATH] Complete - returned to Idle state" << std::endl;
            }
        }
        // Fast-path for EEPROM writes
        else if (dstIsEEPROM && count > 1) {
            // Process all writes instantly
            for (uint32_t i = 0; i < count; ++i) {
                uint16_t val = Read16(currentSrc);
                WriteEEPROM(val);
                if (srcCtrl == 0) currentSrc += 2;
                else if (srcCtrl == 1) currentSrc -= 2;
            }
            totalCycles += count * 2;
        }
        // Normal DMA path for non-EEPROM transfers
        else {
            // Handle EEPROM writes and reads separately
            if (srcIsEEPROM) {
                // Reading from EEPROM - call ReadEEPROM for each word
                for (uint32_t i = 0; i < count; ++i) {
                    uint16_t val = ReadEEPROM();
                    
                    // Log if writing EEPROM data to critical regions
                    if (currentDst >= 0x05000000 && currentDst < 0x05000400) {
                        std::cerr << "[EEPROM->PALETTE] Writing 0x" << std::hex << val 
                                  << " to 0x" << currentDst << std::dec << std::endl;
                    } else if (currentDst >= 0x06000000 && currentDst < 0x06018000) {
                        std::cerr << "[EEPROM->VRAM] Writing 0x" << std::hex << val 
                                  << " to 0x" << currentDst << std::dec << std::endl;
                    } else if (currentDst >= 0x07000000 && currentDst < 0x07000400) {
                        std::cerr << "[EEPROM->OAM] Writing 0x" << std::hex << val 
                                  << " to 0x" << currentDst << std::dec << std::endl;
                    }
                    
                    Write16(currentDst, val);
                    totalCycles += 2;
                    if (destCtrl == 0 || destCtrl == 3) currentDst += 2;
                    else if (destCtrl == 1) currentDst -= 2;
                }
            } else if (dstIsEEPROM) {
                // Writing to EEPROM - call WriteEEPROM for each word
                for (uint32_t i = 0; i < count; ++i) {
                    uint16_t val = Read16(currentSrc);
                    WriteEEPROM(val);
                    totalCycles += 2;
                    if (srcCtrl == 0) currentSrc += 2;
                    else if (srcCtrl == 1) currentSrc -= 2;
                }
            } else {
                // Normal memory-to-memory DMA
                for (uint32_t i = 0; i < count; ++i) {
                    if (is32Bit) {
                        uint32_t val = Read32(currentSrc);
                        Write32(currentDst, val);
                        totalCycles += 4;
                    } else {
                        uint16_t val = Read16(currentSrc);
                        Write16(currentDst, val);
                        totalCycles += 2;
                    }
                }
                
                // Update Source Address (after loop)
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

        // Per GBA spec: Immediate timing always clears Enable bit after first transfer,
        // regardless of Repeat bit. Repeat only applies to VBlank/HBlank/FIFO triggered DMAs.
        if (timing == 0 || !repeat) {
            // Immediate: always clear
            // Other timing: only clear if not repeating
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
            uint32_t baseOffset = IORegs::TM0CNT_L + (i * IORegs::TIMER_CHANNEL_SIZE);
            uint16_t control = io_regs[baseOffset + 2] | (io_regs[baseOffset + 3] << 8);
            
            if (control & TimerControl::ENABLE) { // Timer Enabled
                
                bool overflow = false;
                int increments = 0;
                
                if (control & TimerControl::COUNT_UP) { // Count-Up (Cascade)
                    if (previousOverflow) {
                        increments = 1;
                    }
                } else {
                    // Prescaler
                    int prescaler = control & TimerControl::PRESCALER_MASK;
                    int threshold = 0;
                    switch (prescaler) {
                        case 0: threshold = 1; break;      // F/1
                        case 1: threshold = 64; break;     // F/64
                        case 2: threshold = 256; break;    // F/256
                        case 3: threshold = 1024; break;   // F/1024
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
                        if (control & TimerControl::IRQ_ENABLE) {
                            uint16_t if_reg = io_regs[IORegs::IF] | (io_regs[IORegs::IF + 1] << 8);
                            if_reg |= (InterruptFlags::TIMER0 << i);
                            io_regs[IORegs::IF] = if_reg & 0xFF;
                            io_regs[IORegs::IF + 1] = (if_reg >> 8) & 0xFF;
                        }
                        
                        // Sound DMA trigger (Timer 0 and Timer 1 only)
                        if (i == 0 || i == 1) {
                            uint16_t soundcntH = io_regs[IORegs::SOUNDCNT_H] | (io_regs[IORegs::SOUNDCNT_H + 1] << 8);
                            int fifoATimer = (soundcntH >> 10) & 1;
                            int fifoBTimer = (soundcntH >> 14) & 1;
                            
                            for (int dma = 1; dma <= 2; dma++) {
                                uint32_t dmaBase = IORegs::DMA0SAD + (dma * IORegs::DMA_CHANNEL_SIZE);
                                uint16_t dmaControl = io_regs[dmaBase+10] | (io_regs[dmaBase+11] << 8);
                                
                                if (dmaControl & DMAControl::ENABLE) {
                                    int dmaTiming = (dmaControl & DMAControl::START_TIMING_MASK) >> 12;
                                    if (dmaTiming == 3) { // Special timing (sound FIFO)
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

    void GBAMemory::AdvanceCycles(int cycles) {
        UpdateTimers(cycles);
        if (ppu) ppu->Update(cycles);
        if (apu) apu->Update(cycles);
    }

    uint16_t GBAMemory::ReadEEPROM() {
        uint16_t ret = EEPROMConsts::READY_HIGH; // Default to Ready (high)
        
        if (eepromWriteDelay > 0) {
            static int busyReads = 0;
            if (busyReads < 10) {
                std::cerr << "[EEPROM] Still busy, delay=" << eepromWriteDelay << std::endl;
                busyReads++;
            }
            return EEPROMConsts::BUSY_LOW; // Busy
        }

        if (verboseLogs) {
            static int readCount = 0;
            if (readCount < 50) {
                readCount++;
                if (readCount % 10 == 0) {
                    std::cout << "[EEPROM READ] state=" << (int)eepromState << " bitCounter=" << eepromBitCounter 
                              << " returning=" << ret << std::endl;
                }
            }
        }
        
            if (eepromState == EEPROMState::ReadDummy) {
                ret = EEPROMConsts::BUSY_LOW;
                eepromBitCounter++;
                if (eepromBitCounter >= EEPROMConsts::DUMMY_BITS) { // Standard 4 dummy bits
                    eepromState = EEPROMState::ReadData;
                    eepromBitCounter = 0;
                }
            } else if (eepromState == EEPROMState::ReadData) {
                // Per GBATEK: "data (conventionally MSB first)"
                int bitIndex = (EEPROMConsts::DATA_BITS - 1) - eepromBitCounter;
                ret = (eepromBuffer >> bitIndex) & 1;
                
                eepromBitCounter++;
                if (eepromBitCounter >= EEPROMConsts::DATA_BITS) {
                    eepromState = EEPROMState::Idle;
                    eepromBitCounter = 0;
                }
            } else {
                // Active but not outputting data (e.g. receiving address) or Idle
                // The GBA data bus is pulled up (High-Z) when not driven by the EEPROM
                ret = EEPROMConsts::READY_HIGH;
            }        return ret;
    }

    void GBAMemory::WriteEEPROM(uint16_t value) {
        if (eepromWriteDelay > 0) {
            return;
        }

        uint8_t bit = value & EEPROMConsts::BIT_MASK;
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
                
                if (eepromBitCounter >= (eepromIs64Kbit ? EEPROMConsts::ADDR_BITS_64K : EEPROMConsts::ADDR_BITS_4K)) {
                    // GBATEK: upper 4 address bits of 64Kbit variant are ignored (only lower 10 bits matter)
                    // and 4Kbit uses lower 6 bits. Mask explicitly after the full address has been received.
                    eepromAddress &= eepromIs64Kbit ? 0x3FF : 0x3F;
                    
                    // Prepare data buffer immediately
                    uint32_t offset = eepromAddress * EEPROMConsts::BYTES_PER_BLOCK;
                    eepromBuffer = 0;
                    
                    // DEBUG: Check eepromData size
                    static bool sizeLogged = false;
                    if (!sizeLogged) {
                        std::cerr << "[EEPROM DEBUG] eepromData.size()=" << eepromData.size() << std::endl;
                        sizeLogged = true;
                    }
                    
                    if (offset + (EEPROMConsts::BYTES_PER_BLOCK - 1) < eepromData.size()) {
                        // Per GBATEK: "64 bits data (conventionally MSB first)"
                        // Build big-endian buffer (MSB = byte 0)
                        
                        // DEBUG: Log first byte with explicit index
                        static int firstByteLog = 0;
                        if (firstByteLog < 5) {
                            std::cerr << "[EEPROM BYTE ACCESS] offset=" << offset 
                                      << " eepromData[" << offset << "]=" << std::hex << (int)eepromData[offset]
                                      << " eepromData[" << (offset+1) << "]=" << (int)eepromData[offset+1] << std::dec << std::endl;
                            firstByteLog++;
                        }
                        
                        for (int i = 0; i < (int)EEPROMConsts::BYTES_PER_BLOCK; ++i) {
                            eepromBuffer |= ((uint64_t)eepromData[offset + i] << (56 - i * 8));
                        }
                        
                        // Log read operation with offset details and actual bytes
                        static int readLogCount = 0;
                        if (readLogCount < 20) {
                            std::cerr << "[EEPROM READ PREP] Block=" << eepromAddress 
                                      << " Offset=0x" << std::hex << offset << std::dec;
                            std::cerr << " Bytes[0x" << std::hex << offset << "]=";
                            for (int i = 0; i < (int)EEPROMConsts::BYTES_PER_BLOCK && i < 8; ++i) {
                                std::cerr << std::setw(2) << std::setfill('0') << (int)eepromData[offset + i];
                            }
                            std::cerr << " Data=0x" << std::setw(16) << eepromBuffer 
                                      << std::dec << std::endl;
                            readLogCount++;
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
                
                // CRITICAL: Mark that buffer is NOW valid for this transaction
                eepromBufferValid = true;
                
                // Always proceed to ReadDummy
                eepromState = EEPROMState::ReadDummy;
                eepromBitCounter = 0;
                break;
                
            case EEPROMState::WriteAddress:
                // Per GBATEK: "n bits eeprom address (MSB first, 6 or 14 bits)"
                eepromAddress = (eepromAddress << 1) | bit;
                eepromBitCounter++;
                if (eepromBitCounter >= (eepromIs64Kbit ? EEPROMConsts::ADDR_BITS_64K : EEPROMConsts::ADDR_BITS_4K)) {
                    // GBATEK: upper 4 address bits of 64Kbit variant are ignored (only lower 10 bits matter)
                    // and 4Kbit uses lower 6 bits. Mask explicitly after the full address has been received.
                    eepromAddress &= eepromIs64Kbit ? 0x3FF : 0x3F;
                    
                    eepromState = EEPROMState::WriteData;
                    eepromBitCounter = 0;
                    eepromBuffer = 0;
                }
                break;
                
            case EEPROMState::WriteData:
                eepromBuffer = (eepromBuffer << 1) | bit;
                eepromBitCounter++;
                if (eepromBitCounter >= EEPROMConsts::DATA_BITS) {
                    eepromState = EEPROMState::WriteTermination;
                    eepromBitCounter = 0;
                }
                break;
                
            case EEPROMState::WriteTermination:
                // Expecting a '0' bit to terminate the write command
                
                if (bit == 0) {
                    // Valid Stop Bit - Commit Write
                    uint32_t offset = eepromAddress * EEPROMConsts::BYTES_PER_BLOCK;
                    
                    // Check if game is writing back what it read
                    bool isMismatch = false;
                    if (offset + (EEPROMConsts::BYTES_PER_BLOCK - 1) < eepromData.size()) {
                        uint64_t existingData = 0;
                        for (int i = 0; i < (int)EEPROMConsts::BYTES_PER_BLOCK; ++i) {
                            existingData |= ((uint64_t)eepromData[offset + i] << (56 - i * 8));
                        }
                        if (existingData != eepromBuffer && eepromAddress == 2) {
                            isMismatch = true;
                            std::cerr << "[EEPROM MISMATCH!] Block 2: read=0x" << std::hex << existingData 
                                      << " but writing=0x" << eepromBuffer << std::dec << std::endl;
                        }
                    }
                    
                    if (verboseLogs && (eepromAddress == 2 || isMismatch)) {
                        std::cerr << "[EEPROM WRITE] Block=" << eepromAddress << " Data=0x" 
                                  << std::hex << std::setfill('0') << std::setw(16) << eepromBuffer << std::dec << std::endl;
                    }
                    
                    if (offset + (EEPROMConsts::BYTES_PER_BLOCK - 1) < eepromData.size()) {
                        for (int i = 0; i < (int)EEPROMConsts::BYTES_PER_BLOCK; ++i) {
                            uint8_t byteVal = (eepromBuffer >> (56 - i * 8)) & 0xFF;
                            eepromData[offset + i] = byteVal;
                        }
                    }
                    FlushSave();
                    // Stable timing that prevents crashes
                    eepromWriteDelay = 1000;
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
