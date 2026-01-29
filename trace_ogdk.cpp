#include <iostream>
#include <fstream>
#include <cstdint>
#include <iomanip>

int main() {
    // Read current VRAM dump if available or ROM
    std::ifstream rom("OG-DK.gba", std::ios::binary);
    if (!rom) { std::cerr << "Cannot open ROM\n"; return 1; }
    
    rom.seekg(0, std::ios::end);
    size_t size = rom.tellg();
    rom.seekg(0);
    
    std::vector<uint8_t> data(size);
    rom.read(reinterpret_cast<char*>(data.data()), size);
    
    std::cout << "ROM size: " << size << " bytes\n";
    
    // Look for typical NES CHR patterns - tiles at 16-byte or 8-byte alignments
    // Classic NES games embed NES tiles which are 16 bytes each (8x8, 2bpp)
    // Check around common tile data areas
    
    return 0;
}
