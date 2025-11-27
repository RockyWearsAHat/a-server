#include "emulator/gba/PPU.h"
#include "emulator/gba/GBAMemory.h"
#include <algorithm>
#include <iostream>

namespace AIO::Emulator::GBA {

    PPU::PPU(GBAMemory& mem) : memory(mem), cycleCounter(0), scanline(0), frameCount(0) {
        // Initialize framebuffer with black
        framebuffer.resize(SCREEN_WIDTH * SCREEN_HEIGHT, 0xFF000000); 
    }

    PPU::~PPU() = default;

    void PPU::Update(int cycles) {
        // GBA PPU Timing (Simplified for now)
        // 4 cycles per pixel (roughly)
        // 240 pixels + 68 HBlank = 308 pixels per line
        // 308 * 4 = 1232 cycles per line
        // 160 lines + 68 VBlank = 228 lines total
        
        int oldCycles = cycleCounter;
        cycleCounter += cycles;

        // HBlank Start (approx 960 cycles)
        if (oldCycles < 960 && cycleCounter >= 960) {
            // Trigger HBlank DMA (Timing 2)
            // Only if we are within visible lines (0-159)? 
            // Docs say HBlank DMA is triggered in HBlank of lines 0-159.
            if (scanline < 160) {
                memory.CheckDMA(2);
                
                // Also Trigger HBlank IRQ if enabled in DISPSTAT
                uint16_t dispstat = memory.Read16(0x04000004);
                if (dispstat & 0x10) { // HBlank IRQ Enable (Bit 4)
                    uint16_t if_reg = memory.Read16(0x04000202);
                    if_reg |= 2; // HBlank IRQ bit (Bit 1)
                    memory.Write16(0x04000202, if_reg);
                    std::cout << "HBlank IRQ Requested" << std::endl;
                }
                
                // Update DISPSTAT HBlank Flag (Bit 1)
                dispstat |= 2;
                memory.WriteIORegisterInternal(0x04, dispstat);
            }
        }

        if (cycleCounter >= 1232) {
            cycleCounter -= 1232;
            
            // End of HBlank (Start of next line)
            // Clear DISPSTAT HBlank Flag
            uint16_t dispstat = memory.Read16(0x04000004);
            dispstat &= ~2;
            memory.WriteIORegisterInternal(0x04, dispstat);

            // Update VCOUNT (0x04000006)
            // Note: VCOUNT is read-only for software, but we need to update it so software can read it
            // However, GBAMemory::io_regs is just a vector. We should probably write to it directly or via a friend class.
            // For now, let's assume we can write to memory.
            // But wait, GBAMemory::Write16 handles writes FROM CPU.
            // We need a way to update the IO registers internally.
            // Let's just use Write16 for now, assuming it updates the vector.
            // Actually, GBAMemory::Write16 is TODO.
            // Let's implement a backdoor or just use Write16 once implemented.
            // For now, I'll just implement DrawScanline and assume VCOUNT is handled elsewhere or I'll add a method to GBAMemory.
            
            // Render Scanline (if visible)
            if (scanline < 160) {
                DrawScanline();
            }

            scanline++;
            if (scanline == 0 || scanline == 100 || scanline == 160) {
                 std::cout << "PPU: Scanline " << scanline << std::endl;
            }

            if (scanline >= 228) {
                scanline = 0;
                frameCount++;
                if (frameCount % 1 == 0) {
                    std::cout << "PPU: Frame " << std::dec << frameCount << std::endl;
                }
            }
            
            // Update VCOUNT in memory (0x04000006)
            // We need to cast away constness or add a method to GBAMemory to set IO regs internally
            // For now, let's just implement the rendering logic.
            memory.WriteIORegisterInternal(0x06, scanline);

            // Update DISPSTAT (0x04000004) VBlank flag (Bit 0)
            dispstat = memory.Read16(0x04000004); // Re-read in case it changed
            bool wasVBlank = dispstat & 1;
            bool isVBlank = (scanline >= 160 && scanline <= 227);

            if (isVBlank) {
                dispstat |= 1; // Set VBlank
                
                // Trigger VBlank IRQ on rising edge (start of VBlank)
                if (!wasVBlank) {
                    std::cout << "PPU: VBlank Start. DISPSTAT=0x" << std::hex << dispstat << std::endl;
                    // Trigger VBlank DMA (Timing 1)
                    memory.CheckDMA(1);

                    // Check VBlank IRQ Enable (Bit 3)
                    // HACK: Force VBlank IRQ for SMA2 even if DISPSTAT bit 3 is 0
                    if ((dispstat & 0x8) || true) {
                        // Request Interrupt: Set Bit 0 of IF (0x04000202)
                        uint16_t if_reg = memory.Read16(0x04000202);
                        if_reg |= 1; // VBlank IRQ bit
                        memory.WriteIORegisterInternal(0x202, if_reg);
                        std::cout << "VBlank IRQ Requested. IE=" << std::hex << memory.Read16(0x04000200) << " IF=" << if_reg << std::endl;
                    }
                }
            } else {
                dispstat &= ~1; // Clear VBlank
            }
            // Also update V-Counter Match (Bit 2)
            uint16_t vcountSetting = (dispstat >> 8) & 0xFF;
            if (scanline == vcountSetting) {
                dispstat |= 4; // Set V-Counter Match
                
                // V-Counter IRQ (Bit 5)
                if (dispstat & 0x20) {
                     uint16_t if_reg = memory.Read16(0x04000202);
                     if_reg |= 4; // VCount IRQ bit (Bit 2)
                     memory.WriteIORegisterInternal(0x202, if_reg);
                     std::cout << "VCount IRQ Requested at Line " << std::dec << scanline << std::endl;
                }
            } else {
                dispstat &= ~4; // Clear V-Counter Match
            }
            memory.WriteIORegisterInternal(0x04, dispstat);
        }

        int currentScanline = scanline;

        // Hack removed (moved to rising edge logic)
    }

