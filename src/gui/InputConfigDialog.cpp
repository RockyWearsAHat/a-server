// All includes at global scope
#include "gui/InputConfigDialog.h"
#include "input/InputManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QKeySequence>
#include <SDL2/SDL.h>

namespace AIO {
namespace GUI {

InputConfigDialog::~InputConfigDialog() {}

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

// ...rest of implementation...

void InputConfigDialog::updateTable() {
    // TODO: Implement
}

void InputConfigDialog::onRemapKeyClicked() {
    // TODO: Implement
}

void InputConfigDialog::onRemapGamepadClicked() {
    // TODO: Implement
}

void InputConfigDialog::onPollGamepad() {
    // TODO: Implement
}

void InputConfigDialog::onDefaultsClicked() {
    // TODO: Implement
}

void InputConfigDialog::onSaveClicked() {
    accept();
}

void InputConfigDialog::keyPressEvent(QKeyEvent* event) {
    QDialog::keyPressEvent(event);
}

} // namespace GUI
} // namespace AIO
