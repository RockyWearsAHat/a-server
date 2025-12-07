#include <iostream>
#include <iomanip>
#include <map>
#include <vector>
#include <algorithm>
#include <emulator/gba/GBA.h>

int main(int argc, char** argv) {
    AIO::Emulator::GBA::GBA gba;
    std::string romPath = "SMA2.gba";
    if (argc > 1) {
        romPath = argv[1];
    }
    
    std::cout << "Loading ROM: " << romPath << std::endl;

    if (!gba.LoadROM(romPath)) {
        std::cerr << "Failed to load ROM" << std::endl;
        return 1;
    }

    gba.Reset();
    
    long long totalCycles = 0;

    std::cout << "Starting Emulation..." << std::endl;
    std::cout << "Initial DISPCNT: 0x" << std::hex << gba.ReadMem16(0x04000000) << std::dec << std::endl;
    
    std::map<uint32_t, int> pcHistogram;
    
    for (int frame = 0; frame < 7200; ++frame) {
        int frameCycles = 0;
        
        // Simulate button presses for testing
        if (frame >= 100 && frame < 110) {
            gba.UpdateInput(0x03FF & ~0x0008); // Press Start
        } else if (frame >= 200 && frame < 210) {
            gba.UpdateInput(0x03FF & ~0x0001); // Press A
        } else if (frame >= 300 && frame < 310) {
            gba.UpdateInput(0x03FF & ~0x0008); // Press Start again
        } else {
            gba.UpdateInput(0x03FF); // All released
        }
        
        while (frameCycles < 280000) {
            int step = gba.Step();
            frameCycles += step;
            totalCycles += step;
            
            uint32_t pc = gba.GetPC();
            pcHistogram[pc & 0xFFFFF000]++;
            
            // Check for infinite loops
            uint32_t cpsr = gba.GetCPSR();
            static int irqDisabledCount = 0;
            static bool warnPrinted = false;
            
            // Track R11 corruption
            static uint32_t lastR11 = 0;
            static uint32_t lastPC = 0;
            uint32_t r11 = gba.GetRegister(11);
            
            // Track last 10 PCs before crash
            static uint32_t pcHistory[10] = {0};
            static int pcHistIdx = 0;
            static bool enteredIWRAM3003 = false;
            pcHistory[pcHistIdx] = pc;
            pcHistIdx = (pcHistIdx + 1) % 10;
            
            // Track entry into 0x3003xxx IWRAM code
            if ((pc & 0xFFFFF000) == 0x03003000 && !enteredIWRAM3003) {
                std::cout << "[ENTERED IWRAM 0x3003xxx] PC=0x" << std::hex << pc 
                          << " from last PC=0x" << pcHistory[(pcHistIdx + 8) % 10] 
                          << std::dec << std::endl;
                          
                // Dump sound structure and DMA state
                std::cout << "[SOUND STRUCT DUMP at entry]:" << std::endl;
                for (int i = 0; i < 16; i++) {
                    uint32_t val = gba.ReadMem32(0x03004004 + i * 4);
                    std::cout << "  0x" << std::hex << (0x03004004 + i * 4) << ": 0x" << val << std::dec << std::endl;
                }
                
                // Dump 0x3001500 area
                std::cout << "[MIXBUF 0x3001500 at entry]:" << std::endl;
                for (int i = 0; i < 4; i++) {
                    uint32_t val = gba.ReadMem32(0x03001500 + i * 4);
                    std::cout << "  0x" << std::hex << (0x03001500 + i * 4) << ": 0x" << val << std::dec << std::endl;
                }
                
                enteredIWRAM3003 = true;
            }
            
            // Trace when we're about to execute the corrupting instruction
            if (pc == 0x30032fc) {
                std::cout << "[CRASH PC HISTORY]:" << std::endl;
                for (int i = 0; i < 10; i++) {
                    int idx = (pcHistIdx + i) % 10;
                    std::cout << "  PC[" << i << "] = 0x" << std::hex << pcHistory[idx] << std::dec << std::endl;
                }
                
                uint32_t r0 = gba.GetRegister(0);
                uint32_t addr = r11 + (r0 << 2);
                uint32_t memVal = gba.ReadMem32(addr);
                uint16_t ie = gba.ReadMem16(0x04000200);
                uint16_t if_reg = gba.ReadMem16(0x04000202);
                bool thumbMode = (cpsr & 0x20) != 0;
                
                std::cout << "[TRACE 0x30032fc] R11=0x" << std::hex << r11 
                          << " R0=0x" << r0 
                          << " LoadAddr=0x" << addr 
                          << " MemVal=0x" << memVal 
                          << " IE=0x" << ie
                          << " IF=0x" << if_reg
                          << " CPSR=0x" << cpsr
                          << " Mode=" << (thumbMode ? "THUMB" : "ARM")
                          << std::dec << std::endl;
            }
            
            if (r11 >= 0x10000000 && lastR11 < 0x10000000 && r11 != lastR11) {
                std::cout << "[R11 CORRUPT] at PC=0x" << std::hex << pc << std::endl;
                std::cout << "  R11 changed from 0x" << lastR11 << " to 0x" << r11 << std::endl;
                std::cout << "  Previous PC: 0x" << lastPC << std::endl;
                std::cout << "  CPSR=0x" << cpsr << std::dec << std::endl;
            }
            lastR11 = r11;
            lastPC = pc;
            
            if (cpsr & 0x80) {
                irqDisabledCount++;
                if (irqDisabledCount == 1000000 && !warnPrinted) {
                    std::cout << "[WARN] IRQs disabled for 1M cycles at PC=0x" << std::hex << pc 
                              << " CPSR=0x" << cpsr << std::dec << std::endl;
                    warnPrinted = true;
                }
            } else {
                irqDisabledCount = 0;
            }
        }
        
        if ((frame + 1) % 1000 == 0) {
            std::cout << "Frame " << (frame + 1) << " completed. Total cycles: " << totalCycles << std::endl;
        }
        
        // Dump PPU state at key frames
        if (frame == 60 || frame == 120 || frame == 180) {
            uint16_t dispcnt = gba.ReadMem16(0x04000000);
            std::cout << "\n=== PPU State @ Frame " << frame << " ===" << std::endl;
            std::cout << "DISPCNT: 0x" << std::hex << dispcnt 
                      << " Mode=" << (dispcnt & 7)
                      << " BG0=" << ((dispcnt >> 8) & 1)
                      << " BG1=" << ((dispcnt >> 9) & 1)
                      << " BG2=" << ((dispcnt >> 10) & 1)
                      << " BG3=" << ((dispcnt >> 11) & 1)
                      << " OBJ=" << ((dispcnt >> 12) & 1)
                      << " WIN0=" << ((dispcnt >> 13) & 1)
                      << " WIN1=" << ((dispcnt >> 14) & 1)
                      << " OBJWIN=" << ((dispcnt >> 15) & 1) << std::dec << std::endl;
            
            for (int bg = 0; bg < 4; bg++) {
                uint16_t bgcnt = gba.ReadMem16(0x04000008 + bg * 2);
                uint16_t bghofs = gba.ReadMem16(0x04000010 + bg * 4);
                uint16_t bgvofs = gba.ReadMem16(0x04000012 + bg * 4);
                int priority = bgcnt & 0x3;
                int charBase = (bgcnt >> 2) & 0x3;
                int screenBase = (bgcnt >> 8) & 0x1F;
                bool is8bpp = (bgcnt >> 7) & 1;
                int screenSize = (bgcnt >> 14) & 0x3;
                
                std::cout << "BG" << bg << "CNT: 0x" << std::hex << bgcnt 
                          << " Priority=" << priority
                          << " CharBase=" << charBase 
                          << " ScreenBase=" << screenBase
                          << " 8bpp=" << is8bpp
                          << " Size=" << screenSize
                          << " HOFS=" << bghofs
                          << " VOFS=" << bgvofs << std::dec << std::endl;
            }
            
            // Count non-transparent pixels per BG area
            uint32_t vramBase = 0x06000000;
            for (int bg = 0; bg < 4; bg++) {
                uint16_t bgcnt = gba.ReadMem16(0x04000008 + bg * 2);
                int charBase = (bgcnt >> 2) & 0x3;
                int screenBase = (bgcnt >> 8) & 0x1F;
                
                uint32_t tileBase = vramBase + charBase * 16384;
                uint32_t mapBase = vramBase + screenBase * 2048;
                
                int nonZeroTiles = 0;
                uint16_t sampleTiles[4] = {0, 0, 0, 0};
                int sampleIdx = 0;
                for (int i = 0; i < 2048; i++) {
                    uint16_t tileEntry = gba.ReadMem16(mapBase + i * 2);
                    if (tileEntry != 0) {
                        nonZeroTiles++;
                        // Capture first few non-zero tile entries
                        if (sampleIdx < 4) sampleTiles[sampleIdx++] = tileEntry;
                    }
                }
                
                int nonZeroTileData = 0;
                for (int i = 0; i < 16384; i++) {
                    if (gba.ReadMem(tileBase + i) != 0) nonZeroTileData++;
                }
                
                std::cout << "BG" << bg << " MapBase=0x" << std::hex << mapBase 
                          << " TileBase=0x" << tileBase 
                          << " nonZeroTiles=" << std::dec << nonZeroTiles
                          << " nonZeroTileData=" << nonZeroTileData;
                if (nonZeroTiles > 0) {
                    std::cout << " sampleEntries=[0x" << std::hex 
                              << sampleTiles[0] << ",0x" << sampleTiles[1] 
                              << ",0x" << sampleTiles[2] << ",0x" << sampleTiles[3] << "]";
                    // Check the actual tile data for the first non-zero tile
                    uint16_t tileIdx = sampleTiles[0] & 0x3FF;
                    uint32_t tileDataAddr = tileBase + (tileIdx * 32);
                    uint32_t word0 = gba.ReadMem32(tileDataAddr);
                    uint32_t word1 = gba.ReadMem32(tileDataAddr + 4);
                    std::cout << " tile" << std::dec << tileIdx << "@0x" << std::hex << tileDataAddr 
                              << "=" << word0 << "," << word1;
                }
                std::cout << std::dec << std::endl;
            }
        }
    }
    
    std::cout << "\n=== PC Region Histogram (Top 20) ===" << std::endl;
    std::vector<std::pair<uint32_t, int>> sortedPCs(pcHistogram.begin(), pcHistogram.end());
    std::sort(sortedPCs.begin(), sortedPCs.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    for (int i = 0; i < std::min(20, (int)sortedPCs.size()); ++i) {
        std::cout << "  0x" << std::hex << std::setw(8) << std::setfill('0') << sortedPCs[i].first 
                  << ": " << std::dec << sortedPCs[i].second << " hits" << std::endl;
    }

    const auto& framebuffer = gba.GetPPU().GetFramebuffer();
    std::map<uint32_t, int> colorCounts;
    for (size_t i = 0; i < framebuffer.size(); ++i) {
        colorCounts[framebuffer[i]]++;
    }
    
    std::cout << "\n=== Framebuffer Analysis ===" << std::endl;
    std::cout << "Total pixels: " << framebuffer.size() << std::endl;
    std::cout << "Unique colors: " << colorCounts.size() << std::endl;
    
    std::vector<std::pair<uint32_t, int>> sortedColors(colorCounts.begin(), colorCounts.end());
    std::sort(sortedColors.begin(), sortedColors.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    std::cout << "Top colors:" << std::endl;
    for (int i = 0; i < std::min(10, (int)sortedColors.size()); ++i) {
        uint32_t color = sortedColors[i].first;
        std::cout << "  0x" << std::hex << std::setw(8) << std::setfill('0') << color 
                  << ": " << std::dec << sortedColors[i].second << " pixels" << std::endl;
    }

    uint16_t dispcnt = gba.ReadMem16(0x04000000);
    std::cout << "\n=== PPU Registers ===" << std::endl;
    std::cout << "DISPCNT: 0x" << std::hex << dispcnt << std::dec 
              << " (Mode " << (dispcnt & 7) 
              << ", BG0=" << ((dispcnt >> 8) & 1)
              << ", BG1=" << ((dispcnt >> 9) & 1)
              << ", BG2=" << ((dispcnt >> 10) & 1)
              << ", BG3=" << ((dispcnt >> 11) & 1)
              << ", OBJ=" << ((dispcnt >> 12) & 1)
              << ")" << std::endl;

    int nonZeroVram = 0;
    for (uint32_t addr = 0x06000000; addr < 0x06018000; addr++) {
        if (gba.ReadMem(addr) != 0) nonZeroVram++;
    }
    std::cout << "Non-zero bytes in VRAM: " << nonZeroVram << std::endl;

    std::cout << "\nTest completed successfully!" << std::endl;
    return 0;
}
