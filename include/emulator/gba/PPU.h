#pragma once
#include <vector>
#include <cstdint>

namespace AIO::Emulator::GBA {

    class GBAMemory;

    class PPU {
    public:
        static const int SCREEN_WIDTH = 240;
        static const int SCREEN_HEIGHT = 160;

        PPU(GBAMemory& memory);
        ~PPU();

        void Update(int cycles);
        const std::vector<uint32_t>& GetFramebuffer() const;

    private:
        void DrawScanline();
        void RenderMode0();
        void RenderMode1();
        void RenderMode2();
        void RenderMode3();
        void RenderMode4();
        void RenderMode5();
        void RenderOBJ();
        void RenderBackground(int bgIndex);
        void RenderAffineBackground(int bgIndex);
        
        // Window helpers
        uint8_t GetWindowMaskForPixel(int x, int y);
        bool IsLayerEnabledAtPixel(int x, int y, int layer); // layer 0-3=BG, 4=OBJ, 5=Effects
        
        // Color effects (blending/brightness)
        void ApplyColorEffects();

        uint16_t ReadRegister(uint32_t offset);
        
        static void OnIOWrite(void* context, uint32_t offset, uint16_t value);
        void HandleIOWrite(uint32_t offset, uint16_t value);

        GBAMemory& memory;
        std::vector<uint32_t> framebuffer;
        // Priority buffer: stores priority (0-3) for each pixel, 4 = backdrop (lowest)
        std::vector<uint8_t> priorityBuffer;
        int cycleCounter;
        int scanline;
        int frameCount;
        
        // Track previous VBlank state for edge detection
        bool prevVBlankState = false;
        
        // Internal Affine Counters (28-bit fixed point)
        int32_t bg2x_internal = 0;
        int32_t bg2y_internal = 0;
        int32_t bg3x_internal = 0;
        int32_t bg3y_internal = 0;
    };

}
