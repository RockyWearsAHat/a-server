#include <fstream>
#include <iostream>
#include <sstream>

int main() {
  std::ifstream log("debug.log");
  std::string line;
  int lastFrame = -1;
  int sprite0_y = 0;
  int frame_count = 0;

  std::cout << "Frame | Sprite0_Y | Movement" << std::endl;
  std::cout << "------|-----------|----------" << std::endl;

  while (std::getline(log, line)) {
    // Look for frame logging and Y position updates
    // Format: [FRAME] Sprite 0: Y=50 X=22
    if (line.find("FRAME 1230 SPRITE ANALYSIS") != std::string::npos ||
        line.find("FRAME 1231 SPRITE POSITIONS") != std::string::npos) {
      // Extract frame number
      if (line.find("1230") != std::string::npos) {
        lastFrame = 1230;
      } else if (line.find("1231") != std::string::npos) {
        lastFrame = 1231;
      }
    }

    if (lastFrame != -1 && line.find("Sprite 0: Y=") != std::string::npos) {
      // Parse Y coordinate
      size_t pos = line.find("Y=");
      if (pos != std::string::npos) {
        int y = std::stoi(line.substr(pos + 2));
        if (frame_count == 0) {
          sprite0_y = y;
          std::cout << lastFrame << " | " << y << " | --" << std::endl;
        } else {
          int delta = y - sprite0_y;
          std::cout << lastFrame << " | " << y << " | "
                    << (delta > 0 ? "+" : "") << delta << std::endl;
          sprite0_y = y;
        }
        frame_count++;
      }
    }
  }

  return 0;
}
