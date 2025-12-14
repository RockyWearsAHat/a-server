#include <iostream>
#include <fstream>
#include <vector>
#include <cstdio>

int main() {
    const char* savePath = "SMA2.sav";
    
    std::ifstream saveFile(savePath, std::ios::binary | std::ios::ate);
    if (!saveFile.is_open()) {
        std::cerr << "Failed to open " << savePath << std::endl;
        return 1;
    }
    
    std::streamsize saveSize = saveFile.tellg();
    std::cout << "File size: " << saveSize << " bytes" << std::endl;
    
    saveFile.seekg(0, std::ios::beg);
    std::streampos pos = saveFile.tellg();
    std::cout << "Position after seek(0): " << pos << std::endl;
    
    std::vector<uint8_t> saveData(saveSize);
    std::cout << "Vector size: " << saveData.size() << std::endl;
    
    if (saveFile.read(reinterpret_cast<char*>(saveData.data()), saveSize)) {
        std::cout << "Successfully read " << saveSize << " bytes" << std::endl;
        std::cout << "First 32 bytes:" << std::endl;
        for (size_t i = 0; i < 32 && i < saveData.size(); i++) {
            printf("%02x ", saveData[i]);
            if ((i + 1) % 16 == 0) printf("\n");
        }
        std::cout << std::endl;
        
        std::cout << "Bytes at 0x10-0x17 (validation header):" << std::endl;
        for (size_t i = 0x10; i < 0x18 && i < saveData.size(); i++) {
            printf("%02x ", saveData[i]);
        }
        std::cout << std::endl;
    } else {
        std::cerr << "Failed to read file" << std::endl;
        return 1;
    }
    
    return 0;
}
