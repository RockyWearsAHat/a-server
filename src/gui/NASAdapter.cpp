#include "gui/NASAdapter.h"

#include <QListWidget>
#include <QPushButton>

#include <algorithm>

namespace AIO::GUI {

NASAdapter::NASAdapter(QWidget* page,
                       const std::vector<QPushButton*>& buttons,
                       QListWidget* listWidget)
    : ButtonListAdapter(page, buttons), listWidget_(listWidget) {
    // Keep a stable copy so we can trigger clicks.
    for (auto* b : buttons) {
        buttonsRaw_.push_back(b);
    }
    if (!buttonsRaw_.empty()) {
        backButton_ = buttonsRaw_.back();
    }
}

int NASAdapter::itemCount() const {
    const int listCount = listWidget_ ? listWidget_->count() : 0;
    const int buttonCount = static_cast<int>(buttonsRaw_.size());
    return listCount + buttonCount;
}

void NASAdapter::setHoveredIndex(int index) {
    const int n = itemCount();
    if (n <= 0) return;

    const int clamped = std::clamp(index, 0, n - 1);
    const int listCount = listWidget_ ? listWidget_->count() : 0;

    if (listWidget_ && clamped < listCount) {
        listWidget_->setCurrentRow(clamped);
        listWidget_->setFocus();
        ButtonListAdapter::setHoveredIndex(-1);
        return;
    }

    const int btnIndex = clamped - listCount;
    if (btnIndex >= 0 && btnIndex < static_cast<int>(buttonsRaw_.size()) && buttonsRaw_[btnIndex]) {
        buttonsRaw_[btnIndex]->setFocus();
        ButtonListAdapter::setHoveredIndex(btnIndex);
    }
}

void NASAdapter::activateIndex(int index) {
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

    const int btnIndex = clamped - listCount;
    if (btnIndex >= 0 && btnIndex < static_cast<int>(buttonsRaw_.size()) && buttonsRaw_[btnIndex]) {
        buttonsRaw_[btnIndex]->click();
    }
}

bool NASAdapter::back() {
    if (backButton_) {
        backButton_->click();
        return true;
    }
    return false;
}

} // namespace AIO::GUI
