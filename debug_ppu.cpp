// Temporary debug - add this code to DrawScanline after mode detection
static int framesSinceLog = 0;
if (scanline == 0 && ++framesSinceLog > 60) {
    framesSinceLog = 0;
    std::cout << "[PPU Debug] DISPCNT=0x" << std::hex << dispcnt 
              << " Mode=" << std::dec << mode
              << " BG0=" << ((dispcnt >> 8) & 1)
              << " BG1=" << ((dispcnt >> 9) & 1)
              << " BG2=" << ((dispcnt >> 10) & 1)
              << " BG3=" << ((dispcnt >> 11) & 1)
              << " OBJ=" << ((dispcnt >> 12) & 1)
              << " backdrop=0x" << std::hex << backdropARGB
              << std::endl;
}
