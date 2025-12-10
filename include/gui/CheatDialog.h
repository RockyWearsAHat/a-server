#pragma once
#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <vector>
#include "emulator/gba/CheatManager.h"

namespace AIO {
namespace GUI {

    class CheatDialog : public QDialog {
        Q_OBJECT
    public:
        CheatDialog(AIO::Emulator::GBA::CheatManager* cheatManager, QWidget* parent = nullptr);
        
    private slots:
        void onAddClicked();
        void onRemoveClicked();
        void onToggleCheat(int row, int column);
        void onSaveClicked();
        void onLoadClicked();

    private:
        void RefreshList();

        AIO::Emulator::GBA::CheatManager* cheatManager;
        QTableWidget* table;
        QPushButton* btnAdd;
        QPushButton* btnRemove;
        QPushButton* btnSave;
        QPushButton* btnLoad;
    };

} // namespace GUI
} // namespace AIO
