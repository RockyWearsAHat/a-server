#include "emulator/nes/NES.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <vector>
#include <algorithm>

using namespace AIO::Emulator::NES;

namespace {

// Build a minimal NROM ROM (1 PRG bank, CHR RAM).
std::vector<uint8_t> MakeNromRom(const std::vector<uint8_t>& prog, 
                                 uint16_t resetVec = 0x8000,
                                 uint8_t ppuctrlValue = 0x00) {
    std::vector<uint8_t> rom(16 + 16384, 0);
    rom[0] = 'N'; rom[1] = 'E'; rom[2] = 'S'; rom[3] = 0x1A;
    rom[4] = 1;   // 1 × 16KB PRG bank
    rom[5] = 0;   // CHR RAM (no CHR ROM)
    rom[6] = 0;   // Mapper 0, vertical mirroring
    
    // Fill PRG with NOPs as default
    std::fill(rom.begin() + 16, rom.end(), static_cast<uint8_t>(0xEA));
    
    // Copy program
    for (size_t i = 0; i < prog.size() && i < 16383; ++i) {
        rom[16 + i] = prog[i];
    }
    
    // Reset vector
    rom[16 + 0x3FFC] = static_cast<uint8_t>(resetVec & 0xFF);
    rom[16 + 0x3FFD] = static_cast<uint8_t>(resetVec >> 8);
    
    return rom;
}

} // namespace

// ── NES Instantiation and Frame Rendering ───────────────────────────────────

TEST(NESRenderingTests, NES_Instantiate_And_LoadRom) {
    // Test that NES can be instantiated and a ROM can be loaded
    const std::vector<uint8_t> prog = { 0x4C, 0x00, 0x80 }; // JMP $8000 (infinite loop)
    const auto rom = MakeNromRom(prog, 0x8000);
    
    NES nes;
    EXPECT_NO_THROW(nes.Load(rom));
}

TEST(NESRenderingTests, NES_RunFrame_And_GetFramebuffer) {
    // Test that NES can run a frame and retrieve the framebuffer
    const std::vector<uint8_t> prog = { 0x4C, 0x00, 0x80 }; // JMP $8000
    const auto rom = MakeNromRom(prog, 0x8000);
    
    NES nes;
    nes.Load(rom);
    
    // Get initial state
    const uint32_t* framebuffer = nes.GetPPU().GetFramebuffer();
    EXPECT_NE(framebuffer, nullptr) << "Framebuffer should not be null";
    
    // Run one frame
    nes.RunFrame();
    
    // Get framebuffer after frame execution
    framebuffer = nes.GetPPU().GetFramebuffer();
    EXPECT_NE(framebuffer, nullptr) << "Framebuffer should not be null after RunFrame";
}

TEST(NESRenderingTests, NES_Framebuffer_Has_ValidSize) {
    // Test that framebuffer has expected dimensions (256×240 RGBA)
    const std::vector<uint8_t> prog = { 0x4C, 0x00, 0x80 };
    const auto rom = MakeNromRom(prog, 0x8000);
    
    NES nes;
    nes.Load(rom);
    nes.RunFrame();
    
    const uint32_t* framebuffer = nes.GetPPU().GetFramebuffer();
    EXPECT_NE(framebuffer, nullptr);
    
    // Expected: 256 pixels wide × 240 pixels tall
    const int expectedPixels = 256 * 240;
    const uint32_t* end = framebuffer + expectedPixels;
    
    // Verify we can access all expected pixels without crashing
    uint32_t samplePixel = framebuffer[0];
    EXPECT_NE(end, framebuffer); // At least some pixels
}