    void PPU::DrawScanline() {
        uint16_t dispcnt = ReadRegister(0x00); // DISPCNT
        int mode = dispcnt & 0x7;

        // Debug: Print DISPCNT once per frame (at scanline 0)
        if (scanline == 0 && frameCount % 60 == 0) {
             std::cout << "Frame " << frameCount << " DISPCNT: 0x" << std::hex << dispcnt << " Mode: " << mode << std::endl;
             // Check BG Enables
             bool bg0 = dispcnt & 0x100;
             bool bg1 = dispcnt & 0x200;
             bool bg2 = dispcnt & 0x400;
             bool bg3 = dispcnt & 0x800;
             std::cout << "  BG0:" << bg0 << " BG1:" << bg1 << " BG2:" << bg2 << " BG3:" << bg3 << std::endl;
        }

        // Fetch Backdrop Color (Palette Index 0)
        uint16_t backdropColor = memory.Read16(0x05000000);
        uint8_t r = (backdropColor & 0x1F) << 3;
        uint8_t g = ((backdropColor >> 5) & 0x1F) << 3;
        uint8_t b = ((backdropColor >> 10) & 0x1F) << 3;
        uint32_t backdropARGB = 0xFF000000 | (r << 16) | (g << 8) | b;

        // Clear line with backdrop color
        std::fill(framebuffer.begin() + scanline * SCREEN_WIDTH, 
                  framebuffer.begin() + (scanline + 1) * SCREEN_WIDTH, 
                  backdropARGB);

        // Debug: Check Palette at Scanline 100
        if (scanline == 100) {
            uint16_t pal0 = memory.Read16(0x05000000);
            uint16_t pal1 = memory.Read16(0x05000002);
            uint16_t objPal0 = memory.Read16(0x05000200);
            std::cout << "PPU: Scanline 100. Pal[0]=" << std::hex << pal0 << " Pal[1]=" << pal1 << " ObjPal[0]=" << objPal0 << std::dec << std::endl;
        }

        if (mode == 0) {
            RenderMode0();
        } else if (mode == 2) {
            RenderMode2();
        }
        
        // Render OBJ (Sprites)
        if (dispcnt & 0x1000) { // OBJ Enable
            RenderOBJ();
        }

        // Debug: Check for non-black pixels on this scanline
        if (scanline == 100) { // Check middle of screen
            bool hasContent = false;
            for (int x = 0; x < SCREEN_WIDTH; ++x) {
                if ((framebuffer[scanline * SCREEN_WIDTH + x] & 0xFFFFFF) != 0) {
                    hasContent = true;
                    break;
                }
            }
            if (hasContent) {
                std::cout << "PPU: Scanline " << scanline << " has non-black pixels!" << std::endl;
            } else {
                // std::cout << "PPU: Scanline " << scanline << " is all black." << std::endl;
            }
        }
    }

