#pragma once

#include "gui/ButtonListAdapter.h"
#include <QWidget>

class QPushButton;

namespace AIO::GUI {

class MainWindow;  // Forward declaration

/**
 * @class EmulatorSelectAdapter
 * @brief Navigation adapter for emulator selection menu.
 * 
 * Handles keyboard, mouse, and controller input for selecting between
 * different emulators (GBA, Switch, etc.).
 * 
 * Inherits all navigation functionality from ButtonListAdapter.
 */
class EmulatorSelectAdapter final : public ButtonListAdapter {
public:
    /**
     * @brief Construct the emulator select adapter.
     * @param page The widget containing the emulator selection buttons
     * @param buttons Vector of QPushButton pointers for each emulator
     * @param owner Pointer to MainWindow for callbacks
     */
    EmulatorSelectAdapter(QWidget* page, 
                         const std::vector<QPushButton*>& buttons,
                         MainWindow* owner);

    /**
     * @brief Handle back button press.
     * @return true if navigation was successful
     */
    bool back() override;

private:
    MainWindow* owner_;
};

} // namespace AIO::GUI
