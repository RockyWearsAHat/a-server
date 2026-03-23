#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTimer>
#include <QVector>
#include <functional>

class QMainWindow;
class QTcpSocket;

namespace AIO {
namespace GUI {

class MainWindow;

/**
 * @brief Lightweight localhost-only HTTP server for programmatic input
 * injection and application state polling.
 *
 * Allows external tools (e.g. visual_dev_loop.py) to send key/mouse events
 * directly into Qt's event system without requiring window focus, and to
 * query the application's current state for visual testing automation.
 *
 * Input Endpoints:
 *   POST /input/key          — single key press/down/up
 *   POST /input/key-sequence  — multiple keys with delay
 *   POST /input/type          — type text character by character
 *   POST /input/click         — mouse click at coordinates
 *
 * State Endpoints:
 *   GET  /status              — current page, focus state, PID
 *   GET  /state               — full application state snapshot
 *   GET  /state/navigation    — navigation selection details
 *   GET  /state/input         — input mode and controller state
 *   GET  /state/emulator      — emulator runtime info (if running)
 *   GET  /state/audio         — live audio metrics (RMS, silence, clipping)
 *   GET  /state/widgets       — visible widget tree with geometry
 *   GET  /state/page          — detailed current-page introspection
 *   GET  /health              — liveness check
 *   GET  /events              — chronological ring buffer of app events
 *                               (?since=<epochMs>&limit=<N>)
 *
 * Programmable Endpoint:
 *   POST /execute             — run a named action with parameters
 *                               (navigate, query, set-property, etc.)
 */
class RemoteControlServer final : public QObject {
  Q_OBJECT

public:
  explicit RemoteControlServer(MainWindow *window, QObject *parent = nullptr);

  /**
   * @brief Start listening. Tries the configured port, then up to 5 fallbacks.
   * @return true if listening.
   */
  bool Start();

  void Stop();

  [[nodiscard]] bool IsRunning() const;
  [[nodiscard]] quint16 Port() const;

private slots:
  void onNewConnection();
  void onMonitorTick();

private:
  // ── Event ring buffer ────────────────────────────────────────────────
  struct RCEvent {
    qint64 timestampMs = 0; // ms since RC server started
    QString type;
    QJsonObject data;
  };
  static constexpr int kMaxEvents = 500;
  QVector<RCEvent> eventLog_;
  qint64 startTimeMs_ = 0;

  // Emulator monitor state (updated each tick)
  bool prevRunning_ = false;
  bool prevPaused_ = false;
  quint64 prevFrameNumber_ = 0;
  qint64 lastFrameAdvanceMs_ = 0;
  bool prevAudioSilence_ = false;

  QTimer *monitorTimer_ = nullptr;

  void appendEvent(const QString &type, const QJsonObject &data = {});
  void initEventMonitor();

  // ── HTTP infra ───────────────────────────────────────────────────────
  struct HttpRequest {
    QString method;
    QString path;
    QString query;
    QMap<QString, QString> headers;
    QByteArray body;
  };

  struct HttpResponse {
    int status = 200;
    QByteArray body;
    QString contentType;
  };

  MainWindow *window_;
  QTcpServer server_;
  quint16 configuredPort_ = 9876;

  void handleSocket(QTcpSocket *socket);
  static bool tryParseHttpRequest(QByteArray &buffer, HttpRequest &outReq);
  static void writeResponse(QTcpSocket *socket, const HttpResponse &resp);
  static QByteArray statusText(int status);

  HttpResponse route(const HttpRequest &req);
  HttpResponse handleKeyInput(const HttpRequest &req);
  HttpResponse handleKeySequence(const HttpRequest &req);
  HttpResponse handleTypeInput(const HttpRequest &req);
  HttpResponse handleClickInput(const HttpRequest &req);
  HttpResponse handleStatus();
  HttpResponse handleHealth();
  HttpResponse handleState();
  HttpResponse handleStateNavigation();
  HttpResponse handleStateInput();
  HttpResponse handleStateEmulator();
  HttpResponse handleStateAudio();
  HttpResponse handleStateWidgets();
  HttpResponse handleStatePage();
  HttpResponse handleEvents(const HttpRequest &req);
  HttpResponse handleExecute(const HttpRequest &req);

  void writePortFile(quint16 port);

  static int resolveQtKey(const QString &name);
  static QString keyTextForQt(int qtKey);

  HttpResponse jsonResponse(int status, const QByteArray &json);
  HttpResponse errorResponse(int status, const QString &message);

  // Nav route table — built once at construction from MainWindow's goTo* slots
  // plus alias/special-case lambdas. Adding a new slot to MainWindow is all
  // that's needed; no wiring here.
  QMap<QString, std::function<void()>> navTable_;
  void buildNavTable();
  static QString methodToSlug(const QByteArray &nameWithoutGoTo);
};

} // namespace GUI
} // namespace AIO