    void PPU::RenderOBJ() {
        // Basic OBJ Rendering (No Affine/Rotation yet)
        // OAM is at 0x07000000 (1KB)
        // 128 Sprites, 8 bytes each
        
        if (scanline == 100) std::cout << "RenderOBJ called at scanline 100" << std::endl;

        static int sprite_debug_count = 0;
        if (sprite_debug_count < 20) {
             uint32_t oamAddr = 0x07000000;
             uint16_t attr0 = memory.Read16(oamAddr);
             uint16_t attr1 = memory.Read16(oamAddr + 2);
             uint16_t attr2 = memory.Read16(oamAddr + 4);
             std::cout << "Sprite 0: Attr0=" << std::hex << attr0 << " Attr1=" << attr1 << " Attr2=" << attr2 << std::dec << std::endl;
             sprite_debug_count++;
        }

        // Iterate backwards for priority? Or forwards?
        // GBA renders sprites with lower index on TOP of higher index (if priority same).
        // So we should render 127 down to 0?
        // Actually, hardware has a line buffer.
        // For simple painter's algorithm, we render 127 first, then 0 last (on top).
        
        for (int i = 127; i >= 0; --i) {
            uint32_t oamAddr = 0x07000000 + (i * 8);
            uint16_t attr0 = memory.Read16(oamAddr);
            uint16_t attr1 = memory.Read16(oamAddr + 2);
            uint16_t attr2 = memory.Read16(oamAddr + 4);
            
            // Check Y Coordinate
            int y = attr0 & 0xFF;
            int mode = (attr0 >> 8) & 0x3; // 0=Normal, 1=Affine, 2=Hide, 3=Double Size Affine
            
            if (mode == 2) continue; // Hidden
            
            // Handle Y Wrapping (0-255)
            // If Y > 160, it might wrap?
            // Sprites are displayed at Y, Y+1, ...
            // If Y is e.g. 250, and height is 32, it wraps to 0-26.
            // Coordinate is 0-255. Screen is 160 high.
            // If Y > 160, it's off screen unless it wraps.
            // Actually, Y is treated as signed? No, 0-255.
            // But if Y=250, it means Y=-6.
            if (y > 160) y -= 256;
            
            // Shape (0=Square, 1=Horizontal, 2=Vertical)
            int shape = (attr0 >> 14) & 0x3;
            int size = (attr1 >> 14) & 0x3;
            
            int width = 8, height = 8;
            // Lookup table for size
            static const int sizes[3][4][2] = {
                {{8,8}, {16,16}, {32,32}, {64,64}}, // Square
                {{16,8}, {32,8}, {32,16}, {64,32}}, // Horizontal
                {{8,16}, {8,32}, {16,32}, {32,64}}  // Vertical
            };
            
            width = sizes[shape][size][0];
            height = sizes[shape][size][1];
            
            // Check if scanline is within sprite
            if (scanline >= y && scanline < y + height) {
                // Render this line of the sprite
                int x = attr1 & 0x1FF;
                if (x > 511) x -= 512; // 9-bit X
                
                if (i == 0 && scanline == 72) {
                     std::cout << "  Sprite 0 visible at scanline 72. X=" << x << " Tile=" << (attr2 & 0x3FF) << std::endl;
                     // Dump first 8 bytes of Tile 2
                     uint32_t tAddr = 0x06010040;
                     std::cout << "  Tile 2 Data: ";
                     for(int k=0; k<8; ++k) std::cout << std::hex << (int)memory.Read8(tAddr + k) << " ";
                     std::cout << std::dec << std::endl;
                }
                
                int tileIndex = attr2 & 0x3FF;
                int priority = (attr2 >> 10) & 0x3;
                int paletteBank = (attr2 >> 12) & 0xF;
                bool is8bpp = (attr0 >> 13) & 1;
                bool hFlip = (attr1 >> 12) & 1;
                bool vFlip = (attr1 >> 13) & 1;
                
                // Calculate line in sprite
                int spriteY = scanline - y;
                if (vFlip) spriteY = height - 1 - spriteY;
                
                // Tile Base for OBJ is 0x06010000 (Char Block 4)
                // But in 1D mapping mode (DISPCNT bit 6), tiles are linear.
                // In 2D mode, it's 32x32 grid.
                uint16_t dispcnt = ReadRegister(0x00);
                bool mapping1D = (dispcnt >> 6) & 1;
                
                uint32_t tileBase = 0x06010000;
                
                if (i == 0 && scanline == 72) std::cout << "  Starting Loop. Width=" << width << " Mapping1D=" << mapping1D << std::endl;
                
                for (int sx = 0; sx < width; ++sx) {
                    int screenX = x + sx;
                    if (screenX < 0 || screenX >= SCREEN_WIDTH) continue;
                    
                    int spriteX = sx;
                    if (hFlip) spriteX = width - 1 - sx;
                    
                    // Fetch Pixel
                    uint8_t colorIndex = 0;
                    
                    if (mapping1D) {
                        // 1D Mapping
                        int tileNum;
                        if (is8bpp) {
                            tileNum = tileIndex + (spriteY / 8) * (width / 8) * 2 + (spriteX / 8) * 2;
                        } else {
                            tileNum = tileIndex + (spriteY / 8) * (width / 8) + (spriteX / 8);
                        }
                        
                        int inTileX = spriteX % 8;
                        int inTileY = spriteY % 8;
                        
                        if (is8bpp) {
                            uint32_t addr = tileBase + tileNum * 32 + inTileY * 8 + inTileX; // Wait, 8bpp tile is 64 bytes?
                            // In 1D mode, tile index steps by 1 for 4bpp (32 bytes), by 2 for 8bpp (64 bytes).
                            // So tileNum is correct index into 32-byte blocks?
                            // Actually, 1D mapping: Address = Base + Index * 32.
                            // For 8bpp, we use 2 indices per tile.
                            addr = tileBase + tileNum * 32 + inTileY * 8 + inTileX; 
                            // Wait, 8bpp tile is 64 bytes. 8x8 pixels.
                            // If tileNum is 32-byte units.
                            // 8bpp tile takes 2 units.
                            // So addr is correct?
                            // Let's assume standard linear mapping.
                            colorIndex = memory.Read8(addr);
                        } else {
                            uint32_t addr = tileBase + tileNum * 32 + inTileY * 4 + (inTileX / 2);
                            uint8_t byte = memory.Read8(addr);
                            if (inTileX & 1) colorIndex = (byte >> 4) & 0xF;
                            else colorIndex = byte & 0xF;
                        }
                    } else {
                        // 2D Mapping
                        int tx = spriteX / 8;
                        int ty = spriteY / 8;
                        
                        int tileNum;
                        if (is8bpp) {
                            tileNum = tileIndex + ty * 32 + tx * 2;
                        } else {
                            tileNum = tileIndex + ty * 32 + tx;
                        }
                        
                        int inTileX = spriteX % 8;
                        int inTileY = spriteY % 8;
                        
                        if (is8bpp) {
                            uint32_t addr = tileBase + tileNum * 32 + inTileY * 8 + inTileX;
                            colorIndex = memory.Read8(addr);
                        } else {
                            uint32_t addr = tileBase + tileNum * 32 + inTileY * 4 + (inTileX / 2);
                            uint8_t byte = memory.Read8(addr);
                            if (inTileX & 1) colorIndex = (byte >> 4) & 0xF;
                            else colorIndex = byte & 0xF;
                        }
                    }
                    
                    if (i == 0 && scanline == 72 && sx < 8) {
                         std::cout << "    Sprite 0 Pixel at " << (x+sx) << " (sx=" << sx << "): ColorIdx=" << (int)colorIndex << std::endl;
                    }

                    if (colorIndex != 0) {
                        // Fetch Color (OBJ Palette starts at 0x05000200)
                        uint32_t paletteAddr = 0x05000200;
                        if (is8bpp) {
                            paletteAddr += colorIndex * 2;
                        } else {
                            paletteAddr += (paletteBank * 32) + (colorIndex * 2);
                        }
                        
                        uint16_t color = memory.Read16(paletteAddr);
                        
                        if (scanline == 100 && x + sx == 120) { // Debug center pixel
                             std::cout << "    Sprite Pixel at " << (x+sx) << ": ColorIdx=" << (int)colorIndex << " Color=" << std::hex << color << std::dec << std::endl;
                        }

                        uint8_t r = (color & 0x1F) << 3;
                        uint8_t g = ((color >> 5) & 0x1F) << 3;
                        uint8_t b = ((color >> 10) & 0x1F) << 3;
                        
                        framebuffer[scanline * SCREEN_WIDTH + screenX] = 0xFF000000 | (r << 16) | (g << 8) | b;
                    }
                }
            }
        }
    }

