// Manually decompress and analyze the IWRAM code from OG-DK
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

// Simple LZ77 decompression (GBA BIOS style)
std::vector<uint8_t> decompress_lz77(const uint8_t *src) {
  // Header: byte 0 = 0x10 (LZ77 marker), bytes 1-3 = size (little endian)
  uint32_t size = src[1] | (src[2] << 8) | (src[3] << 16);
  printf("LZ77: Decompressing %u bytes\n", size);

  std::vector<uint8_t> dst(size);
  uint32_t srcPos = 4;
  uint32_t dstPos = 0;

  while (dstPos < size) {
    uint8_t flags = src[srcPos++];

    for (int i = 0; i < 8 && dstPos < size; i++) {
      if (flags & 0x80) {
        // Compressed block
        uint8_t byte1 = src[srcPos++];
        uint8_t byte2 = src[srcPos++];

        int disp = ((byte1 & 0x0F) << 8) | byte2;
        int len = (byte1 >> 4) + 3;

        uint32_t srcOff = dstPos - disp - 1;
        for (int j = 0; j < len && dstPos < size; j++) {
          dst[dstPos++] = dst[srcOff + j];
        }
      } else {
        // Uncompressed byte
        dst[dstPos++] = src[srcPos++];
      }
      flags <<= 1;
    }
  }

  return dst;
}

int main() {
  // Read ROM
  std::ifstream rom("OG-DK.gba", std::ios::binary);
  if (!rom) {
    printf("Failed to open ROM\n");
    return 1;
  }

  std::vector<uint8_t> romData(std::istreambuf_iterator<char>(rom), {});
  printf("ROM size: %zu bytes\n", romData.size());

  // Decompress from 0x5FF4
  uint32_t lz77Offset = 0x5FF4;
  auto decompressed = decompress_lz77(&romData[lz77Offset]);

  printf("\n=== Decompressed IWRAM code (276 bytes) ===\n");

  // Dump as ARM instructions
  printf("\nCode region (0x00-0x7F):\n");
  for (uint32_t i = 0; i < 0x80 && i < decompressed.size(); i += 4) {
    uint32_t op = decompressed[i] | (decompressed[i + 1] << 8) |
                  (decompressed[i + 2] << 16) | (decompressed[i + 3] << 24);
    printf("  [0x%04X] 0x%08X", i, op);

    // Annotate PC-relative loads
    if ((op & 0x0F7F0000) == 0x059F0000) { // LDR Rd, [PC, #imm]
      uint32_t offset = op & 0xFFF;
      uint32_t poolAddr = i + 8 + offset; // PC+8+offset
      printf("  ; LDR from pool at 0x%04X", poolAddr);
      if (poolAddr < decompressed.size() - 3) {
        uint32_t poolVal = decompressed[poolAddr] |
                           (decompressed[poolAddr + 1] << 8) |
                           (decompressed[poolAddr + 2] << 16) |
                           (decompressed[poolAddr + 3] << 24);
        printf(" = 0x%08X", poolVal);
      }
    }
    printf("\n");
  }

  // Dump literal pool (usually at end of code)
  printf("\nLiteral pool region (0x40-0x114):\n");
  for (uint32_t i = 0x40; i < decompressed.size(); i += 4) {
    uint32_t val = decompressed[i] | (decompressed[i + 1] << 8) |
                   (decompressed[i + 2] << 16) | (decompressed[i + 3] << 24);
    printf("  [0x%04X] 0x%08X", i, val);

    // Annotate known addresses
    if ((val & 0xFF000000) == 0x08000000)
      printf("  ; ROM");
    else if ((val & 0xFF000000) == 0x06000000)
      printf("  ; VRAM");
    else if ((val & 0xFF000000) == 0x05000000)
      printf("  ; PALRAM");
    else if ((val & 0xFF000000) == 0x03000000)
      printf("  ; IWRAM");
    else if ((val & 0xFF000000) == 0x04000000)
      printf("  ; I/O");
    printf("\n");
  }

  // Specifically check offset 0x48 (where first LDR loads from)
  printf("\n=== Key literal pool values ===\n");
  printf("Offset 0x48 (for LDR R12, [PC+0x40]): ");
  if (0x48 < decompressed.size() - 3) {
    uint32_t val = decompressed[0x48] | (decompressed[0x49] << 8) |
                   (decompressed[0x4A] << 16) | (decompressed[0x4B] << 24);
    printf("0x%08X\n", val);
  }

  return 0;
}
