#include "gui/RemoteControlServer.h"
#include "gui/HomeScreen.h"
#include "gui/MainWindow.h"
#include "input/InputManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QStackedWidget>
#include <QTcpSocket>
#include <QTimer>
#include <QWidget>

#include <iostream>

namespace AIO {
namespace GUI {

// ─── Key name → Qt::Key mapping ─────────────────────────────────────────

static const QMap<QString, int> &keyMap() {
  static const QMap<QString, int> map = {
      {"up", Qt::Key_Up},
      {"down", Qt::Key_Down},
      {"left", Qt::Key_Left},
      {"right", Qt::Key_Right},
      {"enter", Qt::Key_Return},
      {"return", Qt::Key_Return},
      {"space", Qt::Key_Space},
      {"escape", Qt::Key_Escape},
      {"esc", Qt::Key_Escape},
      {"tab", Qt::Key_Tab},
      {"shift", Qt::Key_Shift},
      {"home", Qt::Key_Home},
      {"backspace", Qt::Key_Backspace},
      {"delete", Qt::Key_Delete},
      // letters
      {"a", Qt::Key_A},
      {"b", Qt::Key_B},
      {"c", Qt::Key_C},
      {"d", Qt::Key_D},
      {"e", Qt::Key_E},
      {"f", Qt::Key_F},
      {"g", Qt::Key_G},
      {"h", Qt::Key_H},
      {"i", Qt::Key_I},
      {"j", Qt::Key_J},
      {"k", Qt::Key_K},
      {"l", Qt::Key_L},
      {"m", Qt::Key_M},
      {"n", Qt::Key_N},
      {"o", Qt::Key_O},
      {"p", Qt::Key_P},
      {"q", Qt::Key_Q},
      {"r", Qt::Key_R},
      {"s", Qt::Key_S},
      {"t", Qt::Key_T},
      {"u", Qt::Key_U},
      {"v", Qt::Key_V},
      {"w", Qt::Key_W},
      {"x", Qt::Key_X},
      {"y", Qt::Key_Y},
      {"z", Qt::Key_Z},
      // digits
      {"0", Qt::Key_0},
      {"1", Qt::Key_1},
      {"2", Qt::Key_2},
      {"3", Qt::Key_3},
      {"4", Qt::Key_4},
      {"5", Qt::Key_5},
      {"6", Qt::Key_6},
      {"7", Qt::Key_7},
      {"8", Qt::Key_8},
      {"9", Qt::Key_9},
  };
  return map;
}

int RemoteControlServer::resolveQtKey(const QString &name) {
  const QString lower = name.toLower().trimmed();
  auto it = keyMap().find(lower);
  if (it != keyMap().end())
    return it.value();
  // Single character fallback
  if (lower.size() == 1) {
    QChar ch = lower[0];
    if (ch.isLetterOrNumber())
      return ch.toUpper().unicode();
  }
  return 0;
}

QString RemoteControlServer::keyTextForQt(int qtKey) {
  if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z)
    return QString(QChar(qtKey - Qt::Key_A + 'a'));
  if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)
    return QString(QChar(qtKey - Qt::Key_0 + '0'));
  if (qtKey == Qt::Key_Space)
    return " ";
  return QString();
}

// ─── HTTP helpers ────────────────────────────────────────────────────────

QByteArray RemoteControlServer::statusText(int status) {
  switch (status) {
  case 200:
    return "OK";
  case 400:
    return "Bad Request";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  case 500:
    return "Internal Server Error";
  default:
    return "";
  }
}

void RemoteControlServer::writeResponse(QTcpSocket *socket,
                                        const HttpResponse &resp) {
  QByteArray out;
  out += "HTTP/1.1 ";
  out += QByteArray::number(resp.status);
  out += " ";
  out += statusText(resp.status);
  out += "\r\n";

  if (!resp.contentType.isEmpty()) {
    out += "Content-Type: ";
    out += resp.contentType.toUtf8();
    out += "\r\n";
  }

  out += "Access-Control-Allow-Origin: *\r\n";
  out += "Content-Length: ";
  out += QByteArray::number(resp.body.size());
  out += "\r\n";
  out += "Connection: close\r\n";
  out += "\r\n";
  out += resp.body;

  socket->write(out);
  socket->flush();
}

bool RemoteControlServer::tryParseHttpRequest(QByteArray &buffer,
                                              HttpRequest &outReq) {
  const int headerEnd = buffer.indexOf("\r\n\r\n");
  if (headerEnd < 0)
    return false;

  const QByteArray headerBytes = buffer.left(headerEnd);
  const QList<QByteArray> lines = headerBytes.split('\n');
  if (lines.isEmpty())
    return false;

  const QByteArray requestLine = lines.first().trimmed();
  const QList<QByteArray> parts = requestLine.split(' ');
  if (parts.size() < 2)
    return false;

  outReq.method = QString::fromUtf8(parts[0]).trimmed();

  const QByteArray target = parts[1];
  const int qpos = target.indexOf('?');
  if (qpos >= 0) {
    outReq.path = QString::fromUtf8(target.left(qpos));
    outReq.query = QString::fromUtf8(target.mid(qpos + 1));
  } else {
    outReq.path = QString::fromUtf8(target);
    outReq.query.clear();
  }

  outReq.headers.clear();
  for (int i = 1; i < lines.size(); ++i) {
    QByteArray line = lines[i].trimmed();
    if (line.isEmpty())
      continue;
    const int colon = line.indexOf(':');
    if (colon <= 0)
      continue;
    const QString key = QString::fromUtf8(line.left(colon)).trimmed().toLower();
    const QString value = QString::fromUtf8(line.mid(colon + 1)).trimmed();
    outReq.headers.insert(key, value);
  }

  qint64 contentLength = 0;
  const auto it = outReq.headers.find("content-length");
  if (it != outReq.headers.end()) {
    bool ok = false;
    contentLength = it.value().toLongLong(&ok);
    if (!ok || contentLength < 0)
      contentLength = 0;
  }

  const qint64 totalNeeded = headerEnd + 4 + contentLength;
  if (buffer.size() < totalNeeded)
    return false;

  outReq.body = buffer.mid(headerEnd + 4, contentLength);
  buffer = buffer.mid(totalNeeded);
  return true;
}

