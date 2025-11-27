#include <iostream>
#include <QApplication>
#include "gui/MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    std::cout << "AIO Server Initializing..." << std::endl;

    AIO::GUI::MainWindow window;
    window.show();

    std::cout << "Loading ROM..." << std::endl;
    window.LoadROM("/Users/alexwaldmann/Desktop/AIO Server/SMA2.gba");

    return app.exec();
}
