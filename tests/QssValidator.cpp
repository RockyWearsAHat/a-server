#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QStyle>
#include <QWidget>

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

std::vector<QString> *g_qssMessages = nullptr;

void qssMessageHandler(QtMsgType type, const QMessageLogContext &,
                       const QString &message) {
  if (!g_qssMessages) {
    return;
  }

  if (type != QtWarningMsg && type != QtCriticalMsg && type != QtFatalMsg) {
    return;
  }

  const QString lowered = message.toLower();
  if (lowered.contains(QStringLiteral("stylesheet")) ||
      lowered.contains(QStringLiteral("style sheet")) ||
      lowered.contains(QStringLiteral("unknown property")) ||
      lowered.contains(QStringLiteral("could not parse"))) {
    g_qssMessages->push_back(message);
  }
}

bool validateQssFile(const QString &filePath, std::vector<QString> &messages) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    messages.push_back(
        QStringLiteral("Failed to open stylesheet: %1").arg(filePath));
    return false;
  }

  const QString styleSheet = QString::fromUtf8(file.readAll());
  const auto previousHandler = qInstallMessageHandler(qssMessageHandler);
  g_qssMessages = &messages;

  QWidget widget;
  widget.setObjectName(QStringLiteral("QssValidationProbe"));
  widget.setStyleSheet(styleSheet);
  widget.ensurePolished();
  if (widget.style()) {
    widget.style()->polish(&widget);
  }
  qApp->processEvents();

  g_qssMessages = nullptr;
  qInstallMessageHandler(previousHandler);
  return messages.empty();
}

} // namespace

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  QApplication app(argc, argv);

  const QString assetsDir =
      argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("assets/qss");
  const QDir dir(assetsDir);
  if (!dir.exists()) {
    std::cerr << "QSS directory not found: " << assetsDir.toStdString()
              << std::endl;
    return 1;
  }

  const QFileInfoList qssFiles = dir.entryInfoList(
      {QStringLiteral("*.qss")}, QDir::Files | QDir::Readable, QDir::Name);
  if (qssFiles.empty()) {
    std::cerr << "No QSS files found in " << assetsDir.toStdString()
              << std::endl;
    return 1;
  }

  bool allValid = true;
  for (const QFileInfo &info : qssFiles) {
    std::vector<QString> messages;
    const bool valid = validateQssFile(info.absoluteFilePath(), messages);
    if (valid) {
      continue;
    }

    allValid = false;
    std::cerr << "QSS validation failed for " << info.fileName().toStdString()
              << std::endl;
    for (const QString &message : messages) {
      std::cerr << "  " << message.toStdString() << std::endl;
    }
  }

  if (!allValid) {
    return 1;
  }

  std::cout << "QSS validation passed for " << qssFiles.size() << " file(s)."
            << std::endl;
  return 0;
}