// ─── Construction / lifecycle ────────────────────────────────────────────

RemoteControlServer::RemoteControlServer(MainWindow *window, QObject *parent)
    : QObject(parent), window_(window) {
  const QString envPort = qEnvironmentVariable("AIO_REMOTE_PORT");
  if (!envPort.isEmpty()) {
    bool ok = false;
    const int p = envPort.toInt(&ok);
    if (ok && p > 0 && p <= 65535)
      configuredPort_ = static_cast<quint16>(p);
  }
  connect(&server_, &QTcpServer::newConnection, this,
          &RemoteControlServer::onNewConnection);
}

bool RemoteControlServer::Start() {
  if (IsRunning())
    return true;

  bool listening = server_.listen(QHostAddress::LocalHost, configuredPort_);
  quint16 chosenPort = configuredPort_;

  if (!listening) {
    for (int i = 1; i <= 5; ++i) {
      const quint16 p = static_cast<quint16>(configuredPort_ + i);
      if (server_.listen(QHostAddress::LocalHost, p)) {
        listening = true;
        chosenPort = p;
        break;
      }
    }
  }

  if (!listening) {
    std::cerr << "[RemoteControl] Failed to listen on port " << configuredPort_
              << ": " << server_.errorString().toStdString() << std::endl;
    return false;
  }

  // Print machine-readable port info for external tools
  std::cout << "{\"remote_control_port\":" << chosenPort << "}" << std::endl;

  writePortFile(chosenPort);
  return true;
}

void RemoteControlServer::Stop() { server_.close(); }

bool RemoteControlServer::IsRunning() const { return server_.isListening(); }

quint16 RemoteControlServer::Port() const { return server_.serverPort(); }

void RemoteControlServer::writePortFile(quint16 port) {
  const QString dir = QDir::currentPath() + "/test_output/visual_loop";
  QDir().mkpath(dir);
  QFile f(dir + "/remote_port");
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    f.write(QByteArray::number(port));
    f.close();
  }
}

// ─── Connection handling ─────────────────────────────────────────────────

void RemoteControlServer::onNewConnection() {
  while (server_.hasPendingConnections()) {
    auto *socket = server_.nextPendingConnection();
    if (!socket)
      continue;

    // Localhost only — QTcpServer is already bound to LocalHost,
    // but double-check the peer address.
    const QHostAddress peer = socket->peerAddress();
    if (!peer.isLoopback()) {
      socket->disconnectFromHost();
      socket->deleteLater();
      continue;
    }

    socket->setProperty("rc_buffer", QByteArray());

    connect(socket, &QTcpSocket::readyRead, this,
            [this, socket]() { handleSocket(socket); });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
  }
}

void RemoteControlServer::handleSocket(QTcpSocket *socket) {
  QByteArray buffer = socket->property("rc_buffer").toByteArray();
  buffer.append(socket->readAll());

  HttpRequest req;
  if (!tryParseHttpRequest(buffer, req)) {
    if (buffer.size() > 1 * 1024 * 1024) {
      socket->disconnectFromHost();
    } else {
      socket->setProperty("rc_buffer", buffer);
    }
    return;
  }

  socket->setProperty("rc_buffer", QByteArray());

  HttpResponse resp = route(req);
  writeResponse(socket, resp);
  socket->disconnectFromHost();
}

// ─── Routing ─────────────────────────────────────────────────────────────

RemoteControlServer::HttpResponse
RemoteControlServer::route(const HttpRequest &req) {
  if (req.path == "/health" && req.method == "GET")
    return handleHealth();
  if (req.path == "/status" && req.method == "GET")
    return handleStatus();
  if (req.path == "/state" && req.method == "GET")
    return handleState();
  if (req.path == "/state/navigation" && req.method == "GET")
    return handleStateNavigation();
  if (req.path == "/state/input" && req.method == "GET")
    return handleStateInput();
  if (req.path == "/state/emulator" && req.method == "GET")
    return handleStateEmulator();
  if (req.path == "/state/widgets" && req.method == "GET")
    return handleStateWidgets();
  if (req.path == "/state/page" && req.method == "GET")
    return handleStatePage();
  if (req.path == "/execute" && req.method == "POST")
    return handleExecute(req);
  if (req.path == "/input/key" && req.method == "POST")
    return handleKeyInput(req);
  if (req.path == "/input/key-sequence" && req.method == "POST")
    return handleKeySequence(req);
  if (req.path == "/input/type" && req.method == "POST")
    return handleTypeInput(req);
  if (req.path == "/input/click" && req.method == "POST")
    return handleClickInput(req);

  return errorResponse(404, "Not found");
}

// ─── JSON helpers ────────────────────────────────────────────────────────

RemoteControlServer::HttpResponse
RemoteControlServer::jsonResponse(int status, const QByteArray &json) {
  HttpResponse resp;
  resp.status = status;
  resp.body = json;
  resp.contentType = "application/json; charset=utf-8";
  return resp;
}

