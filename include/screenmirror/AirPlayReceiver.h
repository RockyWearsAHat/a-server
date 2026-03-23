#pragma once

#include "screenmirror/AirPlayPairing.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

#include <cstdint>
#include <memory>

#ifdef __APPLE__
#include <dns_sd.h>
#endif

namespace AIO::ScreenMirror {

/// AirPlay receiver that advertises via Bonjour and handles incoming
/// connections. Implements service advertisement, the AirPlay HTTP/RTSP
/// protocol layer, NTP timing, and H.264 video decode via VideoToolbox.
class AirPlayReceiver final : public QObject {
  Q_OBJECT

public:
  enum class State { Stopped, Advertising, Connected, Mirroring };
  Q_ENUM(State)

  explicit AirPlayReceiver(QObject *parent = nullptr);
  ~AirPlayReceiver() override;

  /// Start advertising and listening. Returns false on critical failure.
  bool start(uint16_t port = 7000);

  /// Stop advertising and close all connections.
  void stop();

  State state() const { return state_; }
  uint16_t port() const { return port_; }
  uint16_t eventPort() const { return eventPort_; }
  uint16_t timingPort() const { return timingPort_; }
  uint16_t dataPort() const { return dataPort_; }
  QString deviceName() const { return deviceName_; }
  void setDeviceName(const QString &name);

  /// Connected client info (empty when not connected).
  QString clientName() const { return clientName_; }

signals:
  void stateChanged(AIO::ScreenMirror::AirPlayReceiver::State newState);
  void clientConnected(const QString &clientName);
  void clientDisconnected();
  void frameReceived(const QImage &frame);
  void errorOccurred(const QString &message);

private slots:
  void onNewConnection();
  void onClientData();
  void onClientDisconnected();
  void onEventClientConnected();
  void onEventClientData();
  void onDataSocketData();
  void sendNtpRequest();
  void onTimingSocketData();

private:
  bool startMdnsAirPlay(uint16_t port);
  bool startMdnsRaop(uint16_t port);
  void stopMdns();
  bool startHttpServer(uint16_t port);
  void handleHttpRequest(QTcpSocket *socket, const QByteArray &data);
  void handleGetInfo(QTcpSocket *socket);
  void handleServerInfo(QTcpSocket *socket);
  void handlePairSetup(QTcpSocket *socket, const QByteArray &data);
  void handlePairVerify(QTcpSocket *socket, const QByteArray &data);
  void handleFeedback(QTcpSocket *socket);
  void handleOptions(QTcpSocket *socket);
  void handleFpSetup(QTcpSocket *socket, const QByteArray &data);
  void handleSetup(QTcpSocket *socket, const QByteArray &data);
  void handleRecord(QTcpSocket *socket);
  void handleTeardown(QTcpSocket *socket);
  void handleGetParameter(QTcpSocket *socket);
  void handleSetParameter(QTcpSocket *socket);
  void handleFlush(QTcpSocket *socket);
  void handleConfigure(QTcpSocket *socket, const QByteArray &data);
  void handleAuthSetup(QTcpSocket *socket);
  void sendHttpResponse(QTcpSocket *socket, int statusCode,
                        const QByteArray &contentType, const QByteArray &body);
  void setState(State s);
  QString generateDeviceId() const;

  // Binary plist helpers for SETUP body parsing
  uint16_t parseBplistPort(const QByteArray &plist, const QString &key) const;
  QString parseBplistString(const QByteArray &plist, const QString &key) const;

  // Video pipeline
  void processVideoCodecData(const QByteArray &payload);
  void processVideoFrame(const QByteArray &payload, uint64_t ntpTimestamp);
  void initVideoToolbox();
  void teardownVideoToolbox();

public:
  QImage convertCVImageBufferToQImage(void *cvImageBuffer);

private:
  // ── Post-pairing encryption (ChaCha20-Poly1305) ────────────────────
  void enableEncryption(QTcpSocket *socket);
  QByteArray decryptFrame(QTcpSocket *socket, QByteArray &buf);
  QByteArray encryptFrame(QTcpSocket *socket, const QByteArray &plaintext);

  State state_ = State::Stopped;
  uint16_t port_ = 7000;
  QString deviceName_ = QStringLiteral("AIO Server");
  QString deviceId_;
  QString clientName_;
  QString pairingUuid_; // persistent pi= UUID advertised via mDNS

  // HAP pairing state — one instance persists across connections.
  AirPlayPairing pairing_;

  int lastCSeq_ = 0; // CSeq value parsed from last request; echoed in response
  QByteArray lastProtocol_ =
      "HTTP/1.1"; // request protocol echoed in responses (HTTP/1.1 or RTSP/1.0)

  QTcpServer *httpServer_ = nullptr;
  QTcpSocket *activeClient_ = nullptr;

  // Event channel: iOS expects a TCP server (not UDP).
  QTcpServer *eventServer_ = nullptr;
  QTcpSocket *eventClient_ = nullptr;
  uint16_t eventPort_ = 0;

  QUdpSocket *timingSocket_ = nullptr;
  QUdpSocket *dataSocket_ = nullptr;
  uint16_t timingPort_ = 0;
  uint16_t dataPort_ = 0;
  QHash<QTcpSocket *, QByteArray> socketBuffers_;

  // NTP timing state
  QTimer *ntpTimer_ = nullptr;
  QHostAddress clientTimingAddr_;
  uint16_t clientTimingPort_ = 0;
  QString timingProtocol_;

  // Video stream state (SPS/PPS from codec data)
  QByteArray videoSps_;
  QByteArray videoPps_;

#ifdef __APPLE__
  // VideoToolbox decode session
  void *vtDecompSession_ = nullptr; // VTDecompressionSessionRef
  void *vtFormatDesc_ = nullptr;    // CMVideoFormatDescriptionRef
#endif

  // Per-socket encryption state (enabled after pair-verify completes).
  struct EncryptionState {
    QByteArray key;
    uint64_t readNonce = 0;
    uint64_t writeNonce = 0;
    bool active = false;
  };
  QHash<QTcpSocket *, EncryptionState> encryptionState_;

#ifdef __APPLE__
  DNSServiceRef mdnsRef_ = nullptr;     // _airplay._tcp
  DNSServiceRef raopMdnsRef_ = nullptr; // _raop._tcp
#endif
};

} // namespace AIO::ScreenMirror
