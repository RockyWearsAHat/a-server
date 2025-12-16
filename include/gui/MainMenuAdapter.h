#pragma once

#include "gui/ButtonListAdapter.h"

namespace AIO::GUI {

class MainWindow;

/**
 * Main menu adapter - extends ButtonListAdapter for the main menu.
 * Currently just inherits all functionality from ButtonListAdapter,
 * but kept separate for future main-menu-specific customizations.
 */
class MainMenuAdapter final : public ButtonListAdapter {
public:
    MainMenuAdapter(MainWindow* owner, QWidget* page, const std::vector<QPushButton*>& buttons);

private:
    class MainWindow* owner_;
};

} // namespace AIO::GUI
