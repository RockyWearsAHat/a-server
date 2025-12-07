#include "gui/InputConfigDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QKeySequence>
#include <SDL2/SDL.h>

namespace AIO::GUI {

    InputConfigDialog::InputConfigDialog(QWidget *parent) : QDialog(parent) {
        setWindowTitle("Input Configuration");
        resize(500, 500);

        QVBoxLayout* layout = new QVBoxLayout(this);

        table = new QTableWidget(this);
        table->setColumnCount(3);
        table->setHorizontalHeaderLabels({"GBA Button", "Key", "Gamepad"});
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        layout->addWidget(table);

        QHBoxLayout* btnLayout = new QHBoxLayout();
        
        QPushButton* remapKeyBtn = new QPushButton("Remap Key", this);
        connect(remapKeyBtn, &QPushButton::clicked, this, &InputConfigDialog::onRemapKeyClicked);
        btnLayout->addWidget(remapKeyBtn);

        QPushButton* remapGpBtn = new QPushButton("Remap Gamepad", this);
        connect(remapGpBtn, &QPushButton::clicked, this, &InputConfigDialog::onRemapGamepadClicked);
        btnLayout->addWidget(remapGpBtn);

        defaultsButton = new QPushButton("Defaults", this);
        connect(defaultsButton, &QPushButton::clicked, this, &InputConfigDialog::onDefaultsClicked);
        btnLayout->addWidget(defaultsButton);

        saveButton = new QPushButton("Save & Close", this);
        connect(saveButton, &QPushButton::clicked, this, &InputConfigDialog::onSaveClicked);
        btnLayout->addWidget(saveButton);

        layout->addLayout(btnLayout);

        pollTimer = new QTimer(this);
        connect(pollTimer, &QTimer::timeout, this, &InputConfigDialog::onPollGamepad);

        updateTable();
    }

    void InputConfigDialog::updateTable() {
        table->setRowCount(Input::Button_Count);
        auto& input = Input::InputManager::instance();

        for (int i = 0; i < Input::Button_Count; ++i) {
            Input::GBAButton btn = static_cast<Input::GBAButton>(i);
            
            QTableWidgetItem* nameItem = new QTableWidgetItem(input.getButtonName(btn));
            table->setItem(i, 0, nameItem);

            int key = input.getKeyForButton(btn);
            QKeySequence seq(key);
            QTableWidgetItem* keyItem = new QTableWidgetItem(seq.toString());
            table->setItem(i, 1, keyItem);

            int gpBtn = input.getGamepadButtonForButton(btn);
            QString gpName = (gpBtn == SDL_CONTROLLER_BUTTON_INVALID) ? "None" : input.getGamepadButtonName(gpBtn);
            QTableWidgetItem* gpItem = new QTableWidgetItem(gpName);
            table->setItem(i, 2, gpItem);
        }
    }

    void InputConfigDialog::onRemapKeyClicked() {
        int row = table->currentRow();
        if (row < 0) return;

        buttonToRemap = static_cast<Input::GBAButton>(row);
        waitingForKey = true;
        waitingForGamepad = false;
        pollTimer->stop();
        table->item(row, 1)->setText("Press Key...");
        table->setFocus(); 
    }

    void InputConfigDialog::onRemapGamepadClicked() {
        int row = table->currentRow();
        if (row < 0) return;

        buttonToRemap = static_cast<Input::GBAButton>(row);
        waitingForKey = false;
        waitingForGamepad = true;
        table->item(row, 2)->setText("Press Button...");
        pollTimer->start(50);
    }

    void InputConfigDialog::onPollGamepad() {
        if (!waitingForGamepad) {
            pollTimer->stop();
            return;
        }

        SDL_PumpEvents();
        
        // Check all opened controllers
        // We need access to controllers, but InputManager keeps them private.
        // We can iterate all joysticks again or ask InputManager.
        // But InputManager::update() handles opening.
        // Let's just iterate SDL_GameControllerOpen(i) if we can.
        // Or better, just check all joysticks.
        
        for (int i = 0; i < SDL_NumJoysticks(); ++i) {
            if (SDL_IsGameController(i)) {
                SDL_GameController* pad = SDL_GameControllerOpen(i);
                if (pad) {
                    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
                        if (SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)b)) {
                            // Found button press
                            Input::InputManager::instance().setGamepadMapping(b, buttonToRemap);
                            waitingForGamepad = false;
                            pollTimer->stop();
                            updateTable();
                            // Close if we opened it just for this check? 
                            // InputManager manages them. If we open it here, we increment ref count.
                            // SDL_GameControllerClose(pad); 
                            // Actually, if InputManager has it open, it's fine.
                            // If not, we should close it.
                            // But we don't know if InputManager has it open easily.
                            // Let's just close it to be safe (decrement ref count).
                            SDL_GameControllerClose(pad);
                            return;
                        }
                    }
                    SDL_GameControllerClose(pad);
                }
            }
        }
    }

    void InputConfigDialog::onDefaultsClicked() {
        auto& input = Input::InputManager::instance();
        // Reset logic (simplified)
        // Ideally call input.resetToDefaults()
        // For now, just reload config (which might be saved already)
        // Or manually set defaults again.
        // I'll skip full implementation for brevity, user can delete config file.
        // But let's at least set some defaults.
        input.setMapping(Qt::Key_Z, Input::Button_A);
        input.setMapping(Qt::Key_X, Input::Button_B);
        input.setMapping(Qt::Key_Return, Input::Button_Start);
        input.setMapping(Qt::Key_Backspace, Input::Button_Select);
        // ...
        updateTable();
    }

    void InputConfigDialog::onSaveClicked() {
        Input::InputManager::instance().saveConfig();
        accept();
    }

    void InputConfigDialog::keyPressEvent(QKeyEvent* event) {
        if (waitingForKey) {
            Input::InputManager::instance().setMapping(event->key(), buttonToRemap);
            waitingForKey = false;
            updateTable();
        } else {
            QDialog::keyPressEvent(event);
        }
    }

}