    void PPU::RenderMode2() {
        // Mode 2: Affine, BG2 and BG3
        uint16_t dispcnt = ReadRegister(0x00);
        
        // Render backgrounds in priority order (Back to Front)
        // For now, assuming default priority (BG3 > BG2)
        // TODO: Check BGxCNT priority bits
        
        if (dispcnt & 0x0800) RenderAffineBackground(3); // BG3
        if (dispcnt & 0x0400) RenderAffineBackground(2); // BG2
    }

    void PPU::RenderAffineBackground(int bgIndex) {
        // bgIndex is 2 or 3
        uint16_t bgcnt = ReadRegister(0x08 + (bgIndex * 2));
        
        if (scanline == 100) {
            std::cout << "RenderAffineBackground(" << bgIndex << ") called. BGCNT=" << std::hex << bgcnt << std::dec << std::endl;
        }

        // Affine Parameters
        // BG2: 0x20-0x2F, BG3: 0x30-0x3F
        uint32_t paramBase = 0x20 + (bgIndex - 2) * 0x10;
        
        int16_t pa = (int16_t)ReadRegister(paramBase + 0x00);
        int16_t pb = (int16_t)ReadRegister(paramBase + 0x02);
        int16_t pc = (int16_t)ReadRegister(paramBase + 0x04);
        int16_t pd = (int16_t)ReadRegister(paramBase + 0x06);
        
        // Reference Point (Internal registers should be used, but we approximate)
        // BGxX and BGxY are 28-bit (s19.8)
        // We read them as two 16-bit writes usually, but here we read from memory
        uint32_t bgx_l = ReadRegister(paramBase + 0x08);
        uint32_t bgx_h = ReadRegister(paramBase + 0x0A);
        int32_t bgx = (bgx_h << 16) | bgx_l;
        // Sign extend 28-bit to 32-bit
        if (bgx & 0x08000000) bgx |= 0xF0000000;

        uint32_t bgy_l = ReadRegister(paramBase + 0x0C);
        uint32_t bgy_h = ReadRegister(paramBase + 0x0E);
        int32_t bgy = (bgy_h << 16) | bgy_l;
        if (bgy & 0x08000000) bgy |= 0xF0000000;

        // Calculate start position for this scanline
        // x_start = bgx + scanline * pb
        // y_start = bgy + scanline * pd
        // Note: This assumes bgx/bgy were set for scanline 0.
        // If the game updates them during HBlank, this might be wrong.
        // But for now, it's a good start.
        
        int32_t cx = bgx + scanline * pb;
        int32_t cy = bgy + scanline * pd;

        if (scanline == 100) {
             std::cout << "  Affine Params: PA=" << pa << " PB=" << pb << " PC=" << pc << " PD=" << pd << " X=" << bgx << " Y=" << bgy << std::endl;
             std::cout << "  Start CX=" << cx << " CY=" << cy << std::endl;
        }

        // Screen Size (0-3)
        // 0: 16x16 tiles (128x128)
        // 1: 32x32 tiles (256x256)
        // 2: 64x64 tiles (512x512)
        // 3: 128x128 tiles (1024x1024)
        int screenSize = (bgcnt >> 14) & 0x3;
        int sizeShift = 7 + screenSize; // 128, 256, 512, 1024 -> 7, 8, 9, 10
        int sizeMask = (128 << screenSize) - 1;

        int screenBaseBlock = (bgcnt >> 8) & 0x1F;
        int charBaseBlock = (bgcnt >> 2) & 0x3;
        
        uint32_t vramBase = 0x06000000;
        uint32_t mapBase = vramBase + (screenBaseBlock * 2048);
        uint32_t tileBase = vramBase + (charBaseBlock * 16384);
        
        // Affine backgrounds always use 8bpp (256 colors)
        // Palette starts at 0x05000000 (256 colors * 2 bytes = 512 bytes)
        
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            // Convert fixed point (24.8) to integer
            int tx = cx >> 8;
            int ty = cy >> 8;
            
            // Check bounds (Affine maps wrap? Or transparent?)
            // GBATEK: "The display area ... wraps around" if overflow enabled?
            // Bit 13 of BGxCNT: Display Area Overflow (0=Transparent, 1=Wraparound)
            bool overflowWrap = (bgcnt >> 13) & 1;
            
            if (overflowWrap || (tx >= 0 && tx <= sizeMask && ty >= 0 && ty <= sizeMask)) {
                // Wrap coordinates
                int mapX = tx & sizeMask;
                int mapY = ty & sizeMask;
                
                // Fetch Tile Index from Map
                // Map is flat array of bytes
                int tileMapWidth = 16 << screenSize; // 16, 32, 64, 128 tiles
                int tileX = mapX / 8;
                int tileY = mapY / 8;
                
                uint32_t mapAddr = mapBase + (tileY * tileMapWidth) + tileX;
                uint8_t tileIndex = memory.Read8(mapAddr);
                
                // Fetch Pixel from Tile
                // 8bpp tiles are 64 bytes
                int inTileX = mapX % 8;
                int inTileY = mapY % 8;
                
                uint32_t tileAddr = tileBase + (tileIndex * 64) + (inTileY * 8) + inTileX;
                uint8_t colorIndex = memory.Read8(tileAddr);
                
                if (colorIndex != 0) {
                    if (scanline == 100 && x == 120) {
                         std::cout << "    BG Pixel at " << x << ": ColorIdx=" << (int)colorIndex << std::endl;
                    }

                    uint32_t paletteAddr = 0x05000000 + (colorIndex * 2);
                    uint16_t color = memory.Read16(paletteAddr);
                    
                    uint8_t r = (color & 0x1F) << 3;
                    uint8_t g = ((color >> 5) & 0x1F) << 3;
                    uint8_t b = ((color >> 10) & 0x1F) << 3;
                    
                    framebuffer[scanline * SCREEN_WIDTH + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
                }
            }
            
            // Increment position
            cx += pa;
            cy += pc;
        }
    }

