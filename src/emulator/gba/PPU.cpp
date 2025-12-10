#include <emulator/gba/PPU.h>
#include <emulator/gba/GBAMemory.h>
#include <algorithm>
#include <iostream>
#include <iomanip>

namespace AIO::Emulator::GBA {

    PPU::PPU(GBAMemory& mem) : memory(mem), cycleCounter(0), scanline(0), frameCount(0) {
        // Initialize framebuffer with black
        framebuffer.resize(SCREEN_WIDTH * SCREEN_HEIGHT, 0xFF000000);
        // Initialize priority buffer (4 = backdrop, lowest priority)
        priorityBuffer.resize(SCREEN_WIDTH * SCREEN_HEIGHT, 4);
    }

    PPU::~PPU() = default;

    void PPU::Update(int cycles) {
        // GBA PPU Timing (Simplified for now)
        // 4 cycles per pixel (roughly)
        // 240 pixels + 68 HBlank = 308 pixels per line
        // 308 * 4 = 1232 cycles per line
        // 160 lines + 68 VBlank = 228 lines total
        
        while (cycles > 0) {
            // Determine how many cycles we can advance in this step
            // We stop at HBlank Start (960) or End of Line (1232)
            
            int nextEvent = 1232; // Default to end of line
            if (cycleCounter < 960) {
                nextEvent = 960; // Stop at HBlank start
            }
            
            int cyclesToEvent = nextEvent - cycleCounter;
            int step = std::min(cycles, cyclesToEvent);
            
            cycleCounter += step;
            cycles -= step;
            
            // Check if we hit an event
            if (cycleCounter == 960) {
                // HBlank Start
                if (scanline < 160) {
                    memory.CheckDMA(2);
                    
                    // Also Trigger HBlank IRQ if enabled in DISPSTAT
                    uint16_t dispstat = memory.Read16(0x04000004);
                    if (dispstat & 0x10) { // HBlank IRQ Enable (Bit 4)
                        uint16_t if_reg = memory.Read16(0x04000202);
                        if_reg |= 2; // HBlank IRQ bit (Bit 1)
                        memory.Write16(0x04000202, if_reg);
                    }
                    
                    // Update DISPSTAT HBlank Flag (Bit 1)
                    dispstat |= 2;
                    memory.WriteIORegisterInternal(0x04, dispstat);
                }
            }
            else if (cycleCounter >= 1232) {
                // End of Line
                cycleCounter = 0;
                
                // Clear DISPSTAT HBlank Flag
                uint16_t dispstat = memory.Read16(0x04000004);
                dispstat &= ~2;
                memory.WriteIORegisterInternal(0x04, dispstat);

                // Render Scanline (if visible)
                if (scanline < 160) {
                    DrawScanline();
                }

                scanline++;
                if (scanline >= 228) {
                    scanline = 0;
                    frameCount++;

                }
                
                // Update VCOUNT
                memory.WriteIORegisterInternal(0x06, scanline);

                // Update DISPSTAT VBlank flag
                dispstat = memory.Read16(0x04000004);
                bool isVBlank = (scanline >= 160 && scanline <= 227);
                
                bool wasVBlank = prevVBlankState;
                prevVBlankState = isVBlank;

                if (isVBlank) {
                    dispstat |= 1; // Set VBlank
                    
                    // Trigger VBlank IRQ on rising edge
                    if (!wasVBlank) {
                        memory.CheckDMA(1); // VBlank DMA
                        
                        // Latch BGxX/BGxY to internal registers at VBlank start
                        // BG2 reference point (0x04000028-0x0400002F)
                        uint32_t bg2x_l = ReadRegister(0x28);
                        uint32_t bg2x_h = ReadRegister(0x2A);
                        bg2x_internal = (bg2x_h << 16) | bg2x_l;
                        if (bg2x_internal & 0x08000000) bg2x_internal |= 0xF0000000; // Sign extend
                        
                        uint32_t bg2y_l = ReadRegister(0x2C);
                        uint32_t bg2y_h = ReadRegister(0x2E);
                        bg2y_internal = (bg2y_h << 16) | bg2y_l;
                        if (bg2y_internal & 0x08000000) bg2y_internal |= 0xF0000000;
                        
                        // BG3 reference point (0x04000038-0x0400003F)
                        uint32_t bg3x_l = ReadRegister(0x38);
                        uint32_t bg3x_h = ReadRegister(0x3A);
                        bg3x_internal = (bg3x_h << 16) | bg3x_l;
                        if (bg3x_internal & 0x08000000) bg3x_internal |= 0xF0000000;
                        
                        uint32_t bg3y_l = ReadRegister(0x3C);
                        uint32_t bg3y_h = ReadRegister(0x3E);
                        bg3y_internal = (bg3y_h << 16) | bg3y_l;
                        if (bg3y_internal & 0x08000000) bg3y_internal |= 0xF0000000;

                        if (dispstat & 0x8) { // VBlank IRQ Enable
                            uint16_t if_reg = memory.Read16(0x04000202) | 1;
                            memory.WriteIORegisterInternal(0x202, if_reg);
                        }
                    }
                } else {
                    dispstat &= ~1; // Clear VBlank
                }
                
                // V-Counter Match
                uint16_t vcountSetting = (dispstat >> 8) & 0xFF;
                if (scanline == vcountSetting) {
                    dispstat |= 4;
                    if (dispstat & 0x20) { // VCount IRQ Enable
                         uint16_t if_reg = memory.Read16(0x04000202);
                         if_reg |= 4;
                         memory.WriteIORegisterInternal(0x202, if_reg);
                    }
                } else {
                    dispstat &= ~4;
                }
                memory.WriteIORegisterInternal(0x04, dispstat);
            }
        }
    }

