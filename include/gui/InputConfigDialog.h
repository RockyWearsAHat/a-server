#pragma once

// Include ALL Qt headers first, at global scope
#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QTimer>
#include <QKeyEvent>

// Then include project headers (which may contain namespaces)
#include "input/InputManager.h"

namespace AIO {
namespace GUI {

class InputConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit InputConfigDialog(QWidget *parent = nullptr);
    virtual ~InputConfigDialog();
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

} // namespace GUI
} // namespace AIO