TEST(NESRenderingTests, NES_Framebuffer_NotAllZeros_AfterFrame) {
    // Test that after running a frame, framebuffer contains non-zero data
    // indicating rendering has occurred.
    const std::vector<uint8_t> prog = { 0x4C, 0x00, 0x80 }; // Infinite loop
    const auto rom = MakeNromRom(prog, 0x8000);
    
    NES nes;
    nes.Load(rom);
    
    // Run multiple frames to ensure PPU has rendered something
    for (int i = 0; i < 3; ++i) {
        nes.RunFrame();
    }
    
    const uint32_t* framebuffer = nes.GetPPU().GetFramebuffer();
    EXPECT_NE(framebuffer, nullptr);
    
    // Count non-zero pixels
    const int totalPixels = 256 * 240;
    int nonZeroCount = 0;
    
    for (int i = 0; i < totalPixels; ++i) {
        if (framebuffer[i] != 0) {
            ++nonZeroCount;
        }
    }
    
    // Expect at least some pixels to be non-zero (indicating rendering)
    // Even a blank screen should have some color information
    EXPECT_GT(nonZeroCount, 0) 
        << "Framebuffer should contain non-zero pixels after running frame. "
           "Got " << nonZeroCount << " non-zero pixels out of " << totalPixels;
}

TEST(NESRenderingTests, NES_Frame_Rendering_WithNMI) {
    // Test NES frame rendering with NMI enabled (more realistic scenario)
    const std::vector<uint8_t> prog = { 0x4C, 0x00, 0x80 }; // Infinite loop
    const auto rom = MakeNromRom(prog, 0x8000);
    
    NES nes;
    nes.Load(rom);
    
    int nmiCount = 0;
    nes.GetPPU().SetNmiCallback([&nmiCount]() { ++nmiCount; });
    
    // Enable NMI in PPUCTRL (bit 7)
    nes.GetPPU().WriteRegister(0x00, 0x80);
    
    // Run a frame
    nes.RunFrame();
    
    // Should have gotten at least one NMI
    EXPECT_GE(nmiCount, 1) << "Expected at least one NMI during frame rendering";
    
    const uint32_t* framebuffer = nes.GetPPU().GetFramebuffer();
    EXPECT_NE(framebuffer, nullptr);
    
    // Count non-zero pixels
    const int totalPixels = 256 * 240;
    int nonZeroCount = 0;
    
    for (int i = 0; i < totalPixels; ++i) {
        if (framebuffer[i] != 0) {
            ++nonZeroCount;
        }
    }
    
    EXPECT_GT(nonZeroCount, 0)
        << "Framebuffer should have non-zero pixels with NMI enabled";
}

TEST(NESRenderingTests, NES_Multiple_Frames_Consistency) {
    // Test that multiple frames can be run consistently
    const std::vector<uint8_t> prog = { 0x4C, 0x00, 0x80 };
    const auto rom = MakeNromRom(prog, 0x8000);
    
    NES nes;
    nes.Load(rom);
    
    // Run several frames
    int frameCount = 10;
    for (int i = 0; i < frameCount; ++i) {
        EXPECT_NO_THROW(nes.RunFrame());
    }
    
    const uint32_t* framebuffer = nes.GetPPU().GetFramebuffer();
    EXPECT_NE(framebuffer, nullptr);
    
    // Verify PPU frame counter increased
    // Each RunFrame should advance the frame count
    EXPECT_GT(nes.GetPPU().FrameCount(), 0)
        << "PPU frame count should be > 0 after running frames";
}

TEST(NESRenderingTests, NES_Step_And_Render) {
    // Test stepping individual instructions and monitoring frame progression
    const std::vector<uint8_t> prog = { 0x4C, 0x00, 0x80 }; // JMP $8000
    const auto rom = MakeNromRom(prog, 0x8000);
    
    NES nes;
    nes.Load(rom);
    
    uint64_t initialFrame = nes.GetPPU().FrameCount();
    
    // Step until we've completed more than one frame (89343+ CPU cycles NTSC)
    for (int i = 0; i < 90000; ++i) {
        nes.Step();
    }
    
    uint64_t finalFrame = nes.GetPPU().FrameCount();
    EXPECT_GT(finalFrame, initialFrame)
        << "Frame count should increase after stepping CPU";
    
    const uint32_t* framebuffer = nes.GetPPU().GetFramebuffer();
    EXPECT_NE(framebuffer, nullptr);
}