    void PPU::DrawScanline() {
        uint16_t dispcnt = ReadRegister(0x00);
        int mode = dispcnt & 0x7;

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
        
        // Reset priority buffer for this scanline (4 = backdrop, lowest priority)
        std::fill(priorityBuffer.begin() + scanline * SCREEN_WIDTH,
                  priorityBuffer.begin() + (scanline + 1) * SCREEN_WIDTH,
                  (uint8_t)4);

        if (mode == 0) {
            RenderMode0();
        } else if (mode == 1) {
            RenderMode1();
        } else if (mode == 2) {
            RenderMode2();
        } else if (mode == 3) {
            RenderMode3();
        } else if (mode == 4) {
            RenderMode4();
        } else if (mode == 5) {
            RenderMode5();
        }
        
        // Render OBJ (Sprites)
        if (dispcnt & 0x1000) { // OBJ Enable
            RenderOBJ();
        }
        
        // Apply Color Special Effects (Blending/Brightness)
        ApplyColorEffects();
    }

    void PPU::RenderOBJ() {
        // Basic OBJ Rendering (No Affine/Rotation yet)
        // OAM is at 0x07000000 (1KB)
        // 128 Sprites, 8 bytes each
        
        // Iterate backwards for priority (127 first, then 0 on top)
        for (int i = 127; i >= 0; --i) {
            uint32_t oamAddr = 0x07000000 + (i * 8);
            uint16_t attr0 = memory.Read16(oamAddr);
            uint16_t attr1 = memory.Read16(oamAddr + 2);
            uint16_t attr2 = memory.Read16(oamAddr + 4);
            
            // Check Y Coordinate
            int y = attr0 & 0xFF;
            int mode = (attr0 >> 8) & 0x3; // 0=Normal, 1=Affine, 2=Hide, 3=Double Size Affine
            
            if (mode == 2) continue; // Hidden
            
            bool isAffine = (mode == 1 || mode == 3);
            bool isDoubleSize = (mode == 3);
            
            // Handle Y Wrapping (0-255)
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
            
            // For double-size affine, the bounding box is doubled
            int boundWidth = isDoubleSize ? width * 2 : width;
            int boundHeight = isDoubleSize ? height * 2 : height;
            
            // Check if scanline is within sprite bounds
            if (scanline >= y && scanline < y + boundHeight) {
                // Render this line of the sprite
                int x = attr1 & 0x1FF;
                if (x >= 256) x -= 512; // Sign extend 9-bit X
                
                int tileIndex = attr2 & 0x3FF;
                int priority = (attr2 >> 10) & 0x3;
                int paletteBank = (attr2 >> 12) & 0xF;
                bool is8bpp = (attr0 >> 13) & 1;
                
                // Flip flags only apply to non-affine sprites
                bool hFlip = !isAffine && ((attr1 >> 12) & 1);
                bool vFlip = !isAffine && ((attr1 >> 13) & 1);
                
                // Affine parameters
                int16_t pa = 0x100, pb = 0, pc = 0, pd = 0x100; // Identity matrix (1.0 in 8.8 fixed point)
                if (isAffine) {
                    // Get affine parameter group index from bits 9-13 of attr1
                    int affineIndex = (attr1 >> 9) & 0x1F;
                    // Each affine parameter group is 32 bytes apart in OAM
                    // Parameters are at offsets 6, 14, 22, 30 within each 32-byte block
                    uint32_t affineBase = 0x07000006 + (affineIndex * 32);
                    pa = (int16_t)memory.Read16(affineBase);
                    pb = (int16_t)memory.Read16(affineBase + 8);
                    pc = (int16_t)memory.Read16(affineBase + 16);
                    pd = (int16_t)memory.Read16(affineBase + 24);
                }
                
                // Center of the sprite in sprite coordinates
                int centerX = width / 2;
                int centerY = height / 2;
                
                // Tile Base for OBJ is 0x06010000 (Char Block 4)
                uint16_t dispcnt = ReadRegister(0x00);
                bool mapping1D = (dispcnt >> 6) & 1;
                uint32_t tileBase = 0x06010000;
                
                for (int sx = 0; sx < boundWidth; ++sx) {
                    int screenX = x + sx;
                    if (screenX < 0 || screenX >= SCREEN_WIDTH) continue;
                    
                    int spriteX, spriteY;
                    
                    if (isAffine) {
                        // Calculate texture coordinates using inverse affine transformation
                        // Screen position relative to center of bounds
                        int px = sx - boundWidth / 2;
                        int py = (scanline - y) - boundHeight / 2;
                        
                        // Apply inverse affine matrix (in 8.8 fixed point)
                        // texX = pa * px + pb * py + centerX
                        // texY = pc * px + pd * py + centerY
                        spriteX = ((pa * px + pb * py) >> 8) + centerX;
                        spriteY = ((pc * px + pd * py) >> 8) + centerY;
                        
                        // Check if we're within the actual sprite bounds
                        if (spriteX < 0 || spriteX >= width || spriteY < 0 || spriteY >= height) {
                            continue;
                        }
                    } else {
                        // Non-affine sprite
                        spriteX = sx;
                        int lineInSprite = scanline - y;
                        
                        if (hFlip) spriteX = width - 1 - sx;
                        if (vFlip) lineInSprite = height - 1 - lineInSprite;
                        
                        spriteY = lineInSprite;
                    }
                    
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
                            uint32_t addr = tileBase + tileNum * 32 + inTileY * 8 + inTileX;
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

                    if (colorIndex != 0) {
                        // Check if OBJ layer is enabled by window settings at this pixel
                        if (!IsLayerEnabledAtPixel(screenX, scanline, 4)) {
                            continue; // Window masks OBJ at this position
                        }
                        
                        // Check OBJ priority against BG priority at this pixel
                        // OBJ with priority N is drawn in front of BG with priority N or higher (lower priority)
                        // That is, OBJ priority 2 draws in front of BG priority 2, 3 but behind BG priority 0, 1
                        int pixelIndex = scanline * SCREEN_WIDTH + screenX;
                        
                        // Only draw if sprite priority <= existing pixel priority
                        // (lower number = higher display priority)
                        if (priority <= priorityBuffer[pixelIndex]) {
                            // Fetch Color (OBJ Palette starts at 0x05000200)
                            uint32_t paletteAddr = 0x05000200;
                            if (is8bpp) {
                                paletteAddr += colorIndex * 2;
                            } else {
                                paletteAddr += (paletteBank * 32) + (colorIndex * 2);
                            }
                            
                            uint16_t color = memory.Read16(paletteAddr);

                            uint8_t r = (color & 0x1F) << 3;
                            uint8_t g = ((color >> 5) & 0x1F) << 3;
                            uint8_t b = ((color >> 10) & 0x1F) << 3;
                            
                            framebuffer[pixelIndex] = 0xFF000000 | (r << 16) | (g << 8) | b;
                            // Update priority buffer (OBJ takes this priority slot)
                            priorityBuffer[pixelIndex] = priority;
                        }
                    }
                }
            }
        }
    }

