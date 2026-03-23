#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <memory>

namespace AIO::ScreenMirror {

class AirPlayReceiver;

/// High-level session manager for the screen mirroring feature.
/// Owns protocol receivers and exposes a clean state model for the UI.
class MirrorSessionManager final : public QObject {
  Q_OBJECT

public:
  enum class SessionState {
    Idle,       ///< Service not started
    Waiting,    ///< Advertising, waiting for a client
    Connecting, ///< Client initiated handshake
    Mirroring,  ///< Actively receiving video frames
    Error       ///< Recoverable error
  };
  Q_ENUM(SessionState)

  explicit MirrorSessionManager(QObject *parent = nullptr);
  ~MirrorSessionManager() override;

  /// Start all protocol receivers and begin advertising.
  bool startReceiving(uint16_t port = 7000);

  /// Stop all receivers and disconnect any active client.
  void stopReceiving();

  SessionState sessionState() const { return sessionState_; }

  /// Friendly name advertised to clients.
  QString deviceName() const;
  void setDeviceName(const QString &name);

  /// The first non-loopback IPv4 address of this machine.
  QString localIpAddress() const;

  /// Name of the connected client device (empty when idle/waiting).
  QString clientDeviceName() const { return clientDeviceName_; }

  /// Port the AirPlay server is listening on.
  uint16_t serverPort() const;

  /// Last error message (empty if no error).
  QString lastError() const { return lastError_; }

signals:
  void sessionStateChanged(
      AIO::ScreenMirror::MirrorSessionManager::SessionState state);
  void clientDeviceNameChanged(const QString &name);
  void frameReceived(const QImage &frame);
  void errorOccurred(const QString &message);

private:
  void setSessionState(SessionState s);

  SessionState sessionState_ = SessionState::Idle;
  QString clientDeviceName_;
  QString lastError_;

  std::unique_ptr<AirPlayReceiver> airplay_;
};

} // namespace AIO::ScreenMirror
