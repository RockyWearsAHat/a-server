#include <gui/CheatDialog.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QCheckBox>
#include <QTextStream>

namespace AIO::GUI {

    CheatDialog::CheatDialog(AIO::Emulator::GBA::CheatManager* cheatManager, QWidget* parent)
        : QDialog(parent), cheatManager(cheatManager) {
        
        setWindowTitle("Cheats Manager");
        resize(600, 400);

        QVBoxLayout* layout = new QVBoxLayout(this);

        table = new QTableWidget(this);
        table->setColumnCount(3);
        table->setHorizontalHeaderLabels({"Enabled", "Description", "Code"});
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        layout->addWidget(table);

        QHBoxLayout* btnLayout = new QHBoxLayout();
        btnAdd = new QPushButton("Add Cheat", this);
        btnRemove = new QPushButton("Remove Cheat", this);
        btnSave = new QPushButton("Save Cheats", this);
        btnLoad = new QPushButton("Load Cheats", this);
        
        btnLayout->addWidget(btnAdd);
        btnLayout->addWidget(btnRemove);
        btnLayout->addWidget(btnSave);
        btnLayout->addWidget(btnLoad);
        layout->addLayout(btnLayout);

        connect(btnAdd, &QPushButton::clicked, this, &CheatDialog::onAddClicked);
        connect(btnRemove, &QPushButton::clicked, this, &CheatDialog::onRemoveClicked);
        connect(btnSave, &QPushButton::clicked, this, &CheatDialog::onSaveClicked);
        connect(btnLoad, &QPushButton::clicked, this, &CheatDialog::onLoadClicked);
        connect(table, &QTableWidget::cellChanged, this, &CheatDialog::onToggleCheat);

        RefreshList();
    }

    void CheatDialog::RefreshList() {
        table->blockSignals(true);
        table->setRowCount(0);
        
        if (!cheatManager) return;

        const auto& cheats = cheatManager->GetCheats();
        for (size_t i = 0; i < cheats.size(); ++i) {
            table->insertRow(i);
            
            QTableWidgetItem* checkItem = new QTableWidgetItem();
            checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            checkItem->setCheckState(cheats[i].enabled ? Qt::Checked : Qt::Unchecked);
            table->setItem(i, 0, checkItem);
            
            QTableWidgetItem* descItem = new QTableWidgetItem(QString::fromStdString(cheats[i].description));
            descItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            table->setItem(i, 1, descItem);

            QTableWidgetItem* codeItem = new QTableWidgetItem(QString::fromStdString(cheats[i].code));
            codeItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            table->setItem(i, 2, codeItem);
        }
        table->blockSignals(false);
    }

    void CheatDialog::onAddClicked() {
        if (!cheatManager) return;

        QString desc = QInputDialog::getText(this, "Add Cheat", "Description:");
        if (desc.isEmpty()) return;

        QString code = QInputDialog::getMultiLineText(this, "Add Cheat", "Code (GameShark/CodeBreaker):");
        if (code.isEmpty()) return;

        cheatManager->AddCheat(desc.toStdString(), code.toStdString());
        RefreshList();
    }

    void CheatDialog::onRemoveClicked() {
        if (!cheatManager) return;
        int row = table->currentRow();
        if (row >= 0) {
            cheatManager->RemoveCheat(row);
            RefreshList();
        }
    }

    void CheatDialog::onToggleCheat(int row, int column) {
        if (!cheatManager || column != 0) return;
        
        bool enabled = (table->item(row, 0)->checkState() == Qt::Checked);
        cheatManager->ToggleCheat(row, enabled);
    }

    void CheatDialog::onSaveClicked() {
        if (!cheatManager) return;
        QString fileName = QFileDialog::getSaveFileName(this, "Save Cheats", "", "Cheat Files (*.cht)");
        if (fileName.isEmpty()) return;

        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            const auto& cheats = cheatManager->GetCheats();
            for (const auto& cheat : cheats) {
                out << "[Cheat]\n";
                out << "Name=" << QString::fromStdString(cheat.description) << "\n";
                out << "Enabled=" << (cheat.enabled ? "1" : "0") << "\n";
                out << "Code=" << QString::fromStdString(cheat.code).replace("\n", "\\n") << "\n";
            }
            file.close();
        }
    }

    void CheatDialog::onLoadClicked() {
        if (!cheatManager) return;
        QString fileName = QFileDialog::getOpenFileName(this, "Load Cheats", "", "Cheat Files (*.cht)");
        if (fileName.isEmpty()) return;

        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            cheatManager->ClearCheats();
            
            QString line;
            std::string name, code;
            bool enabled = true;
            
            while (!in.atEnd()) {
                line = in.readLine();
                if (line == "[Cheat]") {
                    if (!name.empty()) {
                        cheatManager->AddCheat(name, code);
                        // Hacky way to set enabled state since AddCheat defaults to true
                        if (!enabled) cheatManager->ToggleCheat(cheatManager->GetCheats().size() - 1, false);
                    }
                    name = "";
                    code = "";
                    enabled = true;
                } else if (line.startsWith("Name=")) {
                    name = line.mid(5).toStdString();
                } else if (line.startsWith("Enabled=")) {
                    enabled = (line.mid(8) == "1");
                } else if (line.startsWith("Code=")) {
                    code = line.mid(5).replace("\\n", "\n").toStdString();
                }
            }
            // Add last cheat
            if (!name.empty()) {
                cheatManager->AddCheat(name, code);
                if (!enabled) cheatManager->ToggleCheat(cheatManager->GetCheats().size() - 1, false);
            }
            
            file.close();
            RefreshList();
        }
    }

}
