#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QTimer>
#include <QImage>

class QKeyEvent;

#include "emulator/gba/GBA.h"

namespace AIO::GUI {

    class MainWindow : public QMainWindow {
        Q_OBJECT

    public:
        MainWindow(QWidget *parent = nullptr);
        ~MainWindow();

        void LoadROM(const std::string& path);

    protected:
        void keyPressEvent(QKeyEvent *event) override;
        void keyReleaseEvent(QKeyEvent *event) override;

    private slots:
        void GameLoop();

    private:
        QLabel *statusLabel;
        QLabel *displayLabel;
        AIO::Emulator::GBA::GBA gba;
        QTimer *gameTimer;
        QImage displayImage;
        uint16_t keyInputState = 0x03FF; // Default: All keys released (1)
    };

}
