#include "gui/ButtonListAdapter.h"

#include <QApplication>
#include <QStyle>
#include <QTimer>

namespace AIO::GUI {

// Global timestamp counter to track which interaction happened most recently
static int g_timestamp = 0;

static QString mergeStyleSheetKeepingBase(const QString& base, const QString& selectionFragment) {
    // Keep any existing custom styling (e.g., emulator buttons' left accent).
    // Replace/refresh only our selection fragment.
    static const QString kBegin = "/*AIO_SELECTION_BEGIN*/";
    static const QString kEnd = "/*AIO_SELECTION_END*/";

    QString merged = base;
    const int beginIdx = merged.indexOf(kBegin);
    if (beginIdx >= 0) {
        const int endIdx = merged.indexOf(kEnd, beginIdx);
        if (endIdx >= 0) {
            merged.remove(beginIdx, (endIdx + kEnd.size()) - beginIdx);
        }
    }

    if (!selectionFragment.isEmpty()) {
        if (!merged.isEmpty() && !merged.endsWith('\n')) merged.append('\n');
        merged.append(kBegin);
        merged.append('\n');
        merged.append(selectionFragment);
        if (!selectionFragment.endsWith('\n')) merged.append('\n');
        merged.append(kEnd);
        merged.append('\n');
    }

    return merged;
}

ButtonListAdapter::ButtonListAdapter(QWidget* page, const std::vector<QPushButton*>& buttons)
    : page_(page) {
    buttons_.reserve(buttons.size());
    for (auto* b : buttons) {
        buttons_.push_back(b);
        if (b) {
            // Initialize the property to "false" (string) for QSS matching
            b->setProperty("aio_selected", "false");

            // Preserve any author-provided inline styles and allow us to layer selection on top.
            // This prevents losing unique button looks (e.g., emulator buttons).
            b->setProperty("aio_baseStyle", b->styleSheet());
        }
    }
}

int ButtonListAdapter::itemCount() const {
    int n = 0;
    for (const auto& b : buttons_) {
        if (b) ++n;
    }
    return n;
}

int ButtonListAdapter::indexOfButton(const QPushButton* button) const {
    for (int i = 0; i < static_cast<int>(buttons_.size()); ++i) {
        if (buttons_[i].data() == button) {
            return i;
        }
    }
    return -1;
}

void ButtonListAdapter::setHoveredIndex(int index) {
    hovered_ = index;
    // Note: Don't update lastControllerIndex_ here - only in onControllerNavigation()
    applyHovered();
}

void ButtonListAdapter::setMouseHoverIndex(int index) {
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

void ButtonListAdapter::clearMouseHover() {
    if (mouseHover_ >= 0) {
        mouseHover_ = -1;
        applyHovered();
    }
}

void ButtonListAdapter::onControllerNavigation(int newIndex) {
    // Called when user actually navigates with controller
    hovered_ = newIndex;
    if (newIndex >= 0) {
        lastControllerIndex_ = newIndex;
        controllerIndexTimestamp_ = ++g_timestamp;
    }
    applyHovered();
}

void ButtonListAdapter::activateIndex(int index) {
    hovered_ = index;
    applyHovered();

    int idx = 0;
    for (auto& b : buttons_) {
        if (!b) continue;
        if (idx == hovered_) {
            b->click();
            break;
        }
        ++idx;
    }
}

bool ButtonListAdapter::back() {
    // Default implementation - subclasses can override
    return false;
}

void ButtonListAdapter::saveControllerIndexBeforeMouse() {
    // Save current controller index for resuming after mouse use
    if (hovered_ >= 0) {
        lastControllerIndex_ = hovered_;
        controllerIndexTimestamp_ = ++g_timestamp;
    }
}

void ButtonListAdapter::applyHovered() {
    applyHoveredInternal();
}

void ButtonListAdapter::applyHoveredInternal() {
    // One visual state only: mouse hover overrides visuals while active; otherwise controller selection.
    visualSelected_ = (mouseHover_ >= 0) ? mouseHover_ : hovered_;
    const int displayIndex = visualSelected_;

    // When switching pages via controller (A button), the first directional press selects index 0.
    // Qt stylesheets sometimes lag applying dynamic-property selectors on that first selection.
    // We'll handle that deterministically via a next-tick repolish of the hovered button.

    // Process all buttons, explicitly setting all properties.
    for (int idx = 0; idx < static_cast<int>(buttons_.size()); ++idx) {
        auto& b = buttons_[idx];
        if (!b) continue;

        const bool shouldBeSelected = (idx == displayIndex);

        // Set property as string for QSS matching ("true" or "false").
        const char* value = shouldBeSelected ? "true" : "false";
        b->setProperty("aio_selected", value);

        // Hard guarantee: force the outline for the selected item.
        // Some Qt stylesheet caching paths will apply background-color updates but skip border
        // (most noticeable on the first item when controller selection begins).
        // A per-widget override is deterministic and scoped only to the selected button.
        const QString base = b->property("aio_baseStyle").toString();
        const QString selectionFragment = shouldBeSelected
            ? QStringLiteral(
                "border: 3px solid rgba(170, 179, 197, 0.95);\n"
                "background-color: rgba(255, 255, 255, 0.06);\n"
              )
            : QString();
        b->setStyleSheet(mergeStyleSheetKeepingBase(base, selectionFragment));

        // Do NOT rely on focus for visuals; focus can introduce competing style states.
        // Selection visuals should be purely driven by aio_selected.

        // Force style re-evaluation and repaint.
        // Keeping it per-button avoids page-wide flicker.
        if (b->style()) {
            b->style()->unpolish(b);
            b->style()->polish(b);
        }
        b->update();
    }

    // Repolish only the hovered button on the next tick.
    // Repolishing the entire page every time can race with page transitions and cause flicker.


    // Qt stylesheet evaluation for dynamic properties can occasionally lag one tick,
    // most visible on the first item when controller navigation begins.
    // Schedule a follow-up polish on the next event loop turn, and explicitly repolish
    // the currently hovered button (index 0 in the common failing case).
    const int previous = lastAppliedVisualIndex_;
    lastAppliedVisualIndex_ = displayIndex;
    if (displayIndex < 0) return;

    // Only do the expensive next-tick repolish when it matters:
    // - First selection on a page (-1 -> 0)
    // - Or when the hovered index actually changed
    const bool needsTickle = (previous != displayIndex) || (previous < 0 && displayIndex == 0);
    if (!needsTickle) return;

    QPointer<QPushButton> hoveredBtnGuard;
    int logicalIdx = 0;
    for (auto& b : buttons_) {
        if (!b) continue;
        if (logicalIdx == displayIndex) {
            hoveredBtnGuard = b;
            break;
        }
        ++logicalIdx;
    }

    QTimer::singleShot(0, [hoveredBtnGuard]() {
        if (!hoveredBtnGuard || !hoveredBtnGuard->style()) return;
        hoveredBtnGuard->style()->unpolish(hoveredBtnGuard);
        hoveredBtnGuard->style()->polish(hoveredBtnGuard);
        hoveredBtnGuard->update();
    });
}

} // namespace AIO::GUI
