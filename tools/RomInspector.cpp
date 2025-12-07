#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <rom_path> <offset> [count]" << std::endl;
        return 1;
    }

    std::string path = argv[1];
    uint32_t offset = std::stoul(argv[2], nullptr, 0);
    int count = (argc > 3) ? std::stoi(argv[3]) : 16;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << path << std::endl;
        return 1;
    }

    file.seekg(offset, std::ios::beg);
    std::vector<uint8_t> buffer(count);
    file.read(reinterpret_cast<char*>(buffer.data()), count);

    std::cout << "Hex dump at 0x" << std::hex << offset << ":" << std::endl;
    for (int i = 0; i < count; ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)buffer[i] << " ";
    }
    std::cout << std::endl;

    return 0;
}
