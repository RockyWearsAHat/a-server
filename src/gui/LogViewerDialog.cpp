#include "gui/LogViewerDialog.h"
#include <QTextEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFile>
#include <QTextStream>


namespace AIO {
namespace GUI {

LogViewerDialog::LogViewerDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Crash Log Viewer");
    logTextEdit = new QTextEdit(this);
    logTextEdit->setReadOnly(true);
    closeButton = new QPushButton("Close", this);
    QVBoxLayout* layout = new QVBoxLayout;
    layout->addWidget(logTextEdit);
    layout->addWidget(closeButton);
    setLayout(layout);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

LogViewerDialog::~LogViewerDialog() = default;

void LogViewerDialog::setLogText(const QString& text) {
    logTextEdit->setPlainText(text);
}

void LogViewerDialog::loadLogFile(const QString& path) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        logTextEdit->setPlainText(in.readAll());
        file.close();
    } else {
        logTextEdit->setPlainText("Failed to open log file: " + path);
    }
}

} // namespace GUI
} // namespace AIO
