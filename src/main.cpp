#include <iostream>
#include <QApplication>
#include "gui/MainWindow.h"

int main(int argc, char *argv[]) {
    std::cout << std::unitbuf; // Flush stdout immediately
    QApplication app(argc, argv);

    std::cout << "AIO Server Initializing..." << std::endl;

    AIO::GUI::MainWindow window;
    window.show();

    return app.exec();
}
