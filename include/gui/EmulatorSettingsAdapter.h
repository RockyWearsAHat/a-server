#pragma once

#include "gui/ButtonListAdapter.h"

#include <QWidget>

class QPushButton;

namespace AIO::GUI {

class MainWindow;

class EmulatorSettingsAdapter final : public ButtonListAdapter {
public:
  EmulatorSettingsAdapter(QWidget *page,
                          const std::vector<QPushButton *> &buttons,
                          MainWindow *owner);

  bool back() override;

private:
  MainWindow *owner_;
};

} // namespace AIO::GUI
