#include "gui/EmulatorSettingsAdapter.h"

#include "gui/MainWindow.h"

namespace AIO::GUI {

EmulatorSettingsAdapter::EmulatorSettingsAdapter(
    QWidget *page, const std::vector<QPushButton *> &buttons, MainWindow *owner)
    : ButtonListAdapter(page, buttons), owner_(owner) {}

bool EmulatorSettingsAdapter::back() {
  if (owner_) {
    owner_->closeEmulatorSettings();
    return true;
  }
  return false;
}

} // namespace AIO::GUI
