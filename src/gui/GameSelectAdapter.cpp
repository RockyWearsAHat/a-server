#include "gui/GameSelectAdapter.h"
#include "gui/MainWindow.h"

#include <algorithm>
#include <QListWidget>

namespace AIO::GUI {

GameSelectAdapter::GameSelectAdapter(QWidget* page,
                                     const std::vector<QPushButton*>& buttons,
                                     MainWindow* owner,
                                     QListWidget* listWidget)
    : ButtonListAdapter(page, buttons), owner_(owner), listWidget_(listWidget) {
    const auto& btns = getButtons();
    if (!btns.empty()) {
        backButton_ = btns.front();
    }
}

int GameSelectAdapter::itemCount() const {
    const int listCount = listWidget_ ? listWidget_->count() : 0;
    const int backCount = backButton_ ? 1 : 0;
    return listCount + backCount;
}

void GameSelectAdapter::setHoveredIndex(int index) {
    const int n = itemCount();
    if (n <= 0) return;

    const int clamped = std::clamp(index, 0, n - 1);
    const int listCount = listWidget_ ? listWidget_->count() : 0;

    if (listWidget_ && clamped < listCount) {
        listWidget_->setCurrentRow(clamped);
        listWidget_->setFocus();
        // Ensure the Back button isn't visually selected.
        ButtonListAdapter::setHoveredIndex(-1);
        return;
    }

    // Back item selected.
    if (backButton_) {
        backButton_->setFocus();
        ButtonListAdapter::setHoveredIndex(0);
    }
}

void GameSelectAdapter::activateIndex(int index) {
    const int n = itemCount();
    if (n <= 0) return;

    const int clamped = std::clamp(index, 0, n - 1);
    const int listCount = listWidget_ ? listWidget_->count() : 0;

    if (listWidget_ && clamped < listCount) {
        listWidget_->setCurrentRow(clamped);
        if (auto* item = listWidget_->item(clamped)) {
            emit listWidget_->itemActivated(item);
        }
        return;
    }

    // Activate Back.
    back();
}

bool GameSelectAdapter::back() {
    if (owner_) {
        owner_->goToEmulatorSelect();
        return true;
    }
    return false;
}

} // namespace AIO::GUI
