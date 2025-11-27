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
        void RenderMode2(); // Add this
        void RenderOBJ(); // Add this
        void RenderBackground(int bgIndex);
        void RenderAffineBackground(int bgIndex); // Add this

        uint16_t ReadRegister(uint32_t offset);

        GBAMemory& memory;
        std::vector<uint32_t> framebuffer;
        int cycleCounter;
        int scanline;
        int frameCount;
    };

}
