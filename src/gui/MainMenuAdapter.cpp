#include "gui/MainMenuAdapter.h"

#include "gui/MainWindow.h"

namespace AIO::GUI {

MainMenuAdapter::MainMenuAdapter(MainWindow* owner, QWidget* page, const std::vector<QPushButton*>& buttons)
    : ButtonListAdapter(page, buttons), owner_(owner) {
    // Any main-menu-specific initialization here
}

} // namespace AIO::GUI