RemoteControlServer::HttpResponse
RemoteControlServer::errorResponse(int status, const QString &message) {
  QJsonObject obj;
  obj["ok"] = false;
  obj["error"] = message;
  return jsonResponse(status,
                      QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// ─── GET /health ─────────────────────────────────────────────────────────

RemoteControlServer::HttpResponse RemoteControlServer::handleHealth() {
  QJsonObject obj;
  obj["ok"] = true;
  return jsonResponse(200, QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// ─── GET /status ─────────────────────────────────────────────────────────

RemoteControlServer::HttpResponse RemoteControlServer::handleStatus() {
  QJsonObject obj;
  obj["ok"] = true;
  obj["pid"] = static_cast<qint64>(QCoreApplication::applicationPid());
  obj["focused"] = window_->isActiveWindow();

  // Determine current page name from the stacked widget
  QString pageName = "unknown";
  auto *stacked = window_->findChild<QStackedWidget *>();
  if (stacked) {
    QWidget *current = stacked->currentWidget();
    if (current) {
      const QString objName = current->objectName();
      if (!objName.isEmpty())
        pageName = objName;
    }
  }
  obj["page"] = pageName;

  return jsonResponse(200, QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// ─── POST /input/key ─────────────────────────────────────────────────────

RemoteControlServer::HttpResponse
RemoteControlServer::handleKeyInput(const HttpRequest &req) {
  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(req.body, &err);
  if (doc.isNull())
    return errorResponse(400, "Invalid JSON: " + err.errorString());

  QJsonObject obj = doc.object();
  const QString keyName = obj["key"].toString();
  const QString eventType = obj["event"].toString("press");

  const int qtKey = resolveQtKey(keyName);
  if (qtKey == 0)
    return errorResponse(400, "Unknown key: " + keyName);

  const QString text = keyTextForQt(qtKey);

  if (eventType == "press" || eventType == "down") {
    QKeyEvent pressEvent(QEvent::KeyPress, qtKey, Qt::NoModifier, text);
    QCoreApplication::sendEvent(window_, &pressEvent);
  }
  if (eventType == "press" || eventType == "up") {
    QKeyEvent releaseEvent(QEvent::KeyRelease, qtKey, Qt::NoModifier, text);
    QCoreApplication::sendEvent(window_, &releaseEvent);
  }

  QJsonObject resp;
  resp["ok"] = true;
  resp["key"] = keyName;
  resp["qt_key"] = qtKey;
  resp["event"] = eventType;
  return jsonResponse(200, QJsonDocument(resp).toJson(QJsonDocument::Compact));
}

// ─── POST /input/key-sequence ────────────────────────────────────────────

RemoteControlServer::HttpResponse
RemoteControlServer::handleKeySequence(const HttpRequest &req) {
  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(req.body, &err);
  if (doc.isNull())
    return errorResponse(400, "Invalid JSON: " + err.errorString());

  QJsonObject obj = doc.object();
  const QJsonArray keys = obj["keys"].toArray();
  const int delayMs = obj["delay_ms"].toInt(50);

  if (keys.isEmpty())
    return errorResponse(400, "Empty keys array");

  // Validate all keys upfront
  QVector<int> qtKeys;
  qtKeys.reserve(keys.size());
  for (const auto &k : keys) {
    const int qtKey = resolveQtKey(k.toString());
    if (qtKey == 0)
      return errorResponse(400, "Unknown key: " + k.toString());
    qtKeys.append(qtKey);
  }

  // Schedule keys on the Qt event loop with delays
  for (int i = 0; i < qtKeys.size(); ++i) {
    const int qtKey = qtKeys[i];
    QTimer::singleShot(i * delayMs, window_, [this, qtKey]() {
      const QString text = keyTextForQt(qtKey);
      QKeyEvent press(QEvent::KeyPress, qtKey, Qt::NoModifier, text);
      QCoreApplication::sendEvent(window_, &press);
      QKeyEvent release(QEvent::KeyRelease, qtKey, Qt::NoModifier, text);
      QCoreApplication::sendEvent(window_, &release);
    });
  }

  QJsonObject resp;
  resp["ok"] = true;
  resp["keys_scheduled"] = keys.size();
  resp["total_ms"] = (keys.size() - 1) * delayMs;
  return jsonResponse(200, QJsonDocument(resp).toJson(QJsonDocument::Compact));
}

// ─── POST /input/type ────────────────────────────────────────────────────

RemoteControlServer::HttpResponse
RemoteControlServer::handleTypeInput(const HttpRequest &req) {
  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(req.body, &err);
  if (doc.isNull())
    return errorResponse(400, "Invalid JSON: " + err.errorString());

  QJsonObject obj = doc.object();
  const QString text = obj["text"].toString();
  if (text.isEmpty())
    return errorResponse(400, "Empty text");

  const int delayMs = obj["delay_ms"].toInt(30);

  for (int i = 0; i < text.size(); ++i) {
    const QChar ch = text[i];
    QTimer::singleShot(i * delayMs, window_, [this, ch]() {
      const int qtKey = ch.toUpper().unicode();
      const QString charText(ch);
      QKeyEvent press(QEvent::KeyPress, qtKey, Qt::NoModifier, charText);
      QCoreApplication::sendEvent(window_, &press);
      QKeyEvent release(QEvent::KeyRelease, qtKey, Qt::NoModifier, charText);
      QCoreApplication::sendEvent(window_, &release);
    });
  }

  QJsonObject resp;
  resp["ok"] = true;
  resp["chars_scheduled"] = text.size();
  return jsonResponse(200, QJsonDocument(resp).toJson(QJsonDocument::Compact));
}

// ─── POST /input/click ───────────────────────────────────────────────────

RemoteControlServer::HttpResponse
RemoteControlServer::handleClickInput(const HttpRequest &req) {
  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(req.body, &err);
  if (doc.isNull())
    return errorResponse(400, "Invalid JSON: " + err.errorString());

  QJsonObject obj = doc.object();
  if (!obj.contains("x") || !obj.contains("y"))
    return errorResponse(400, "Missing x or y coordinate");

  const int x = obj["x"].toInt();
  const int y = obj["y"].toInt();
  const QPoint localPos(x, y);
  const QPoint globalPos = window_->mapToGlobal(localPos);

  // Find the actual child widget at that position
  QWidget *target = QApplication::widgetAt(globalPos);
  if (!target)
    target = window_;

  const QPointF widgetLocal = target->mapFromGlobal(globalPos);

  QMouseEvent pressEvent(QEvent::MouseButtonPress, widgetLocal, globalPos,
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(target, &pressEvent);

  QMouseEvent releaseEvent(QEvent::MouseButtonRelease, widgetLocal, globalPos,
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(target, &releaseEvent);

  QJsonObject resp;
  resp["ok"] = true;
  resp["x"] = x;
  resp["y"] = y;
  resp["target_widget"] = target->objectName().isEmpty()
                              ? target->metaObject()->className()
                              : target->objectName();
  return jsonResponse(200, QJsonDocument(resp).toJson(QJsonDocument::Compact));
}

// ─── Helper: page name from stacked widget ──────────────────────────────

static QString currentPageName(MainWindow *window) {
  auto *stacked = window->findChild<QStackedWidget *>();
  if (!stacked)
    return "unknown";
  QWidget *current = stacked->currentWidget();
  if (!current)
    return "unknown";
  const QString objName = current->objectName();
  return objName.isEmpty() ? QString(current->metaObject()->className())
                           : objName;
}

static int currentPageIndex(MainWindow *window) {
  auto *stacked = window->findChild<QStackedWidget *>();
  return stacked ? stacked->currentIndex() : -1;
}

static int totalPageCount(MainWindow *window) {
  auto *stacked = window->findChild<QStackedWidget *>();
  return stacked ? stacked->count() : 0;
}

static QJsonObject focusedWidgetInfo() {
  QJsonObject info;
  QWidget *focused = QApplication::focusWidget();
  if (focused) {
    info["class"] = QString(focused->metaObject()->className());
    info["objectName"] = focused->objectName();
    info["visible"] = focused->isVisible();
    info["enabled"] = focused->isEnabled();
    // Walk up to find the nearest named ancestor
    QWidget *ancestor = focused->parentWidget();
    while (ancestor) {
      if (!ancestor->objectName().isEmpty()) {
        info["parentObjectName"] = ancestor->objectName();
        break;
      }
      ancestor = ancestor->parentWidget();
    }
  } else {
    info["class"] = QJsonValue::Null;
    info["objectName"] = QJsonValue::Null;
  }
  return info;
}

// ─── GET /state — full application state snapshot ────────────────────────

RemoteControlServer::HttpResponse RemoteControlServer::handleState() {
  QJsonObject obj;
  obj["ok"] = true;
  obj["pid"] = static_cast<qint64>(QCoreApplication::applicationPid());
  obj["focused"] = window_->isActiveWindow();
  obj["page"] = currentPageName(window_);
  obj["pageIndex"] = currentPageIndex(window_);
  obj["pageCount"] = totalPageCount(window_);
  obj["focusWidget"] = focusedWidgetInfo();

  // Input mode
  const auto &input = AIO::Input::InputManager::instance();
  const bool isController =
      (window_->currentInputMode == MainWindow::InputMode::Controller);
  obj["inputMode"] = isController ? "controller" : "mouse";
  obj["inputContext"] =
      (input.activeContext() == AIO::Input::InputContext::Emulator) ? "emulator"
                                                                    : "ui";

  // Emulator state
  obj["emulatorRunning"] = window_->emulatorRunning.load();
  obj["emulatorPaused"] = window_->emulatorPaused.load();

  // Navigation state (homescreen)
  auto *homeScreen = window_->homeScreen_;
  if (homeScreen &&
      currentPageName(window_).contains("Home", Qt::CaseInsensitive)) {
    QJsonObject nav;
    nav["focusRow"] = homeScreen->focusRow_;
    nav["focusCol"] = homeScreen->focusCol_;
    // Get the focused tile name
    if (homeScreen->focusRow_ >= 0 &&
        homeScreen->focusRow_ < homeScreen->rows_.size()) {
      const auto &row = homeScreen->rows_[homeScreen->focusRow_];
      if (homeScreen->focusCol_ >= 0 && homeScreen->focusCol_ < row.size()) {
        auto *tile = row[homeScreen->focusCol_];
        // Map kind to string
        const char *names[] = {"GBA",      "PS1",     "Switch",  "Media",
                               "Settings", "YouTube", "Netflix", "Disney+",
                               "Hulu",     "Blank"};
        int kindIdx = static_cast<int>(tile->kind());
        nav["focusedTile"] =
            (kindIdx >= 0 && kindIdx < 10) ? names[kindIdx] : "unknown";
        nav["focusProgress"] = tile->focusProgress();
      }
    }
    nav["rowCount"] = homeScreen->rows_.size();
    obj["navigation"] = nav;
  }

  // Window geometry
  QJsonObject geom;
  geom["x"] = window_->x();
  geom["y"] = window_->y();
  geom["width"] = window_->width();
  geom["height"] = window_->height();
  obj["geometry"] = geom;

  return jsonResponse(200, QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// ─── GET /state/navigation — detailed navigation state ───────────────────

RemoteControlServer::HttpResponse RemoteControlServer::handleStateNavigation() {
  QJsonObject obj;
  obj["ok"] = true;
  obj["page"] = currentPageName(window_);
  obj["focusWidget"] = focusedWidgetInfo();

  // NavigationController state
  const auto &navCtrl = window_->nav;
  obj["hoveredIndex"] = navCtrl.hoveredIndex();
  obj["hasAdapter"] = (navCtrl.adapter() != nullptr);
  if (navCtrl.adapter()) {
    obj["adapterItemCount"] = navCtrl.adapter()->itemCount();
  }

  // HomeScreen-specific grid state
  auto *homeScreen = window_->homeScreen_;
  if (homeScreen) {
    QJsonObject grid;
    grid["focusRow"] = homeScreen->focusRow_;
    grid["focusCol"] = homeScreen->focusCol_;
    grid["rowCount"] = homeScreen->rows_.size();

    // List all tiles with their state
    QJsonArray tilesArray;
    const char *kindNames[] = {"GBA",      "PS1",     "Switch",  "Media",
                               "Settings", "YouTube", "Netflix", "Disney+",
                               "Hulu",     "Blank"};
    for (int r = 0; r < homeScreen->rows_.size(); ++r) {
      const auto &row = homeScreen->rows_[r];
      for (int c = 0; c < row.size(); ++c) {
        auto *tile = row[c];
        QJsonObject tileObj;
        tileObj["row"] = r;
        tileObj["col"] = c;
        int ki = static_cast<int>(tile->kind());
        tileObj["kind"] = (ki >= 0 && ki < 10) ? kindNames[ki] : "unknown";
        tileObj["focused"] =
            (r == homeScreen->focusRow_ && c == homeScreen->focusCol_);
        tileObj["focusProgress"] = tile->focusProgress();
        tilesArray.append(tileObj);
      }
    }
    grid["tiles"] = tilesArray;
    obj["homeGrid"] = grid;
  }

  return jsonResponse(200, QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// ─── GET /state/input — input mode and device state ─────────────────────

RemoteControlServer::HttpResponse RemoteControlServer::handleStateInput() {
  QJsonObject obj;
  obj["ok"] = true;

  // Current GUI input mode
  const bool isController =
      (window_->currentInputMode == MainWindow::InputMode::Controller);
  obj["inputMode"] = isController ? "controller" : "mouse";

  // InputManager state
  const auto &input = AIO::Input::InputManager::instance();
  obj["inputContext"] =
      (input.activeContext() == AIO::Input::InputContext::Emulator) ? "emulator"
                                                                    : "ui";

  // Logical button state (GBA convention: 1=released, 0=pressed)
  uint32_t buttons = input.logicalButtonsDown();
  obj["logicalButtonsRaw"] = static_cast<qint64>(buttons);

  // Decode active presses
  QJsonArray pressed;
  const char *buttonNames[] = {
      "A",    "B", "Select", "Start",   "Right",  "Left", "Up",
      "Down", "R", "L",      "Confirm", "Cancel", "Menu", "SystemHome"};
  for (int i = 0; i < 14; ++i) {
    if (!(buttons & (1u << i))) { // Active-low: 0 = pressed
      pressed.append(buttonNames[i]);
    }
  }
  obj["pressedButtons"] = pressed;

  return jsonResponse(200, QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// ─── GET /state/emulator — emulator runtime info ─────────────────────────

RemoteControlServer::HttpResponse RemoteControlServer::handleStateEmulator() {
  QJsonObject obj;
  obj["ok"] = true;
  obj["running"] = window_->emulatorRunning.load();
  obj["paused"] = window_->emulatorPaused.load();
  obj["frameNumber"] = static_cast<qint64>(window_->emulatorFrameNumber.load());

  // Emulator type
  const char *emuTypes[] = {"none", "GBA", "Switch", "PS1"};
  int emuIdx = static_cast<int>(window_->currentEmulator);
  obj["type"] = (emuIdx >= 0 && emuIdx < 4) ? emuTypes[emuIdx] : "unknown";

  if (window_->emulatorRunning.load()) {
    obj["emulatedMs"] = static_cast<qint64>(window_->GetEmulatedMilliseconds());
    obj["audioRecording"] = window_->IsAudioRecording();
    obj["avRecording"] = window_->IsAVRecording();
  }

  return jsonResponse(200, QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// ─── GET /state/widgets — visible widget tree with geometry ──────────────

static QJsonObject widgetToJson(QWidget *w, int depth, int maxDepth) {
  QJsonObject obj;
  obj["class"] = QString(w->metaObject()->className());
  if (!w->objectName().isEmpty())
    obj["objectName"] = w->objectName();
  obj["visible"] = w->isVisible();
  obj["enabled"] = w->isEnabled();

  // Geometry in parent coordinates
  QJsonObject geom;
  geom["x"] = w->x();
  geom["y"] = w->y();
  geom["w"] = w->width();
  geom["h"] = w->height();
  obj["geometry"] = geom;

  // Text content (labels, buttons)
  if (auto *label = qobject_cast<QLabel *>(w)) {
    const QString text = label->text();
    if (!text.isEmpty())
      obj["text"] = text;
  } else if (auto *button = qobject_cast<QPushButton *>(w)) {
    obj["text"] = button->text();
    if (button->property("aio_selected").toBool())
      obj["selected"] = true;
    if (button->property("variant").isValid())
      obj["variant"] = button->property("variant").toString();
    if (button->property("system").isValid())
      obj["system"] = button->property("system").toString();
  }

  // Dynamic properties of interest
  if (w->property("aio_selected").toBool())
    obj["aio_selected"] = true;
  if (w->property("role").isValid())
    obj["role"] = w->property("role").toString();

  // Recurse into children (limit depth)
  if (depth < maxDepth) {
    QJsonArray children;
    for (auto *child :
         w->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
      if (child->isVisible())
        children.append(widgetToJson(child, depth + 1, maxDepth));
    }
    if (!children.isEmpty())
      obj["children"] = children;
  }

  return obj;
}

RemoteControlServer::HttpResponse RemoteControlServer::handleStateWidgets() {
  QJsonObject obj;
  obj["ok"] = true;
  obj["page"] = currentPageName(window_);

  // Get max depth from query (?depth=N, default 4)
  int maxDepth = 4;

  auto *stacked = window_->findChild<QStackedWidget *>();
  if (stacked) {
    QWidget *current = stacked->currentWidget();
    if (current)
      obj["widgetTree"] = widgetToJson(current, 0, maxDepth);
  }

  return jsonResponse(200, QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// ─── GET /state/page — detailed current-page introspection ──────────────

RemoteControlServer::HttpResponse RemoteControlServer::handleStatePage() {
  QJsonObject obj;
  obj["ok"] = true;

  const QString page = currentPageName(window_);
  obj["page"] = page;
  obj["pageIndex"] = currentPageIndex(window_);
  obj["pageCount"] = totalPageCount(window_);
  obj["focusWidget"] = focusedWidgetInfo();
  obj["windowFocused"] = window_->isActiveWindow();

  // Window geometry
  QJsonObject geom;
  geom["x"] = window_->x();
  geom["y"] = window_->y();
  geom["width"] = window_->width();
  geom["height"] = window_->height();
  obj["geometry"] = geom;

  // Collect all visible text on the current page
  auto *stacked = window_->findChild<QStackedWidget *>();
  if (stacked) {
    QWidget *current = stacked->currentWidget();
    if (current) {
      QJsonArray texts;
      // Labels
      for (auto *label : current->findChildren<QLabel *>()) {
        if (label->isVisible() && !label->text().isEmpty()) {
          QJsonObject t;
          t["type"] = "label";
          t["text"] = label->text();
          if (!label->objectName().isEmpty())
            t["objectName"] = label->objectName();
          if (label->property("role").isValid())
            t["role"] = label->property("role").toString();
          texts.append(t);
        }
      }
      // Buttons
      for (auto *btn : current->findChildren<QPushButton *>()) {
        if (btn->isVisible()) {
          QJsonObject t;
          t["type"] = "button";
          t["text"] = btn->text();
          if (!btn->objectName().isEmpty())
            t["objectName"] = btn->objectName();
          t["selected"] = btn->property("aio_selected").toBool();
          t["enabled"] = btn->isEnabled();
          // Geometry relative to page
          QPoint pos = btn->mapTo(current, QPoint(0, 0));
          QJsonObject bg;
          bg["x"] = pos.x();
          bg["y"] = pos.y();
          bg["w"] = btn->width();
          bg["h"] = btn->height();
          t["geometry"] = bg;
          texts.append(t);
        }
      }
      obj["visibleElements"] = texts;
    }
  }

  // Adapter state (button list navigation)
  const auto &navCtrl = window_->nav;
  if (navCtrl.adapter()) {
    QJsonObject adapterObj;
    adapterObj["itemCount"] = navCtrl.adapter()->itemCount();
    adapterObj["hoveredIndex"] = navCtrl.hoveredIndex();
    obj["adapter"] = adapterObj;
  }

  // Page-specific enrichments
  if (page.contains("Home", Qt::CaseInsensitive)) {
    auto *homeScreen = window_->homeScreen_;
    if (homeScreen) {
      QJsonObject nav;
      nav["focusRow"] = homeScreen->focusRow_;
      nav["focusCol"] = homeScreen->focusCol_;
      nav["rowCount"] = homeScreen->rows_.size();
      const char *kindNames[] = {"GBA",      "PS1",     "Switch",  "Media",
                                 "Settings", "YouTube", "Netflix", "Disney+",
                                 "Hulu",     "Blank"};
      if (homeScreen->focusRow_ >= 0 &&
          homeScreen->focusRow_ < homeScreen->rows_.size()) {
        const auto &row = homeScreen->rows_[homeScreen->focusRow_];
        if (homeScreen->focusCol_ >= 0 && homeScreen->focusCol_ < row.size()) {
          int ki = static_cast<int>(row[homeScreen->focusCol_]->kind());
          nav["focusedTile"] = (ki >= 0 && ki < 10) ? kindNames[ki] : "unknown";
        }
      }
      obj["homeNavigation"] = nav;
    }
  }

  if (page.contains("Emulator", Qt::CaseInsensitive) ||
      page.contains("Game", Qt::CaseInsensitive)) {
    const char *emuTypes[] = {"none", "GBA", "Switch", "PS1"};
    int emuIdx = static_cast<int>(window_->currentEmulator);
    obj["emulatorType"] =
        (emuIdx >= 0 && emuIdx < 4) ? emuTypes[emuIdx] : "unknown";
    obj["emulatorRunning"] = window_->emulatorRunning.load();
  }

  return jsonResponse(200, QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// ─── POST /execute — programmable tool-call endpoint ─────────────────────
//
// Accepts: { "action": "<name>", "params": { ... } }
//
// Available actions:
//   navigate       — go to a named page (home, settings, emulators, nas,
//                    streaming:<app>)
//   select         — on homescreen, move selection to {row, col}
//   press          — press a key (shorthand for /input/key)
//   get-text       — return all visible text on the current page
//   get-property   — read a widget property by objectName
//   list-pages     — enumerate all pages in the stacked widget
//   list-actions   — return the list of supported actions
//   dump-frame     — write current emulator frame to a file path
//   set-emulator   — set emulator type before launching (gba, ps1, switch)

RemoteControlServer::HttpResponse
RemoteControlServer::handleExecute(const HttpRequest &req) {
  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(req.body, &err);
  if (doc.isNull())
    return errorResponse(400, "Invalid JSON: " + err.errorString());

  QJsonObject body = doc.object();
  const QString action = body["action"].toString().toLower().trimmed();
  const QJsonObject params = body["params"].toObject();

  if (action.isEmpty())
    return errorResponse(400, "Missing 'action' field");

  // ── navigate ──────────────────────────────────────────────────────
  if (action == "navigate") {
    const QString target = params["page"].toString().toLower().trimmed();
    if (target.isEmpty())
      return errorResponse(400, "navigate: missing 'page' param");

    if (target == "home" || target == "main" || target == "menu") {
      QMetaObject::invokeMethod(window_, &MainWindow::goToMainMenu,
                                Qt::QueuedConnection);
    } else if (target == "settings") {
      QMetaObject::invokeMethod(window_, &MainWindow::goToSettings,
                                Qt::QueuedConnection);
    } else if (target == "emulators" || target == "emulator-select") {
      QMetaObject::invokeMethod(window_, &MainWindow::goToEmulatorSelect,
                                Qt::QueuedConnection);
    } else if (target == "gba") {
      QMetaObject::invokeMethod(
          window_,
          [win = window_]() {
            win->currentEmulator = MainWindow::EmulatorType::GBA;
            win->goToGameSelect();
          },
          Qt::QueuedConnection);
    } else if (target == "ps1" || target == "playstation") {
      QMetaObject::invokeMethod(
          window_,
          [win = window_]() {
            win->currentEmulator = MainWindow::EmulatorType::PS1;
            win->goToGameSelect();
          },
          Qt::QueuedConnection);
    } else if (target == "switch" || target == "nintendo-switch") {
      QMetaObject::invokeMethod(
          window_,
          [win = window_]() {
            win->currentEmulator = MainWindow::EmulatorType::Switch;
            win->goToGameSelect();
          },
          Qt::QueuedConnection);
    } else if (target == "games" || target == "game-select") {
      QMetaObject::invokeMethod(window_, &MainWindow::goToGameSelect,
                                Qt::QueuedConnection);
    } else if (target == "nas") {
      QMetaObject::invokeMethod(window_, &MainWindow::goToNAS,
                                Qt::QueuedConnection);
    } else if (target.startsWith("streaming:") || target == "youtube" ||
               target == "netflix" || target == "disney+" || target == "hulu") {
      int app = -1;
      QString appName = target;
      if (target.startsWith("streaming:"))
        appName = target.mid(10);
      if (appName == "youtube")
        app = 0;
      else if (appName == "netflix")
        app = 1;
      else if (appName == "disney+" || appName == "disneyplus" ||
               appName == "disney")
        app = 2;
      else if (appName == "hulu")
        app = 3;
      if (app < 0)
        return errorResponse(400,
                             "navigate: unknown streaming app: " + appName);
      QMetaObject::invokeMethod(
          window_, [win = window_, app]() { win->launchStreamingApp(app); },
          Qt::QueuedConnection);
    } else if (target == "emulator-settings") {
      QMetaObject::invokeMethod(window_, &MainWindow::goToEmulatorSettings,
                                Qt::QueuedConnection);
    } else {
      return errorResponse(400, "navigate: unknown page: " + target);
    }

    QJsonObject resp;
    resp["ok"] = true;
    resp["action"] = "navigate";
    resp["target"] = target;
    return jsonResponse(200,
                        QJsonDocument(resp).toJson(QJsonDocument::Compact));
  }

  // ── select ────────────────────────────────────────────────────────
  if (action == "select") {
    auto *homeScreen = window_->homeScreen_;
    if (!homeScreen)
      return errorResponse(400, "select: homescreen not available");

    const int row = params["row"].toInt(-1);
    const int col = params["col"].toInt(-1);
    if (row < 0 || col < 0)
      return errorResponse(400, "select: missing row/col");
    if (row >= homeScreen->rows_.size())
      return errorResponse(400, "select: row out of range");
    if (col >= homeScreen->rows_[row].size())
      return errorResponse(400, "select: col out of range");

    QMetaObject::invokeMethod(
        homeScreen,
        [homeScreen, row, col]() {
          homeScreen->focusRow_ = row;
          homeScreen->focusCol_ = col;
          homeScreen->updateFocus();
        },
        Qt::QueuedConnection);

    const char *kindNames[] = {"GBA",      "PS1",     "Switch",  "Media",
                               "Settings", "YouTube", "Netflix", "Disney+",
                               "Hulu",     "Blank"};
    int ki = static_cast<int>(homeScreen->rows_[row][col]->kind());

    QJsonObject resp;
    resp["ok"] = true;
    resp["action"] = "select";
    resp["row"] = row;
    resp["col"] = col;
    resp["tile"] = (ki >= 0 && ki < 10) ? kindNames[ki] : "unknown";
    return jsonResponse(200,
                        QJsonDocument(resp).toJson(QJsonDocument::Compact));
  }

  // ── press ─────────────────────────────────────────────────────────
  if (action == "press") {
    const QString keyName = params["key"].toString();
    if (keyName.isEmpty())
      return errorResponse(400, "press: missing 'key' param");
    const int qtKey = resolveQtKey(keyName);
    if (qtKey == 0)
      return errorResponse(400, "press: unknown key: " + keyName);

    const int count = qBound(1, params["count"].toInt(1), 20);
    const int delay = qBound(30, params["delay_ms"].toInt(50), 2000);

    for (int i = 0; i < count; ++i) {
      QTimer::singleShot(i * delay, window_, [this, qtKey]() {
        const QString text = keyTextForQt(qtKey);
        QKeyEvent press(QEvent::KeyPress, qtKey, Qt::NoModifier, text);
        QCoreApplication::sendEvent(window_, &press);
        QKeyEvent release(QEvent::KeyRelease, qtKey, Qt::NoModifier, text);
        QCoreApplication::sendEvent(window_, &release);
      });
    }

    QJsonObject resp;
    resp["ok"] = true;
    resp["action"] = "press";
    resp["key"] = keyName;
    resp["count"] = count;
    return jsonResponse(200,
                        QJsonDocument(resp).toJson(QJsonDocument::Compact));
  }

  // ── get-text ──────────────────────────────────────────────────────
  if (action == "get-text") {
    auto *stacked = window_->findChild<QStackedWidget *>();
    QJsonArray texts;
    if (stacked) {
      QWidget *current = stacked->currentWidget();
      if (current) {
        for (auto *label : current->findChildren<QLabel *>()) {
          if (label->isVisible() && !label->text().isEmpty())
            texts.append(label->text());
        }
        for (auto *btn : current->findChildren<QPushButton *>()) {
          if (btn->isVisible() && !btn->text().isEmpty())
            texts.append(btn->text());
        }
      }
    }
    QJsonObject resp;
    resp["ok"] = true;
    resp["action"] = "get-text";
    resp["page"] = currentPageName(window_);
    resp["texts"] = texts;
    return jsonResponse(200,
                        QJsonDocument(resp).toJson(QJsonDocument::Compact));
  }

  // ── get-property ──────────────────────────────────────────────────
  if (action == "get-property") {
    const QString widgetName = params["objectName"].toString();
    const QString propName = params["property"].toString();
    if (widgetName.isEmpty() || propName.isEmpty())
      return errorResponse(400, "get-property: missing objectName or property");

    QWidget *target = window_->findChild<QWidget *>(widgetName);
    if (!target)
      return errorResponse(404,
                           "get-property: widget not found: " + widgetName);

    QVariant val = target->property(propName.toUtf8().constData());
    QJsonObject resp;
    resp["ok"] = true;
    resp["action"] = "get-property";
    resp["objectName"] = widgetName;
    resp["property"] = propName;
    resp["value"] = QJsonValue::fromVariant(val);
    return jsonResponse(200,
                        QJsonDocument(resp).toJson(QJsonDocument::Compact));
  }

  // ── list-pages ────────────────────────────────────────────────────
  if (action == "list-pages") {
    auto *stacked = window_->findChild<QStackedWidget *>();
    QJsonArray pages;
    if (stacked) {
      for (int i = 0; i < stacked->count(); ++i) {
        QWidget *page = stacked->widget(i);
        QJsonObject p;
        p["index"] = i;
        p["objectName"] = page->objectName();
        p["className"] = QString(page->metaObject()->className());
        p["current"] = (i == stacked->currentIndex());
        pages.append(p);
      }
    }
    QJsonObject resp;
    resp["ok"] = true;
    resp["action"] = "list-pages";
    resp["pages"] = pages;
    return jsonResponse(200,
                        QJsonDocument(resp).toJson(QJsonDocument::Compact));
  }

  // ── dump-frame ────────────────────────────────────────────────────
  if (action == "dump-frame") {
    const QString path = params["path"].toString();
    if (path.isEmpty())
      return errorResponse(400, "dump-frame: missing 'path' param");

    double nonBlackRatio = 0.0;
    bool ok = window_->DumpCurrentFramePPM(path.toStdString(), &nonBlackRatio);

    QJsonObject resp;
    resp["ok"] = ok;
    resp["action"] = "dump-frame";
    resp["path"] = path;
    resp["nonBlackRatio"] = nonBlackRatio;
    if (!ok)
      resp["error"] = "Failed to write frame";
    return jsonResponse(ok ? 200 : 500,
                        QJsonDocument(resp).toJson(QJsonDocument::Compact));
  }

  // ── list-actions ──────────────────────────────────────────────────
  if (action == "list-actions") {
    QJsonArray actions;
    auto addAction = [&](const char *name, const char *desc,
                         const char *paramsDesc) {
      QJsonObject a;
      a["name"] = name;
      a["description"] = desc;
      a["params"] = paramsDesc;
      actions.append(a);
    };
    addAction("navigate", "Go to a named page",
              "{page: "
              "home|settings|emulators|gba|ps1|switch|games|nas|youtube|"
              "netflix|disney+|hulu|emulator-settings}");
    addAction("select", "Move homescreen selection to a tile",
              "{row: int, col: int}");
    addAction("press", "Press a key (with optional repeat)",
              "{key: string, count?: int, delay_ms?: int}");
    addAction("get-text", "Return all visible text on the current page", "{}");
    addAction("get-property", "Read a Qt property from a named widget",
              "{objectName: string, property: string}");
    addAction("list-pages", "Enumerate all pages in the stacked widget", "{}");
    addAction("dump-frame", "Write current emulator frame to a PPM file",
              "{path: string}");
    addAction("list-actions", "Return this action catalog", "{}");

    QJsonObject resp;
    resp["ok"] = true;
    resp["action"] = "list-actions";
    resp["actions"] = actions;
    return jsonResponse(200,
                        QJsonDocument(resp).toJson(QJsonDocument::Compact));
  }

  return errorResponse(400, "Unknown action: " + action);
}

} // namespace GUI
} // namespace AIO
