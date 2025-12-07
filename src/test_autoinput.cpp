#include <iostream>
#include <thread>
#include <chrono>
#include <QApplication>
#include <QTimer>
#include <QKeyEvent>
#include "gui/MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    std::cout << "AIO Server with Auto-Input Test..." << std::endl;

    AIO::GUI::MainWindow window;
    window.show();

    std::cout << "Loading ROM..." << std::endl;
    window.LoadROM("/Users/alexwaldmann/Desktop/AIO Server/SMA2.gba");

    // Auto-press A button (Z key) after 3 seconds
    QTimer::singleShot(3000, [&window]() {
        std::cout << "AUTO-PRESSING A BUTTON (Z KEY)..." << std::endl;
        QKeyEvent pressEvent(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier);
        QApplication::sendEvent(&window, &pressEvent);
        
        // Release after 100ms
        QTimer::singleShot(100, [&window]() {
            QKeyEvent releaseEvent(QEvent::KeyRelease, Qt::Key_Z, Qt::NoModifier);
            QApplication::sendEvent(&window, &releaseEvent);
            std::cout << "A BUTTON RELEASED" << std::endl;
        });
    });

    // Keep pressing A every 2 seconds
    QTimer *repeatTimer = new QTimer();
    QObject::connect(repeatTimer, &QTimer::timeout, [&window]() {
        std::cout << "AUTO-PRESSING A BUTTON AGAIN..." << std::endl;
        QKeyEvent pressEvent(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier);
        QApplication::sendEvent(&window, &pressEvent);
        QTimer::singleShot(100, [&window]() {
            QKeyEvent releaseEvent(QEvent::KeyRelease, Qt::Key_Z, Qt::NoModifier);
            QApplication::sendEvent(&window, &releaseEvent);
        });
    });
    repeatTimer->start(2000);

    return app.exec();
}
