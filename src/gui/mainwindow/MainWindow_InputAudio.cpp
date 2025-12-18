#include "gui/MainWindow.h"

#include "gui/MainMenuAdapter.h"

#include "input/InputManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>

#include <SDL2/SDL.h>
#include <SDL2/SDL_hints.h>

#include <cstring>
#include <iostream>

namespace AIO {
namespace GUI {

static MainWindow* audioInstance = nullptr;

void MainWindow::initAudio() {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        ::std::cerr << "SDL audio init failed: " << SDL_GetError() << ::std::endl;
        return;
    }

    // Best-effort: disable SDL controller event forwarding outside focus.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");

    SDL_AudioSpec want, have;
    SDL_memset(&want, 0, sizeof(want));
    want.freq = AUDIO_SAMPLE_RATE;
    want.format = AUDIO_S16SYS;  // Signed 16-bit, system byte order
    want.channels = 2;  // Stereo
    want.samples = AUDIO_BUFFER_SIZE;
    want.callback = audioCallback;
    want.userdata = this;

    audioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audioDevice == 0) {
        ::std::cerr << "SDL audio device open failed: " << SDL_GetError() << ::std::endl;
        return;
    }

    ::std::cout << "Audio initialized: " << have.freq << " Hz, "
                << (int)have.channels << " channels, buffer " << have.samples << ::std::endl;

    // Start audio playback
    SDL_PauseAudioDevice(audioDevice, 0);
    audioInstance = this;
}

void MainWindow::closeAudio() {
    if (audioDevice != 0) {
        SDL_CloseAudioDevice(audioDevice);
        audioDevice = 0;
    }
    audioInstance = nullptr;
}

void MainWindow::audioCallback(void* userdata, Uint8* stream, int len) {
    MainWindow* self = static_cast<MainWindow*>(userdata);
    int16_t* buffer = reinterpret_cast<int16_t*>(stream);
    int numSamples = len / 4;  // stereo samples (2 channels * 2 bytes)

    if (self->currentEmulator == EmulatorType::GBA) {
        // Get samples from APU ring buffer
        self->gba.GetAPU().GetSamples(buffer, numSamples);
    } else {
        // Silence for now
        memset(stream, 0, len);
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    // Always keep keyboard mapping updated for emulator core.
    const bool mapped = Input::InputManager::instance().processKeyEvent(event);

    // If we're not in an active emulator run, treat arrows/enter/esc as UI navigation.
    const bool inEmu = (stackedWidget && stackedWidget->currentWidget() == emulatorPage && emulatorRunning);
    if (!inEmu) {
        QMainWindow::keyPressEvent(event);
        return;
    }

    if (!mapped) {
        // Debugger controls via GUI
        if (debuggerEnabled) {
            if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
                gba.SetSingleStep(true);
                gba.Step();
            } else if (event->key() == Qt::Key_Up) {
                gba.StepBack();
            } else if (event->key() == Qt::Key_C) {
                debuggerContinue = true;
            }
        }
        QMainWindow::keyPressEvent(event);
    }
}

void MainWindow::keyReleaseEvent(QKeyEvent *event) {
    if (Input::InputManager::instance().processKeyEvent(event)) {
        // Input handled
    } else {
        QMainWindow::keyReleaseEvent(event);
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent* event) {
    // Show cursor and switch to mouse mode on mouse movement
    setCursor(Qt::ArrowCursor);
    if (stackedWidget) stackedWidget->setCursor(Qt::ArrowCursor);
    if (mainMenuPage) mainMenuPage->setCursor(Qt::ArrowCursor);
    if (mainMenuAdapter) {
        for (const auto& btn : mainMenuAdapter->getButtons()) {
            if (auto* btnPtr = btn.data()) {
                btnPtr->setCursor(Qt::ArrowCursor);
            }
        }
    }

    // Switch from controller mode to mouse mode
    if (currentInputMode == InputMode::Controller) {
        std::cout << "[INPUT_MODE] Mouse movement detected, switching to Mouse mode" << std::endl;
        currentInputMode = InputMode::Mouse;
        lastHoveredButton = nullptr;  // Force re-polling on next frame

        // Save controller state and clear visual display
        if (mainMenuAdapter) {
            mainMenuAdapter->saveControllerIndexBeforeMouse();
            // Clear controller display, show nothing (both hovered and mouseHover are -1)
            mainMenuAdapter->setHoveredIndex(-1);
            std::cout << "[STATE] Cleared visual state on mouse mode entry" << std::endl;
        }
    }

    actionMapper.notifyMouseActivity();
    QMainWindow::mouseMoveEvent(event);
}

void MainWindow::mousePressEvent(QMouseEvent* event) {
    // Show cursor and switch to mouse mode on mouse press
    setCursor(Qt::ArrowCursor);
    if (stackedWidget) stackedWidget->setCursor(Qt::ArrowCursor);
    if (mainMenuPage) mainMenuPage->setCursor(Qt::ArrowCursor);
    if (mainMenuAdapter) {
        for (const auto& btn : mainMenuAdapter->getButtons()) {
            if (auto* btnPtr = btn.data()) {
                btnPtr->setCursor(Qt::ArrowCursor);
            }
        }
    }

    // Switch from controller mode to mouse mode
    if (currentInputMode == InputMode::Controller) {
        std::cout << "[INPUT_MODE] Mouse press detected, switching to Mouse mode" << std::endl;
        currentInputMode = InputMode::Mouse;
        lastHoveredButton = nullptr;

        // Save controller state and clear visual display
        if (mainMenuAdapter) {
            mainMenuAdapter->saveControllerIndexBeforeMouse();
            // Clear controller display, show nothing (both hovered and mouseHover are -1)
            mainMenuAdapter->setHoveredIndex(-1);
            std::cout << "[STATE] Cleared visual state on mouse mode entry" << std::endl;
        }
    }

    actionMapper.notifyMouseActivity();
    QMainWindow::mousePressEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent* event) {
    // Show cursor and switch to mouse mode on mouse release
    setCursor(Qt::ArrowCursor);
    if (stackedWidget) stackedWidget->setCursor(Qt::ArrowCursor);
    if (mainMenuPage) mainMenuPage->setCursor(Qt::ArrowCursor);
    if (mainMenuAdapter) {
        for (const auto& btn : mainMenuAdapter->getButtons()) {
            if (auto* btnPtr = btn.data()) {
                btnPtr->setCursor(Qt::ArrowCursor);
            }
        }
    }

    // Switch from controller mode to mouse mode
    if (currentInputMode == InputMode::Controller) {
        std::cout << "[INPUT_MODE] Mouse release detected, switching to Mouse mode" << std::endl;
        currentInputMode = InputMode::Mouse;
        lastHoveredButton = nullptr;

        // Save controller state and clear visual display
        if (mainMenuAdapter) {
            mainMenuAdapter->saveControllerIndexBeforeMouse();
            // Clear controller display, show nothing (both hovered and mouseHover are -1)
            mainMenuAdapter->setHoveredIndex(-1);
            std::cout << "[STATE] Cleared visual state on mouse mode entry" << std::endl;
        }
    }

    actionMapper.notifyMouseActivity();
    QMainWindow::mouseReleaseEvent(event);
}

} // namespace GUI
} // namespace AIO
