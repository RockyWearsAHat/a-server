#include "gui/SettingsMenuAdapter.h"
#include "gui/MainWindow.h"

namespace AIO::GUI {

SettingsMenuAdapter::SettingsMenuAdapter(QWidget* page,
                                         const std::vector<QPushButton*>& buttons,
                                         MainWindow* owner)
    : ButtonListAdapter(page, buttons), owner_(owner) {}

bool SettingsMenuAdapter::back() {
    if (owner_) {
        owner_->goToMainMenu();
        return true;
    }
    return false;
}

} // namespace AIO::GUI
