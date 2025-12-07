#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QTimer>
#include "input/InputManager.h"

namespace AIO::GUI {

    class InputConfigDialog : public QDialog {
        Q_OBJECT
    public:
        explicit InputConfigDialog(QWidget *parent = nullptr);

    private slots:
        void onRemapKeyClicked();
        void onRemapGamepadClicked();
        void onSaveClicked();
        void onDefaultsClicked();
        void onPollGamepad();

    private:
        void updateTable();
        
        QTableWidget* table;
        QPushButton* saveButton;
        QPushButton* defaultsButton;
        QTimer* pollTimer;
        
        bool waitingForKey = false;
        bool waitingForGamepad = false;
        Input::GBAButton buttonToRemap;
        
    protected:
        void keyPressEvent(QKeyEvent* event) override;
    };

}