    void PPU::RenderMode2() {
        // Mode 2: Affine, BG2 and BG3
        uint16_t dispcnt = ReadRegister(0x00);
        
        // Get priorities for enabled BGs
        bool bg2Enabled = dispcnt & 0x0400;
        bool bg3Enabled = dispcnt & 0x0800;
        
        int bg2Priority = bg2Enabled ? (ReadRegister(0x0C) & 0x3) : 99;
        int bg3Priority = bg3Enabled ? (ReadRegister(0x0E) & 0x3) : 99;
        
        // Render in priority order: lower priority first, then higher (BG2 wins ties)
        if (bg2Priority > bg3Priority || (bg2Priority == bg3Priority && bg2Enabled)) {
            if (bg3Enabled) RenderAffineBackground(3);
            if (bg2Enabled) RenderAffineBackground(2);
        } else {
            if (bg2Enabled) RenderAffineBackground(2);
            if (bg3Enabled) RenderAffineBackground(3);
        }
    }

    void PPU::RenderAffineBackground(int bgIndex) {
        // bgIndex is 2 or 3
        uint16_t bgcnt = ReadRegister(0x08 + (bgIndex * 2));
        
        // Get background priority (0 = highest, 3 = lowest)
        int bgPriority = bgcnt & 0x3;

        // Affine Parameters
        // BG2: 0x20-0x2F, BG3: 0x30-0x3F
        uint32_t paramBase = 0x20 + (bgIndex - 2) * 0x10;
        
        int16_t pa = (int16_t)ReadRegister(paramBase + 0x00);
        int16_t pb = (int16_t)ReadRegister(paramBase + 0x02);
        int16_t pc = (int16_t)ReadRegister(paramBase + 0x04);
        int16_t pd = (int16_t)ReadRegister(paramBase + 0x06);
        
        // Use internal reference point registers (properly latched at VBlank)
        int32_t* bgx_int_ptr;
        int32_t* bgy_int_ptr;
        if (bgIndex == 2) {
            bgx_int_ptr = &bg2x_internal;
            bgy_int_ptr = &bg2y_internal;
        } else {
            bgx_int_ptr = &bg3x_internal;
            bgy_int_ptr = &bg3y_internal;
        }
        
        // Get current internal reference point for this scanline
        int32_t cx = *bgx_int_ptr;
        int32_t cy = *bgy_int_ptr;

        // Screen Size (0-3)
        int screenSize = (bgcnt >> 14) & 0x3;
        int sizeShift = 7 + screenSize;
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
                    // Check if this BG layer is enabled by window settings at this pixel
                    if (!IsLayerEnabledAtPixel(x, scanline, bgIndex)) {
                        cx += pa;
                        cy += pc;
                        continue;
                    }
                    
                    int pixelIndex = scanline * SCREEN_WIDTH + x;
                    if (bgPriority <= priorityBuffer[pixelIndex]) {
                        uint32_t paletteAddr = 0x05000000 + (colorIndex * 2);
                        uint16_t color = memory.Read16(paletteAddr);
                        
                        uint8_t r = (color & 0x1F) << 3;
                        uint8_t g = ((color >> 5) & 0x1F) << 3;
                        uint8_t b = ((color >> 10) & 0x1F) << 3;
                        
                        framebuffer[pixelIndex] = 0xFF000000 | (r << 16) | (g << 8) | b;
                        priorityBuffer[pixelIndex] = bgPriority;
                    }
                }
            }
            
            // Increment position for next pixel
            cx += pa;
            cy += pc;
        }
        
        // Update internal reference point for next scanline
        // Add pb and pd to the internal registers (not pa and pc!)
        *bgx_int_ptr += pb;
        *bgy_int_ptr += pd;
    }

    void PPU::RenderMode3() {
        // Mode 3: 240x160 16bpp Bitmap (direct color, no palette)
        // Frame buffer at 0x06000000
        // Each pixel is 2 bytes (BGR555)
        
        uint32_t vramBase = 0x06000000;
        
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            uint32_t addr = vramBase + (scanline * SCREEN_WIDTH + x) * 2;
            uint16_t color = memory.Read16(addr);
            
            uint8_t r = (color & 0x1F) << 3;
            uint8_t g = ((color >> 5) & 0x1F) << 3;
            uint8_t b = ((color >> 10) & 0x1F) << 3;
            
            framebuffer[scanline * SCREEN_WIDTH + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }

    void PPU::RenderMode4() {
        // Mode 4: 240x160 8bpp Indexed Bitmap (palette lookup)
        // Frame buffer at 0x06000000 (or 0x0600A000 for page 2)
        // Each pixel is 1 byte (palette index)
        // Palette at 0x05000000
        
        uint16_t dispcnt = ReadRegister(0x00);
        bool page1 = (dispcnt >> 4) & 1; // Bit 4 = Display Frame Select
        
        uint32_t vramBase = page1 ? 0x0600A000 : 0x06000000;
        
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            uint32_t addr = vramBase + scanline * SCREEN_WIDTH + x;
            uint8_t colorIndex = memory.Read8(addr);
            
            if (colorIndex != 0) { // Index 0 is transparent/backdrop
                uint32_t paletteAddr = 0x05000000 + colorIndex * 2;
                uint16_t color = memory.Read16(paletteAddr);
                
                uint8_t r = (color & 0x1F) << 3;
                uint8_t g = ((color >> 5) & 0x1F) << 3;
                uint8_t b = ((color >> 10) & 0x1F) << 3;
                
                framebuffer[scanline * SCREEN_WIDTH + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
            // If colorIndex == 0, backdrop is already filled
        }
    }

    void PPU::RenderMode5() {
        // Mode 5: 160x128 16bpp Bitmap (direct color), double buffered
        // Smaller resolution centered on screen
        // Frame buffer at 0x06000000 (or 0x0600A000 for page 2)
        // Each pixel is 2 bytes (BGR555)
        
        uint16_t dispcnt = ReadRegister(0x00);
        bool page1 = (dispcnt >> 4) & 1; // Bit 4 = Display Frame Select
        
        uint32_t vramBase = page1 ? 0x0600A000 : 0x06000000;
        
        // Mode 5 is 160x128, centered would start at (40, 16) on 240x160 screen
        // But games typically handle positioning themselves
        // We render the 160x128 area into the top-left for simplicity
        // (Games may use affine to position it)
        
        const int MODE5_WIDTH = 160;
        const int MODE5_HEIGHT = 128;
        
        if (scanline < MODE5_HEIGHT) {
            for (int x = 0; x < MODE5_WIDTH && x < SCREEN_WIDTH; ++x) {
                uint32_t addr = vramBase + (scanline * MODE5_WIDTH + x) * 2;
                uint16_t color = memory.Read16(addr);
                
                uint8_t r = (color & 0x1F) << 3;
                uint8_t g = ((color >> 5) & 0x1F) << 3;
                uint8_t b = ((color >> 10) & 0x1F) << 3;
                
                framebuffer[scanline * SCREEN_WIDTH + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
        }
        // Scanlines >= 128 will just show backdrop
    }

    void PPU::RenderMode1() {
        // Mode 1: Mixed Tiled mode
        // BG0, BG1 = Regular tiled (text mode)
        // BG2 = Affine/Rotation-Scaling
        // BG3 = Not available in Mode 1
        
        uint16_t dispcnt = ReadRegister(0x00);
        
        // Get priorities for enabled BGs
        struct BGInfo { int index; int priority; bool enabled; bool affine; };
        BGInfo bgs[3];
        
        bgs[0] = {0, 0, (dispcnt & 0x0100) != 0, false};
        bgs[1] = {1, 0, (dispcnt & 0x0200) != 0, false};
        bgs[2] = {2, 0, (dispcnt & 0x0400) != 0, true};
        
        for (int i = 0; i < 3; ++i) {
            if (bgs[i].enabled) {
                uint16_t bgcnt = ReadRegister(0x08 + (bgs[i].index * 2));
                bgs[i].priority = bgcnt & 0x3;
            } else {
                bgs[i].priority = 99;
            }
        }
        
        // Sort by priority (descending) then by index (descending for same priority)
        for (int i = 0; i < 3; ++i) {
            for (int j = i + 1; j < 3; ++j) {
                if (bgs[i].priority < bgs[j].priority ||
                    (bgs[i].priority == bgs[j].priority && bgs[i].index < bgs[j].index)) {
                    BGInfo temp = bgs[i];
                    bgs[i] = bgs[j];
                    bgs[j] = temp;
                }
            }
        }
        
        // Render in sorted order (lowest priority first)
        for (int i = 0; i < 3; ++i) {
            if (bgs[i].enabled) {
                if (bgs[i].affine) {
                    RenderAffineBackground(bgs[i].index);
                } else {
                    RenderBackground(bgs[i].index);
                }
            }
        }
    }

    void PPU::RenderMode0() {
        // Mode 0: Tiled, BG0-BG3
        uint16_t dispcnt = ReadRegister(0x00);
        
        // Render backgrounds from lowest priority to highest
        // BG priority is in bits 0-1 of BGxCNT (0 = highest, 3 = lowest)
        // When priorities are equal, lower BG number wins (BG0 > BG1 > BG2 > BG3)
        // So we render: priority 3 first, then 2, 1, 0
        // Within same priority: BG3 first, then BG2, BG1, BG0 (so BG0 wins on ties)
        
        // Get priorities for enabled BGs
        struct BGInfo { int index; int priority; bool enabled; };
        BGInfo bgs[4];
        
        for (int i = 0; i < 4; ++i) {
            bgs[i].index = i;
            bgs[i].enabled = dispcnt & (0x100 << i);
            if (bgs[i].enabled) {
                uint16_t bgcnt = ReadRegister(0x08 + (i * 2));
                bgs[i].priority = bgcnt & 0x3;
            } else {
                bgs[i].priority = 99; // Won't be rendered
            }
        }
        
        // Sort by priority (descending) then by index (descending for same priority)
        // This means we render lowest priority first, BG with higher index first for ties
        for (int i = 0; i < 4; ++i) {
            for (int j = i + 1; j < 4; ++j) {
                if (bgs[i].priority < bgs[j].priority ||
                    (bgs[i].priority == bgs[j].priority && bgs[i].index < bgs[j].index)) {
                    BGInfo temp = bgs[i];
                    bgs[i] = bgs[j];
                    bgs[j] = temp;
                }
            }
        }
        
        // Render in sorted order (lowest priority first)
        for (int i = 0; i < 4; ++i) {
            if (bgs[i].enabled) {
                RenderBackground(bgs[i].index);
            }
        }
    }

    void PPU::RenderBackground(int bgIndex) {
        uint16_t bgcnt = ReadRegister(0x08 + (bgIndex * 2));
        uint16_t bghofs = ReadRegister(0x10 + (bgIndex * 4));
        uint16_t bgvofs = ReadRegister(0x12 + (bgIndex * 4));
        
        // Get background priority (0 = highest, 3 = lowest)
        int bgPriority = bgcnt & 0x3;

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
            int scrolledX = (x + bghofs) & 0x1FF; // 512 pixel wrap
            int scrolledY = (scanline + bgvofs) & 0x1FF;

            // Handle multi-screen-block maps for larger sizes
            // Size 0: 256x256 (1 block, 32x32 tiles)
            // Size 1: 512x256 (2 blocks horizontal, 64x32 tiles)
            // Size 2: 256x512 (2 blocks vertical, 32x64 tiles)
            // Size 3: 512x512 (4 blocks, 64x64 tiles)
            
            int tx = (scrolledX / 8);
            int ty = (scrolledY / 8);
            
            // Calculate which screen block we're in
            int blockX = (tx >= 32) ? 1 : 0;
            int blockY = (ty >= 32) ? 1 : 0;
            
            // Wrap tile coordinates within the block
            tx &= 31;
            ty &= 31;
            
            // Calculate block offset
            int blockOffset = 0;
            switch (screenSize) {
                case 0: // 32x32, single block
                    break;
                case 1: // 64x32, two horizontal blocks
                    blockOffset = blockX * 2048;
                    break;
                case 2: // 32x64, two vertical blocks
                    blockOffset = blockY * 2048;
                    break;
                case 3: // 64x64, four blocks
                    blockOffset = blockX * 2048 + blockY * 4096;
                    break;
            }

            // Fetch Tile Map Entry
            // Each screen block is 32x32 entries (2KB = 2048 bytes)
            uint32_t mapAddr = mapBase + blockOffset + (ty * 32 + tx) * 2;
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
                // Check if this BG layer is enabled by window settings at this pixel
                if (!IsLayerEnabledAtPixel(x, scanline, bgIndex)) {
                    continue; // Window masks this layer at this position
                }
                
                // Only write if this BG has higher or equal priority (lower or equal number) than what's already there
                // We use <= because when priority is the same, lower BG index wins (rendered later in sorted order)
                int pixelIndex = scanline * SCREEN_WIDTH + x;
                if (bgPriority <= priorityBuffer[pixelIndex]) {
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

                    framebuffer[pixelIndex] = 0xFF000000 | (r << 16) | (g << 8) | b;
                    priorityBuffer[pixelIndex] = bgPriority;
                }
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
    
    // Get window enable bits for a given pixel position
    // Returns the enable mask (bits 0-3: BG0-3, bit 4: OBJ, bit 5: Color Effects)
    uint8_t PPU::GetWindowMaskForPixel(int x, int y) {
        uint16_t dispcnt = ReadRegister(0x00);
        bool win0Enable = (dispcnt >> 13) & 1;
        bool win1Enable = (dispcnt >> 14) & 1;
        bool objwinEnable = (dispcnt >> 15) & 1;
        
        // If no windows are enabled, all layers are visible
        if (!win0Enable && !win1Enable && !objwinEnable) {
            return 0x3F; // All layers and effects enabled
        }
        
        // Check WIN0 (highest priority)
        if (win0Enable) {
            uint16_t win0h = ReadRegister(0x40);
            uint16_t win0v = ReadRegister(0x44);
            
            int win0Left = (win0h >> 8) & 0xFF;
            int win0Right = win0h & 0xFF;
            int win0Top = (win0v >> 8) & 0xFF;
            int win0Bottom = win0v & 0xFF;
            
            // Handle horizontal wrap-around
            bool inWin0H;
            if (win0Left <= win0Right) {
                inWin0H = (x >= win0Left && x < win0Right);
            } else {
                // Wrap case: left > right means window wraps around screen edge
                inWin0H = (x >= win0Left || x < win0Right);
            }
            
            // Handle vertical wrap-around  
            bool inWin0V;
            if (win0Top <= win0Bottom) {
                inWin0V = (y >= win0Top && y < win0Bottom);
            } else {
                inWin0V = (y >= win0Top || y < win0Bottom);
            }
            
            if (inWin0H && inWin0V) {
                uint16_t winin = ReadRegister(0x48);
                return winin & 0x3F; // WIN0 enable bits (lower 6 bits)
            }
        }
        
        // Check WIN1 (second priority)
        if (win1Enable) {
            uint16_t win1h = ReadRegister(0x42);
            uint16_t win1v = ReadRegister(0x46);
            
            int win1Left = (win1h >> 8) & 0xFF;
            int win1Right = win1h & 0xFF;
            int win1Top = (win1v >> 8) & 0xFF;
            int win1Bottom = win1v & 0xFF;
            
            bool inWin1H;
            if (win1Left <= win1Right) {
                inWin1H = (x >= win1Left && x < win1Right);
            } else {
                inWin1H = (x >= win1Left || x < win1Right);
            }
            
            bool inWin1V;
            if (win1Top <= win1Bottom) {
                inWin1V = (y >= win1Top && y < win1Bottom);
            } else {
                inWin1V = (y >= win1Top || y < win1Bottom);
            }
            
            if (inWin1H && inWin1V) {
                uint16_t winin = ReadRegister(0x48);
                return (winin >> 8) & 0x3F; // WIN1 enable bits (upper 6 bits of lower word)
            }
        }
        
        // TODO: Check OBJ window (would require checking OBJ window pixels)
        // For now, skip OBJ window
        
        // Outside all windows - use WINOUT
        uint16_t winout = ReadRegister(0x4A);
        return winout & 0x3F;
    }
    
    // Check if a specific layer should be rendered at this pixel
    // layer: 0-3 = BG0-3, 4 = OBJ, 5 = Color Effects
    bool PPU::IsLayerEnabledAtPixel(int x, int y, int layer) {
        uint8_t mask = GetWindowMaskForPixel(x, y);
        return (mask >> layer) & 1;
    }
    
    void PPU::ApplyColorEffects() {
        // Read blend control registers
        uint16_t bldcnt = ReadRegister(0x50);   // BLDCNT
        uint16_t bldalpha = ReadRegister(0x52); // BLDALPHA
        uint16_t bldy = ReadRegister(0x54);     // BLDY
        
        int effectMode = (bldcnt >> 6) & 0x3;
        
        if (effectMode == 0) {
            return;
        }
        
        // Get the brightness coefficient (0-16, higher = more effect)
        int evy = bldy & 0x1F;
        if (evy > 16) evy = 16;
        
        // For alpha blending (mode 1)
        int eva = bldalpha & 0x1F;       // First target coefficient
        int evb = (bldalpha >> 8) & 0x1F; // Second target coefficient
        if (eva > 16) eva = 16;
        if (evb > 16) evb = 16;
        
        // First target layers (bits 0-5 of BLDCNT)
        uint8_t firstTarget = bldcnt & 0x3F;
        // Second target layers (bits 8-13 of BLDCNT)  
        uint8_t secondTarget = (bldcnt >> 8) & 0x3F;
        
        // Apply effect to each pixel on this scanline
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            int pixelIndex = scanline * SCREEN_WIDTH + x;
            
            // Check if color effects are enabled at this pixel (window bit 5)
            if (!IsLayerEnabledAtPixel(x, scanline, 5)) {
                continue;
            }
            
            uint32_t color = framebuffer[pixelIndex];
            uint8_t r = (color >> 16) & 0xFF;
            uint8_t g = (color >> 8) & 0xFF;
            uint8_t b = color & 0xFF;
            
            // For now, we'll apply effects to all visible pixels
            // A more accurate implementation would track which layer each pixel came from
            // and only apply effects to first-target pixels
            
            // The priorityBuffer tells us the layer:
            // 0-3 = BG0-3 (by priority), 4 = backdrop
            // But we need to know the actual BG index, not just priority...
            // For simplicity, let's assume the effect applies if the mode is brightness
            
            if (effectMode == 2) {
                // Brightness Increase (fade to white)
                // I = I + (31-I) * EVY / 16
                r = r + ((255 - r) * evy / 16);
                g = g + ((255 - g) * evy / 16);
                b = b + ((255 - b) * evy / 16);
            } else if (effectMode == 3) {
                // Brightness Decrease (fade to black)
                // I = I - I * EVY / 16
                r = r - (r * evy / 16);
                g = g - (g * evy / 16);
                b = b - (b * evy / 16);
            }
            // Mode 1 (alpha blending) requires knowing both layers at each pixel
            // which needs more complex tracking - skip for now
            
            framebuffer[pixelIndex] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }

}
