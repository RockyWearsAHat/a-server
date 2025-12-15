#include "gui/MainMenuAdapter.h"

#include "gui/MainWindow.h"

#include <QEvent>
#include <QHoverEvent>
#include <QApplication>
#include <iostream>

namespace AIO::GUI {

// Global timestamp counter to track which interaction happened most recently
static int g_timestamp = 0;

MainMenuAdapter::MainMenuAdapter(MainWindow* owner, QWidget* page, const std::vector<QPushButton*>& buttons)
    : owner_(owner), page_(page) {
    buttons_.reserve(buttons.size());
    for (auto* b : buttons) {
        buttons_.push_back(b);
        if (b) {
            b->setAttribute(Qt::WA_Hover, true);
            // Initialize the property to "false" (string) for QSS matching
            b->setProperty("aio_hovered", "false");
        }
    }
}

int MainMenuAdapter::itemCount() const {
    int n = 0;
    for (const auto& b : buttons_) {
        if (b) ++n;
    }
    return n;
}

int MainMenuAdapter::indexOfButton(const QPushButton* button) const {
    if (!button) return -1;
    // Return the actual vector index, matching how applyHovered uses indices
    for (int idx = 0; idx < static_cast<int>(buttons_.size()); ++idx) {
        if (buttons_[idx] == button) return idx;
    }
    return -1;
}

void MainMenuAdapter::applyHovered() {
    // Display mouseHover_ if set, otherwise display hovered_ (controller selection)
    int displayIndex = (mouseHover_ >= 0) ? mouseHover_ : hovered_;
    
    // Process all buttons, explicitly setting all properties
    for (int idx = 0; idx < static_cast<int>(buttons_.size()); ++idx) {
        auto& b = buttons_[idx];
        if (!b) continue;
        
        bool shouldBeHovered = (idx == displayIndex);
        
        // Set property as string for QSS matching ("true" or "false")
        b->setProperty("aio_hovered", shouldBeHovered ? "true" : "false");
        
        // Always force style re-evaluation and repaint
        // This ensures buttons get styled consistently, especially the first button
        if (b->style()) {
            b->style()->unpolish(b);
            b->style()->polish(b);
        }
        b->repaint();
    }
    
    // Process pending events to ensure stylesheets are evaluated
    // This is important for the first button styling on initial controller input
    QApplication::processEvents();
}

void MainMenuAdapter::setMouseHoverIndex(int index) {
    if (mouseHover_ != index) {
        mouseHover_ = index;
        // Track the last position mouse hovered (for resuming controller navigation)
        if (index >= 0) {
            lastMouseHover_ = index;
            mouseHoverTimestamp_ = ++g_timestamp;
        }
        applyHovered();
    }
}

void MainMenuAdapter::clearMouseHover() {
    if (mouseHover_ >= 0) {
        mouseHover_ = -1;
        applyHovered();
    }
}


void MainMenuAdapter::setHoveredIndex(int index) {
    std::cout << "[HOVERED] setHoveredIndex(" << index << ") was hovered_=" << hovered_ << std::endl;
    hovered_ = index;
    // Note: Don't update lastControllerIndex_ here - only in onControllerNavigation()
    applyHovered();
}

void MainMenuAdapter::onControllerNavigation(int newIndex) {
    // Called when user actually navigates with controller
    hovered_ = newIndex;
    if (newIndex >= 0) {
        lastControllerIndex_ = newIndex;
        controllerIndexTimestamp_ = ++g_timestamp;
    }
    applyHovered();
}

void MainMenuAdapter::activateIndex(int index) {
    hovered_ = index;
    applyHovered();

    int idx = 0;
    for (auto& b : buttons_) {
        if (!b) continue;
        if (idx == hovered_) {
            b->click();
            return;
        }
        ++idx;
    }
}

bool MainMenuAdapter::back() {
    // Already at root.
    return true;
}

void MainMenuAdapter::saveControllerIndexBeforeMouse() {
    // Save where controller was before switching to mouse
    if (hovered_ >= 0) {
        lastControllerIndex_ = hovered_;
        std::cout << "[STATE] Saved controller index " << lastControllerIndex_ << " before switching to mouse" << std::endl;
    }
}

} // namespace AIO::GUI

