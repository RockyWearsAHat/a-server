#include "gui/EmulatorSelectAdapter.h"
#include "gui/MainWindow.h"

namespace AIO::GUI {

EmulatorSelectAdapter::EmulatorSelectAdapter(QWidget* page,
                                             const std::vector<QPushButton*>& buttons,
                                             MainWindow* owner)
    : ButtonListAdapter(page, buttons), owner_(owner) {}

bool EmulatorSelectAdapter::back() {
    if (owner_) {
        owner_->goToMainMenu();
        return true;
    }
    return false;
}

} // namespace AIO::GUI