    void PPU::RenderMode0() {
        // Mode 0: Tiled, BG0-BG3
        uint16_t dispcnt = ReadRegister(0x00);
        
        // Render backgrounds in order of priority (TODO: Real priority system)
        // For now, just render enabled backgrounds back-to-front based on ID?
        // Actually, we should check priority bits in BGxCNT.
        // Let's just render BG0, BG1, BG2, BG3 if enabled for now.
        // Wait, if BG0 is top, we should render it LAST.
        
        if (dispcnt & 0x0800) RenderBackground(3); // BG3
        if (dispcnt & 0x0400) RenderBackground(2); // BG2
        if (dispcnt & 0x0200) RenderBackground(1); // BG1
        if (dispcnt & 0x0100) RenderBackground(0); // BG0
    }

    void PPU::RenderBackground(int bgIndex) {
        uint16_t bgcnt = ReadRegister(0x08 + (bgIndex * 2));
        uint16_t bghofs = ReadRegister(0x10 + (bgIndex * 4));
        uint16_t bgvofs = ReadRegister(0x12 + (bgIndex * 4));

        int charBaseBlock = (bgcnt >> 2) & 0x3;
        int screenBaseBlock = (bgcnt >> 8) & 0x1F;
        bool is8bpp = (bgcnt >> 7) & 1; // Wait, bit 7 is 256 colors? Docs say Bit 7.
        // GBATEK: Bit 7 = 256 Colors/1 Palette (0=16/16, 1=256/1)
        
        // Screen Size (0-3)
        // 0: 256x256 (32x32 tiles)
        // 1: 512x256 (64x32 tiles)
        // 2: 256x512 (32x64 tiles)
        // 3: 512x512 (64x64 tiles)
        int screenSize = (bgcnt >> 14) & 0x3;
        int mapWidth = (screenSize & 1) ? 64 : 32;
        int mapHeight = (screenSize & 2) ? 64 : 32;

        uint32_t vramBase = 0x06000000;
        uint32_t mapBase = vramBase + (screenBaseBlock * 2048);
        uint32_t tileBase = vramBase + (charBaseBlock * 16384);

        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            int scrolledX = (x + bghofs) & 0x1FF; // TODO: Handle larger maps correctly
            int scrolledY = (scanline + bgvofs) & 0x1FF;

            // Simple wrapping for 256x256 map for now
            // TODO: Implement full screen size logic (multi-map)
            int tx = (scrolledX / 8) % 32;
            int ty = (scrolledY / 8) % 32;

            // Fetch Tile Map Entry
            // Map is 32x32 entries (2KB)
            uint32_t mapAddr = mapBase + (ty * 32 + tx) * 2;
            uint16_t tileEntry = memory.Read16(mapAddr);

            int tileIndex = tileEntry & 0x3FF;
            bool hFlip = (tileEntry >> 10) & 1;
            bool vFlip = (tileEntry >> 11) & 1;
            int paletteBank = (tileEntry >> 12) & 0xF;

            int inTileX = scrolledX % 8;
            int inTileY = scrolledY % 8;

            if (hFlip) inTileX = 7 - inTileX;
            if (vFlip) inTileY = 7 - inTileY;

            uint8_t colorIndex = 0;

            if (!is8bpp) {
                // 4bpp (16 colors)
                // 32 bytes per tile (8x8 pixels * 4 bits = 256 bits = 32 bytes)
                uint32_t tileAddr = tileBase + (tileIndex * 32) + (inTileY * 4) + (inTileX / 2);
                uint8_t byte = memory.Read8(tileAddr);
                
                if (inTileX & 1) {
                    colorIndex = (byte >> 4) & 0xF;
                } else {
                    colorIndex = byte & 0xF;
                }
            } else {
                // 8bpp (256 colors)
                // 64 bytes per tile
                uint32_t tileAddr = tileBase + (tileIndex * 64) + (inTileY * 8) + inTileX;
                colorIndex = memory.Read8(tileAddr);
            }

            if (colorIndex != 0) { // Index 0 is transparent
                // Fetch Color from Palette RAM
                // 0x05000000
                uint32_t paletteAddr = 0x05000000;
                if (!is8bpp) {
                    paletteAddr += (paletteBank * 32) + (colorIndex * 2);
                } else {
                    paletteAddr += (colorIndex * 2);
                }

                uint16_t color = memory.Read16(paletteAddr);
                
                // Convert 15-bit BGR to 32-bit ARGB
                // GBA: xBBBBBGGGGGRRRRR
                uint8_t r = (color & 0x1F) << 3;
                uint8_t g = ((color >> 5) & 0x1F) << 3;
                uint8_t b = ((color >> 10) & 0x1F) << 3;

                framebuffer[scanline * SCREEN_WIDTH + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
        }
    }

    uint16_t PPU::ReadRegister(uint32_t offset) {
        // IO Registers start at 0x04000000
        return memory.Read16(0x04000000 + offset);
    }

    const std::vector<uint32_t>& PPU::GetFramebuffer() const {
        return framebuffer;
    }

}
