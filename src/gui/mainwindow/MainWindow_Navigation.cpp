#include "gui/MainWindow.h"

#include "gui/ButtonListAdapter.h"
#include "gui/EmulatorSelectAdapter.h"
#include "gui/GameSelectAdapter.h"
#include "gui/MainMenuAdapter.h"
#include "gui/NASAdapter.h"
#include "gui/SettingsMenuAdapter.h"

#include "input/InputManager.h"

#include <QApplication>
#include <QCursor>
#include <QElapsedTimer>
#include <QEvent>

#include <iostream>

namespace AIO {
namespace GUI {

void MainWindow::setupNavigation() {
    navTimer = new QTimer(this);
    connect(navTimer, &QTimer::timeout, this, [this]() {
        QWidget* current = stackedWidget ? stackedWidget->currentWidget() : nullptr;
        const bool inEmu = (current == emulatorPage) && emulatorRunning;
        AIO::Input::InputManager::instance().setActiveContext(
            inEmu ? AIO::Input::InputContext::Emulator : AIO::Input::InputContext::UI);

        const auto snapshot = AIO::Input::InputManager::instance().updateSnapshot();
        const auto frame = actionMapper.update(snapshot);

        static bool gUiNavDebug = false;
        static bool gUiNavDebugInit = false;
        if (!gUiNavDebugInit) {
            gUiNavDebug = (qEnvironmentVariableIntValue("AIO_UI_NAV_DEBUG") != 0);
            gUiNavDebugInit = true;
        }
        if (gUiNavDebug && frame.primary != AIO::GUI::UIAction::None) {
            auto actionName = [&](AIO::GUI::UIAction a) -> const char* {
                switch (a) {
                    case AIO::GUI::UIAction::Up: return "Up";
                    case AIO::GUI::UIAction::Down: return "Down";
                    case AIO::GUI::UIAction::Left: return "Left";
                    case AIO::GUI::UIAction::Right: return "Right";
                    case AIO::GUI::UIAction::Select: return "Select";
                    case AIO::GUI::UIAction::Back: return "Back";
                    case AIO::GUI::UIAction::Home: return "Home";
                    default: return "None";
                }
            };
            std::cout << "[UI_NAV] action=" << actionName(frame.primary)
                      << " source=" << (int)frame.source
                      << " page=" << (stackedWidget ? stackedWidget->currentIndex() : -1)
                      << std::endl;
        }

        // Detect if controller/keyboard input occurred
        const bool hasControllerInput =
            (frame.source == AIO::GUI::UIInputSource::Controller ||
             frame.source == AIO::GUI::UIInputSource::Keyboard) &&
            frame.primary != AIO::GUI::UIAction::None;

        if (hasControllerInput) {
            // Controller input detected - switch to controller mode
            if (currentInputMode != InputMode::Controller) {
                std::cout << "[INPUT_MODE] Detected controller input, switching to Controller mode" << std::endl;
                currentInputMode = InputMode::Controller;

                // Hide cursor on main window and all child widgets
                QApplication::setOverrideCursor(Qt::BlankCursor);
                std::cout << "[CURSOR] Set to BlankCursor globally" << std::endl;

                lastHoveredButton = nullptr; // Clear mouse hover tracking

                // Clear mouse hover visual overlay and restore controller selection
                nav.setHoverFromMouse(-1);

                // Restore selection for the currently active button-list adapter (if any)
                if (auto* buttonList = dynamic_cast<AIO::GUI::ButtonListAdapter*>(nav.adapter())) {
                    // Only resume if we have an actual saved interaction.
                    // getLastResumeIndex() defaults to 0, but that can interfere with the
                    // first directional press selection logic and make it feel like we start at 1.
                    const int resumeIndex = buttonList->getLastResumeIndex();
                    if (resumeIndex > 0) {
                        nav.setControllerSelection(resumeIndex);
                        std::cout << "[STATE] Resumed controller from index " << resumeIndex << std::endl;
                    }
                    std::cout << "[HOVER] Cleared mouse hover, restored controller selection" << std::endl;
                }
            }
        }

        // Only poll mouse hover when in mouse mode
        // Apply to any active ButtonListAdapter-based page (not just main menu).
        if (currentInputMode == InputMode::Mouse && stackedWidget && nav.adapter()) {
            auto* buttonList = dynamic_cast<AIO::GUI::ButtonListAdapter*>(nav.adapter());
            QWidget* currentPage = stackedWidget->currentWidget();
            if (!buttonList || !currentPage || buttonList->pageWidget() != currentPage) {
                // If we're not on a button-list page, clear any stale mouse hover.
                return;
            }

            const QPoint mousePos = QCursor::pos();
            QPushButton* currentlyUnderMouse = nullptr;

            // Find which button (if any) the mouse is currently over
            for (const auto& btn : buttonList->getButtons()) {
                auto* btnPtr = btn.data();
                if (btnPtr && btnPtr->isVisible()) {
                    const QPoint localMousePos = btnPtr->mapFromGlobal(mousePos);
                    if (btnPtr->rect().contains(localMousePos)) {
                        currentlyUnderMouse = btnPtr;
                        break;
                    }
                }
            }

            // If mouse position changed, update hover
            if (currentlyUnderMouse != lastHoveredButton) {
                if (currentlyUnderMouse) {
                    // Mouse entered a button - set hover (overrides controller selection visually)
                    const int idx = buttonList->indexOfButton(currentlyUnderMouse);
                    if (idx >= 0) {
                        nav.setHoverFromMouse(idx);
                        std::cout << "[MOUSE_HOVER] Button " << idx << " now hovered" << std::endl;
                    }
                } else if (lastHoveredButton) {
                    // Mouse left all buttons - clear hover (shows controller selection again)
                    nav.setHoverFromMouse(-1);
                    std::cout << "[MOUSE_HOVER] All buttons left, cleared mouse hover" << std::endl;
                }
                lastHoveredButton = currentlyUnderMouse;
            }
        }

        if (frame.primary == AIO::GUI::UIAction::None) return;

        // Process controller/keyboard action
        onUIAction(frame);
    });
    navTimer->start(16);

    connect(stackedWidget, &QStackedWidget::currentChanged, this, [this](int) {
        onPageChanged();
    });

    // Install global event filter to catch mouse events from any widget.
    // This is intentionally disabled when streaming is enabled to avoid known
    // QtWebEngine/macOS instability with app-wide event filters.
    if (!streamingEnabled_) {
        QApplication::instance()->installEventFilter(this);
    }

    onPageChanged();
}

void MainWindow::onPageChanged() {
    QWidget* current = stackedWidget ? stackedWidget->currentWidget() : nullptr;
    if (!current) return;

    const bool inEmu = (current == emulatorPage) && emulatorRunning;
    AIO::Input::InputManager::instance().setActiveContext(
        inEmu ? AIO::Input::InputContext::Emulator : AIO::Input::InputContext::UI);

    // Reset navigation index when swapping pages to avoid carrying stale hover state.
    nav.clearHover();

    // Reset action edge tracking on page transitions so Confirm/Back edges
    // don't get suppressed by stale/held state from the previous page.
    // Ensure we have a fresh snapshot when seeding edge tracking.
    const auto snapshot = AIO::Input::InputManager::instance().updateSnapshot();
    actionMapper.reset(snapshot.logical);

    if (current == mainMenuPage) {
        nav.setAdapter(mainMenuAdapter.get());
        if (mainMenuAdapter) {
            mainMenuAdapter->applyHovered();
        }
        // When navigating with controller, default selection should be the first item.
        nav.setControllerSelection(0);
        return;
    }

    if (current == emulatorSelectPage) {
        nav.setAdapter(emulatorSelectAdapter.get());
        if (emulatorSelectAdapter) {
            emulatorSelectAdapter->applyHovered();
        }
        nav.setControllerSelection(0);
        return;
    }

    if (current == gameSelectPage) {
        nav.setAdapter(gameSelectAdapter.get());
        if (gameSelectAdapter) {
            gameSelectAdapter->applyHovered();
        }
        nav.setControllerSelection(0);
        return;
    }

    if (current == settingsPage) {
        nav.setAdapter(settingsMenuAdapter.get());
        if (settingsMenuAdapter) {
            settingsMenuAdapter->applyHovered();
        }
        nav.setControllerSelection(0);
        return;
    }

    if (current == nasPage) {
        nav.setAdapter(nasAdapter.get());
        if (nasAdapter) {
            nasAdapter->applyHovered();
        }
        nav.setControllerSelection(0);
        return;
    }

    nav.setAdapter(nullptr);
}

bool MainWindow::event(QEvent* e) {
    // Let the specific mouse event handlers deal with mouse events
    return QMainWindow::event(e);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    // Global keyboard capture: keep InputManager state updated even when focus
    // is on child widgets (e.g., emulator display label).
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        if (auto* keyEv = dynamic_cast<QKeyEvent*>(event)) {
            AIO::Input::InputManager::instance().processKeyEvent(keyEv);
        }
        // Do not consume the event; focused widgets may still need it.
    }

