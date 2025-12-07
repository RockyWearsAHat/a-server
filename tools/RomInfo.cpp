#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <rom_path>" << std::endl;
        return 1;
    }

    std::string path = argv[1];
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << path << std::endl;
        return 1;
    }

    std::vector<uint8_t> buffer(0xC0);
    file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());

    if (buffer.size() < 0xC0) {
        std::cerr << "File too small to be a GBA ROM" << std::endl;
        return 1;
    }

    std::string gameTitle(reinterpret_cast<char*>(&buffer[0xA0]), 12);
    std::string gameCode(reinterpret_cast<char*>(&buffer[0xAC]), 4);
    std::string makerCode(reinterpret_cast<char*>(&buffer[0xB0]), 2);

    std::cout << "File: " << path << std::endl;
    std::cout << "Title: " << gameTitle << std::endl;
    std::cout << "Game Code: " << gameCode << std::endl;
    std::cout << "Maker Code: " << makerCode << std::endl;

    return 0;
}
