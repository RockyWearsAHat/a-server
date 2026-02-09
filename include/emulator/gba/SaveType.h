#pragma once

namespace AIO::Emulator::GBA {

enum class SaveType {
  Auto,
  None,
  SRAM,
  Flash512,
  Flash1M,
  EEPROM_4K,
  EEPROM_64K
};

} // namespace AIO::Emulator::GBA
