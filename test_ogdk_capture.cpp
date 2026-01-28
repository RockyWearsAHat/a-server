// Capture a frame from OG-DK to see what's actually rendering
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "emulator/gba/GBA.h"

// Write a simple PPM image
void writePPM(const char *filename, const uint32_t *fb, int width, int height) {
  std::ofstream f(filename, std::ios::binary);
  f << "P6\n" << width << " " << height << "\n255\n";
  for (int i = 0; i < width * height; i++) {
    uint32_t pixel = fb[i];
    uint8_t r = (pixel >> 16) & 0xFF;
    uint8_t g = (pixel >> 8) & 0xFF;
    uint8_t b = pixel & 0xFF;
    f.write((char *)&r, 1);
    f.write((char *)&g, 1);
    f.write((char *)&b, 1);
  }
}

int main() {
  AIO::Emulator::GBA::GBA gba;

  if (!gba.LoadROM("/Users/alexwaldmann/Desktop/AIO Server/OG-DK.gba")) {
    printf("Failed to load ROM\n");
    return 1;
  }

  printf("Running for 120 frames...\n");
  const int CYCLES_PER_FRAME = 280896;
  int cyclesRun = 0;
  int frame = 0;

  // Run for 120 frames
  while (frame < 120) {
    int c = gba.Step();
    cyclesRun += c;
    if (cyclesRun >= CYCLES_PER_FRAME) {
      cyclesRun -= CYCLES_PER_FRAME;
      frame++;
    }
  }

  printf("Capturing frame %d...\n", frame);

  // Get framebuffer via PPU
  const auto &fb = gba.GetPPU().GetFramebuffer();
  writePPM("ogdk_frame120.ppm", fb.data(), 240, 160);
  printf("Saved to ogdk_frame120.ppm\n");

  // Also capture VRAM state
  auto &mem = gba.GetMemory();
  printf("\n=== PPU Register State ===\n");
  uint16_t dispcnt = mem.Read16(0x04000000);
  printf("DISPCNT (0x04000000) = 0x%04X\n", dispcnt);
  printf("  Mode: %d\n", dispcnt & 7);
  printf("  BG0: %s\n", (dispcnt & 0x100) ? "ON" : "OFF");
  printf("  BG1: %s\n", (dispcnt & 0x200) ? "ON" : "OFF");
  printf("  BG2: %s\n", (dispcnt & 0x400) ? "ON" : "OFF");
  printf("  BG3: %s\n", (dispcnt & 0x800) ? "ON" : "OFF");
  printf("  OBJ: %s\n", (dispcnt & 0x1000) ? "ON" : "OFF");

  // Check palette
  printf("\n=== First 16 palette entries (BG) ===\n");
  for (int i = 0; i < 16; i++) {
    uint16_t color = mem.Read16(0x05000000 + i * 2);
    printf("  [%2d] = 0x%04X (R=%d G=%d B=%d)\n", i, color, color & 0x1F,
           (color >> 5) & 0x1F, (color >> 10) & 0x1F);
  }

  // Check BG control
  printf("\n=== BG Control ===\n");
  for (int i = 0; i < 4; i++) {
    uint16_t bgcnt = mem.Read16(0x04000008 + i * 2);
    printf("BG%dCNT = 0x%04X (Priority=%d, CharBase=0x%X, TileBase=0x%X)\n", i,
           bgcnt, bgcnt & 3, ((bgcnt >> 2) & 3) * 0x4000,
           ((bgcnt >> 8) & 0x1F) * 0x800);
  }

  return 0;
}
