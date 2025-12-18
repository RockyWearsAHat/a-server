#pragma once

#include "gui/ButtonListAdapter.h"

#include <QPointer>
#include <QWidget>
#include <vector>

class QPushButton;
class QListWidget;

namespace AIO::GUI {

// Navigation adapter for the native NAS page.
//
// Provides controller/keyboard navigation across the directory list plus a row
// of action buttons (Up/Refresh/etc.).
class NASAdapter final : public ButtonListAdapter {
public:
    NASAdapter(QWidget* page,
               const std::vector<QPushButton*>& buttons,
               QListWidget* listWidget);

    int itemCount() const override;
    void setHoveredIndex(int index) override;
    void activateIndex(int index) override;
    bool back() override;

private:
    QPointer<QListWidget> listWidget_;
    std::vector<QPointer<QPushButton>> buttonsRaw_;
    QPointer<QPushButton> backButton_;
};

} // namespace AIO::GUI