    // Handle global mouse events to detect mouse movement even when cursor is over child widgets
    if (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseButtonRelease) {

        // If we're in controller mode, switch to mouse mode
        if (currentInputMode == InputMode::Controller) {
            std::cout << "[INPUT_MODE] Global mouse event detected (" << event->type()
                      << "), switching to Mouse mode" << std::endl;
            currentInputMode = InputMode::Mouse;
            lastHoveredButton = nullptr; // Force re-polling on next frame

            // Show cursor - restore from override
            QApplication::restoreOverrideCursor();
            std::cout << "[CURSOR] Restored cursor globally" << std::endl;

            // Save controller state and clear visual display for the currently active adapter.
            if (auto* buttonList = dynamic_cast<AIO::GUI::ButtonListAdapter*>(nav.adapter())) {
                buttonList->saveControllerIndexBeforeMouse();
                buttonList->setHoveredIndex(-1);
                std::cout << "[STATE] Cleared visual state on mouse mode entry (global)" << std::endl;
            }
        }

        actionMapper.notifyMouseActivity();
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onUIAction(const AIO::GUI::UIActionFrame& frame) {
    if (frame.primary == AIO::GUI::UIAction::Home) {
        static QElapsedTimer homeTimer;
        static qint64 lastHomeMs = -100000;
        if (!homeTimer.isValid()) homeTimer.start();
        const qint64 nowMs = homeTimer.elapsed();
        const bool quickSecondPress = (nowMs - lastHomeMs) < 800;
        lastHomeMs = nowMs;

        QWidget* current = stackedWidget ? stackedWidget->currentWidget() : nullptr;
        if (current == emulatorPage) {
            // From emulator: go back to ROM select.
            stopGame();
            return;
        }
        if (current == gameSelectPage) {
            // From ROM select: second press goes back to main menu.
            if (quickSecondPress) {
                goToMainMenu();
            }
            return;
        }
        // Default: go to main menu.
        goToMainMenu();
        return;
    }

    if (nav.adapter()) {
        nav.apply(frame);
        return;
    }
}

} // namespace GUI
} // namespace AIO
