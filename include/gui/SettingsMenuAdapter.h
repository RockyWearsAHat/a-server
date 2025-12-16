#pragma once

#include "gui/ButtonListAdapter.h"
#include <QWidget>

class QPushButton;

namespace AIO::GUI {

class MainWindow;  // Forward declaration

/**
 * @class SettingsMenuAdapter
 * @brief Navigation adapter for settings menu.
 * 
 * Handles keyboard, mouse, and controller input for settings options.
 * Inherits all navigation functionality from ButtonListAdapter.
 */
class SettingsMenuAdapter final : public ButtonListAdapter {
public:
    /**
     * @brief Construct the settings menu adapter.
     * @param page The widget containing the settings buttons
     * @param buttons Vector of QPushButton pointers for each setting
     * @param owner Pointer to MainWindow for callbacks
     */
    SettingsMenuAdapter(QWidget* page,
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
