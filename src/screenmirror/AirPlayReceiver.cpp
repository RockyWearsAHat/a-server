#include "screenmirror/AirPlayReceiver.h"
#include "screenmirror/AirPlayTlv.h"

#include <QHostInfo>
#include <QNetworkInterface>
#include <QSettings>
#include <QSocketNotifier>
#include <QTimer>
#include <QUuid>

#include <openssl/evp.h>

#ifdef __APPLE__
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>
#include <dns_sd.h>
#endif

namespace AIO::ScreenMirror {

// AirPlay 2 feature flags — conservative values matching UxPlay/RPiPlay for
// iOS 14+ compatibility.  Format: "0xLow32,0xHigh32".
//
// Low32  0x5A7FFFF7: standard video/audio/screen-mirroring feature set.
// High32 0x1E (= 0b00011110): AudioRedundant | FPSAPv2pt5_AES_GCM |
//                              PhotoCaching | Buffering — only bits we
//                              actually implement.  Advertising higher bits
//                              (e.g. HAP pairing, MFi SNC) causes iOS to
//                              attempt authentication flows we cannot handle,
//                              which aborts pairing before M3 is ever sent.
static const char kAirPlayFeatures[] = "0x5A7FFFF7,0x1E";

// Full 64-bit features integer: (0x1ELL << 32) | 0x5A7FFFF7LL
static const int64_t kAirPlayFeaturesInt64 =
    static_cast<int64_t>(0x0000001E5A7FFFF7LL);

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

AirPlayReceiver::AirPlayReceiver(QObject *parent) : QObject(parent) {
  deviceId_ = generateDeviceId();

  // Load or generate persistent Ed25519 key pair for this accessory.
  QSettings cfg;
  cfg.beginGroup(QStringLiteral("AirPlayReceiver"));
  QByteArray ltsk =
      QByteArray::fromHex(cfg.value(QStringLiteral("ltsk")).toByteArray());
  QByteArray ltpk =
      QByteArray::fromHex(cfg.value(QStringLiteral("ltpk")).toByteArray());

  // Strip colons from deviceId so the pairing ID is a plain hex string.
  const QString accessoryId = QString(deviceId_).remove(QLatin1Char(':'));
  pairing_.init(accessoryId, ltsk, ltpk);

  // Persist newly generated keys so they survive restarts.
  if (ltsk.isEmpty()) {
    cfg.setValue(QStringLiteral("ltsk"), pairing_.privateKey().toHex());
    cfg.setValue(QStringLiteral("ltpk"), pairing_.publicKey().toHex());
  }

  // Persistent pairing identity UUID — iOS needs this to classify the device
  // as an AirPlay 2 TV destination (shown in the "TVs" section, not "other
  // devices").
  pairingUuid_ = cfg.value(QStringLiteral("pi")).toString();
  if (pairingUuid_.isEmpty()) {
    pairingUuid_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    cfg.setValue(QStringLiteral("pi"), pairingUuid_);
  }
  cfg.endGroup();
}

AirPlayReceiver::~AirPlayReceiver() { stop(); }

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool AirPlayReceiver::start(uint16_t port) {
  if (state_ != State::Stopped)
    return true;

  port_ = port;

  if (!startHttpServer(port)) {
    emit errorOccurred(
        QStringLiteral(
            "Failed to start AirPlay server — all ports unavailable. "
            "Check if macOS AirPlay Receiver is using port %1 "
            "(System Settings → AirDrop & Handoff).")
            .arg(port));
    return false;
  }

  // Use the actual bound port (may differ from requested if a fallback was
  // used).
  if (!startMdnsAirPlay(port_)) {
    emit errorOccurred(
        QStringLiteral("Failed to register _airplay._tcp Bonjour service"));
  }
  startMdnsRaop(port_); // non-fatal

  setState(State::Advertising);
  return true;
}

void AirPlayReceiver::stop() {
  if (state_ == State::Stopped)
    return;

  stopMdns();

  if (activeClient_) {
    activeClient_->disconnectFromHost();
    activeClient_ = nullptr;
  }
  socketBuffers_.clear();
  encryptionState_.clear();
  clientName_.clear();

  if (httpServer_) {
    httpServer_->close();
    httpServer_->deleteLater();
    httpServer_ = nullptr;
  }

  if (eventClient_) {
    eventClient_->close();
    eventClient_->deleteLater();
    eventClient_ = nullptr;
  }
  if (eventServer_) {
    eventServer_->close();
    eventServer_->deleteLater();
    eventServer_ = nullptr;
    eventPort_ = 0;
  }
  if (ntpTimer_) {
    ntpTimer_->stop();
    ntpTimer_->deleteLater();
    ntpTimer_ = nullptr;
  }
  if (timingSocket_) {
    timingSocket_->close();
    timingSocket_->deleteLater();
    timingSocket_ = nullptr;
    timingPort_ = 0;
  }
  if (dataSocket_) {
    dataSocket_->close();
    dataSocket_->deleteLater();
    dataSocket_ = nullptr;
    dataPort_ = 0;
  }

  clientTimingPort_ = 0;
  timingProtocol_.clear();
  teardownVideoToolbox();

  pairing_.reset();
  setState(State::Stopped);
}

void AirPlayReceiver::setDeviceName(const QString &name) {
  if (deviceName_ == name)
    return;
  deviceName_ = name;
  if (state_ != State::Stopped) {
    stopMdns();
    startMdnsAirPlay(port_);
    startMdnsRaop(port_);
  }
}

// ---------------------------------------------------------------------------
// mDNS / Bonjour
// ---------------------------------------------------------------------------

#ifdef __APPLE__

static void DNSSD_API mdnsRegisterCallback(
    DNSServiceRef /*sdRef*/, DNSServiceFlags /*flags*/,
    DNSServiceErrorType errorCode, const char * /*name*/,
    const char * /*regtype*/, const char * /*domain*/, void * /*context*/) {
  if (errorCode != kDNSServiceErr_NoError)
    qWarning("AirPlayReceiver: mDNS registration error %d", errorCode);
}

bool AirPlayReceiver::startMdnsAirPlay(uint16_t port) {
  if (mdnsRef_)
    return true;

  TXTRecordRef txt;
  TXTRecordCreate(&txt, 0, nullptr);

  auto set = [&](const char *k, const QByteArray &v) {
    TXTRecordSetValue(&txt, k, v.size(), v.constData());
  };

  set("deviceid", deviceId_.toUtf8());
  set("features", QByteArray(kAirPlayFeatures));
  set("model", QByteArrayLiteral("AppleTV3,2"));
  set("srcvers", QByteArrayLiteral("220.68"));
  set("flags", QByteArrayLiteral("0x4"));
  set("vv", QByteArrayLiteral("2"));
  set("pk", pairing_.publicKey().toHex());
  set("pi", pairingUuid_.toUtf8());
  set("pw", QByteArrayLiteral("false"));
  set("acl", QByteArrayLiteral("0"));

  const QByteArray name = deviceName_.toUtf8();

  DNSServiceErrorType err = DNSServiceRegister(
      &mdnsRef_, 0, 0, name.constData(), "_airplay._tcp", nullptr, nullptr,
      htons(port), TXTRecordGetLength(&txt), TXTRecordGetBytesPtr(&txt),
      mdnsRegisterCallback, this);

  TXTRecordDeallocate(&txt);

  if (err != kDNSServiceErr_NoError) {
    mdnsRef_ = nullptr;
    return false;
  }

  int fd = DNSServiceRefSockFD(mdnsRef_);
  if (fd >= 0) {
    auto *notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    notifier->setObjectName(QStringLiteral("mdnsNotifier"));
    connect(notifier, &QSocketNotifier::activated, this, [this]() {
      if (mdnsRef_)
        DNSServiceProcessResult(mdnsRef_);
    });
  }
  return true;
}

bool AirPlayReceiver::startMdnsRaop(uint16_t port) {
  if (raopMdnsRef_)
    return true;

  // RAOP service name: "AABBCCDDEEFF@<DisplayName>"
  const QString noColons = QString(deviceId_).remove(QLatin1Char(':'));
  const QByteArray raopName =
      (noColons + QLatin1Char('@') + deviceName_).toUtf8();

  TXTRecordRef txt;
  TXTRecordCreate(&txt, 0, nullptr);

  // Standard RAOP / AirPlay 2 TXT fields expected by iOS.
  auto set = [&](const char *k, const QByteArray &v) {
    TXTRecordSetValue(&txt, k, v.size(), v.constData());
  };
  // AirPlay 2 RAOP TXT records — values must match what iOS expects from
  // an Apple TV-class device.  Incorrect values cause iOS to either hide
  // the device or place it in the wrong section of the AirPlay picker.
  set("tp", QByteArrayLiteral("UDP"));
  set("sm", QByteArrayLiteral("false"));
  set("sv", QByteArrayLiteral("false"));
  set("ek", QByteArrayLiteral("1"));
  set("et", QByteArrayLiteral(
                "0,3,5")); // AirPlay 2: none, FairPlay SAPv2.5, transient
  set("cn", QByteArrayLiteral("0,1,2,3")); // PCM, ALAC, AAC, AAC-ELD
  set("ch", QByteArrayLiteral("2"));
  set("ss", QByteArrayLiteral("16"));
  set("sr", QByteArrayLiteral("44100"));
  set("pw", QByteArrayLiteral("false"));
  set("vn", QByteArrayLiteral("65537")); // AirPlay 2 protocol version 1.1
  set("txtvers", QByteArrayLiteral("1"));
  set("da", QByteArrayLiteral("true"));
  set("vs", QByteArrayLiteral("220.68"));
  set("md", QByteArrayLiteral("0,1,2"));
  set("ft", QByteArray(kAirPlayFeatures));
  set("am", QByteArrayLiteral("AppleTV3,2"));
  set("rhd", QByteArrayLiteral("5.6.0.0"));
  set("sf", QByteArrayLiteral("0x4"));
  set("pk", pairing_.publicKey().toHex());

  DNSServiceErrorType err = DNSServiceRegister(
      &raopMdnsRef_, 0, 0, raopName.constData(), "_raop._tcp", nullptr, nullptr,
      htons(port), TXTRecordGetLength(&txt), TXTRecordGetBytesPtr(&txt),
      mdnsRegisterCallback, this);

  TXTRecordDeallocate(&txt);

  if (err != kDNSServiceErr_NoError) {
    raopMdnsRef_ = nullptr;
    return false;
  }

  int fd = DNSServiceRefSockFD(raopMdnsRef_);
  if (fd >= 0) {
    auto *notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    notifier->setObjectName(QStringLiteral("raopMdnsNotifier"));
    connect(notifier, &QSocketNotifier::activated, this, [this]() {
      if (raopMdnsRef_)
        DNSServiceProcessResult(raopMdnsRef_);
    });
  }
  return true;
}

void AirPlayReceiver::stopMdns() {
  if (auto *n = findChild<QSocketNotifier *>(QStringLiteral("mdnsNotifier"))) {
    n->setEnabled(false);
    n->deleteLater();
  }
  if (auto *n =
          findChild<QSocketNotifier *>(QStringLiteral("raopMdnsNotifier"))) {
    n->setEnabled(false);
    n->deleteLater();
  }
  if (mdnsRef_) {
    DNSServiceRefDeallocate(mdnsRef_);
    mdnsRef_ = nullptr;
  }
  if (raopMdnsRef_) {
    DNSServiceRefDeallocate(raopMdnsRef_);
    raopMdnsRef_ = nullptr;
  }
}

#else
bool AirPlayReceiver::startMdnsAirPlay(uint16_t) { return false; }
bool AirPlayReceiver::startMdnsRaop(uint16_t) { return false; }
void AirPlayReceiver::stopMdns() {}
#endif

// ---------------------------------------------------------------------------
// HTTP server
// ---------------------------------------------------------------------------

bool AirPlayReceiver::startHttpServer(uint16_t port) {
  httpServer_ = new QTcpServer(this);
  connect(httpServer_, &QTcpServer::newConnection, this,
          &AirPlayReceiver::onNewConnection);

  // Bind explicitly to IPv4 — QHostAddress::Any maps to :: (IPv6) on macOS
  // and Qt may leave IPV6_V6ONLY set, which blocks iPhones connecting over
  // IPv4 on the local network.
  if (httpServer_->listen(QHostAddress::AnyIPv4, port)) {
    port_ = port;
    return true;
  }

  qWarning("AirPlayReceiver: port %u unavailable (%s), trying fallbacks", port,
           qPrintable(httpServer_->errorString()));

  constexpr uint16_t fallbacks[] = {7100, 47000, 47001, 0};
  for (uint16_t fb : fallbacks) {
    if (httpServer_->listen(QHostAddress::AnyIPv4, fb)) {
      port_ = (fb == 0) ? httpServer_->serverPort() : fb;
      qInfo("AirPlayReceiver: bound to fallback port %u", port_);
      return true;
    }
  }
  return false;
}

void AirPlayReceiver::onNewConnection() {
  while (httpServer_->hasPendingConnections()) {
    QTcpSocket *socket = httpServer_->nextPendingConnection();
    if (!socket)
      continue;
    connect(socket, &QTcpSocket::readyRead, this,
            &AirPlayReceiver::onClientData);
    connect(socket, &QTcpSocket::disconnected, this,
            &AirPlayReceiver::onClientDisconnected);
  }
}

void AirPlayReceiver::onClientData() {
  auto *socket = qobject_cast<QTcpSocket *>(sender());
  if (!socket)
    return;

  socketBuffers_[socket] += socket->readAll();

  // If this socket has encryption enabled, decrypt frames first.
  if (encryptionState_.contains(socket) && encryptionState_[socket].active) {
    QByteArray &buf = socketBuffers_[socket];
    while (buf.size() >= 2) {
      QByteArray plaintext = decryptFrame(socket, buf);
      if (plaintext.isEmpty())
        break; // incomplete frame or decryption error

      // Decrypted plaintext is a complete HTTP/RTSP request.
      handleHttpRequest(socket, plaintext);
    }
    if (buf.isEmpty())
      socketBuffers_.remove(socket);
    return;
  }

  // Unencrypted path: process complete HTTP/RTSP requests.
  while (socketBuffers_.contains(socket)) {
    QByteArray &buf = socketBuffers_[socket];

    const int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0)
      return; // headers not yet complete

    int contentLength = 0;
    const QByteArray headers = buf.left(headerEnd);
    const int clIdx = headers.toLower().indexOf("content-length:");
    if (clIdx >= 0) {
      const int lineEnd = headers.indexOf('\n', clIdx);
      const QByteArray clStr =
          headers.mid(clIdx + 15, lineEnd - clIdx - 15).trimmed();
      contentLength = clStr.toInt();
    }

    const int totalLen = headerEnd + 4 + contentLength;
    if (buf.size() < totalLen)
      return; // body not yet complete

    const QByteArray request = buf.left(totalLen);
    buf.remove(0, totalLen);
    const bool bufEmpty = buf.isEmpty();
    if (bufEmpty)
      socketBuffers_.remove(socket);

    handleHttpRequest(socket, request);

    if (bufEmpty)
      break;
  }
}

void AirPlayReceiver::onClientDisconnected() {
  auto *socket = qobject_cast<QTcpSocket *>(sender());
  socketBuffers_.remove(socket);
  encryptionState_.remove(socket);
  if (socket == activeClient_) {
    activeClient_ = nullptr;
    clientName_.clear();
    if (state_ == State::Connected || state_ == State::Mirroring) {
      setState(State::Advertising);
      emit clientDisconnected();
    }
  }
  // Do NOT call pairing_.reset() here.  iOS uses multiple TCP connections
  // during a single handshake (GET /info, pair-setup, pair-verify arrive on
  // different sockets).  Resetting on every disconnect wipes SRP state before
  // pair-setup can complete.  pairing_.reset() is only called in stop().
  if (socket)
    socket->deleteLater();
}

// ---------------------------------------------------------------------------
// Request dispatch
// ---------------------------------------------------------------------------

void AirPlayReceiver::handleHttpRequest(QTcpSocket *socket,
                                        const QByteArray &data) {
  // Parse CSeq header (case-insensitive) — iOS expects it echoed in the reply.
  lastCSeq_ = 0;
  const int cseqIdx = data.toLower().indexOf("cseq:");
  if (cseqIdx >= 0) {
    const int lineEnd = data.indexOf('\n', cseqIdx);
    lastCSeq_ = data.mid(cseqIdx + 5, lineEnd - cseqIdx - 5).trimmed().toInt();
  }

  // Detect request protocol (HTTP or RTSP) and echo it in every response.
  // RTSP commands (SETUP, RECORD, TEARDOWN, OPTIONS, …) must receive
  // RTSP/1.0 responses; HTTP commands (GET /info, POST /pair-setup, …)
  // must receive HTTP/1.1 responses.
  lastProtocol_ = data.contains("RTSP/1.0") ? "RTSP/1.0" : "HTTP/1.1";

  const QString request = QString::fromUtf8(data);

  // Log the request line for diagnostics.
  qInfo("AirPlayReceiver: → %s", data.left(data.indexOf('\r')).constData());

  if (request.startsWith(QLatin1String("OPTIONS"))) {
    handleOptions(socket);
    return;
  }

  if (request.startsWith(QLatin1String("GET /info")) ||
      request.startsWith(QLatin1String("GET /server-info"))) {
    handleGetInfo(socket);
    return;
  }

  if (request.startsWith(QLatin1String("POST /pair-setup"))) {
    handlePairSetup(socket, data);
    return;
  }

  if (request.startsWith(QLatin1String("POST /pair-verify"))) {
    handlePairVerify(socket, data);
    return;
  }

  if (request.startsWith(QLatin1String("POST /feedback"))) {
    handleFeedback(socket);
    return;
  }

  if (request.startsWith(QLatin1String("POST /fp-setup"))) {
    // FairPlay SAP — iOS sends a challenge before pair-setup and expects
    // a specific stub response (not an empty body).
    handleFpSetup(socket, data);
    return;
  }

  if (request.startsWith(QLatin1String("SETUP")) ||
      request.startsWith(QLatin1String("POST /setup"))) {
    handleSetup(socket, data);
    return;
  }

  if (request.startsWith(QLatin1String("RECORD"))) {
    handleRecord(socket);
    return;
  }

  if (request.startsWith(QLatin1String("TEARDOWN"))) {
    handleTeardown(socket);
    return;
  }

  if (request.startsWith(QLatin1String("SET_PARAMETER"))) {
    handleSetParameter(socket);
    return;
  }

  if (request.startsWith(QLatin1String("GET_PARAMETER"))) {
    handleGetParameter(socket);
    return;
  }

  if (request.startsWith(QLatin1String("FLUSH"))) {
    handleFlush(socket);
    return;
  }

  if (request.startsWith(QLatin1String("PAUSE"))) {
    sendHttpResponse(socket, 200, "text/plain", {});
    return;
  }

  if (request.startsWith(QLatin1String("POST /configure"))) {
    handleConfigure(socket, data);
    return;
  }

  if (request.startsWith(QLatin1String("POST /auth-setup"))) {
    handleAuthSetup(socket);
    return;
  }

  sendHttpResponse(socket, 404, "text/plain", "Not Found");
}

// ---------------------------------------------------------------------------
// Endpoint handlers
// ---------------------------------------------------------------------------

// Minimal binary plist (bplist00) writer for AirPlay /info responses.
// Supports: bool, int64, float64, data, ASCII string, array of refs, dict.
namespace {

class BPlistWriter {
public:
  int addBool(bool v) {
    objs_.append(QByteArray(1, v ? '\x09' : '\x08'));
    return objs_.size() - 1;
  }

  int addInt(int64_t v) {
    QByteArray o;
    if (v >= 0 && v <= 0xFF) {
      o.append('\x10');
      o.append(static_cast<char>(v & 0xFF));
    } else if (v >= 0 && v <= 0xFFFF) {
      o.append('\x11');
      be16(o, static_cast<uint16_t>(v));
    } else if (v >= 0 && v <= 0xFFFFFFFFLL) {
      o.append('\x12');
      be32(o, static_cast<uint32_t>(v));
    } else {
      o.append('\x13');
      be64(o, static_cast<uint64_t>(v));
    }
    objs_.append(o);
    return objs_.size() - 1;
  }

  int addReal(double v) {
    QByteArray o;
    o.append('\x23');
    uint64_t bits;
    memcpy(&bits, &v, sizeof(bits));
    be64(o, bits);
    objs_.append(o);
    return objs_.size() - 1;
  }

  int addData(const QByteArray &d) {
    QByteArray o;
    sizePrefix(o, 0x40, d.size());
    o.append(d);
    objs_.append(o);
    return objs_.size() - 1;
  }

  int addString(const char *s) { return addString(QByteArray(s)); }
  int addString(const QString &s) { return addString(s.toUtf8()); }
  int addString(const QByteArray &s) {
    QByteArray o;
    sizePrefix(o, 0x50, s.size());
    o.append(s);
    objs_.append(o);
    return objs_.size() - 1;
  }

  int addArray(const QVector<int> &refs) {
    QByteArray hdr;
    sizePrefix(hdr, 0xA0, refs.size());
    refs_.append({objs_.size(), refs});
    objs_.append(hdr);
    return objs_.size() - 1;
  }

  int addDict(const QVector<int> &keys, const QVector<int> &vals) {
    QByteArray hdr;
    sizePrefix(hdr, 0xD0, keys.size());
    QVector<int> combined = keys + vals;
    refs_.append({objs_.size(), combined});
    objs_.append(hdr);
    return objs_.size() - 1;
  }

  QByteArray serialize(int rootIdx) const {
    const int n = objs_.size();
    const int refSz = (n < 256) ? 1 : 2;

    // Build final serialized bytes for each object (header + refs).
    QVector<QByteArray> ser;
    ser.reserve(n);
    // Index mapping from obj index to its refs.
    QHash<int, QVector<int>> refMap;
    for (const auto &r : refs_)
      refMap[r.first] = r.second;

    for (int i = 0; i < n; ++i) {
      QByteArray s = objs_[i];
      if (refMap.contains(i)) {
        for (int ref : refMap[i]) {
          if (refSz == 1)
            s.append(static_cast<char>(ref & 0xFF));
          else
            be16(s, static_cast<uint16_t>(ref));
        }
      }
      ser.append(s);
    }

    // Compute offsets (relative to after "bplist00" header).
    QVector<int64_t> offsets;
    int64_t pos = 8;
    for (const auto &s : ser) {
      offsets.append(pos);
      pos += s.size();
    }
    const int64_t offTblStart = pos;
    int offSz = (offTblStart > 0xFFFF) ? 4 : (offTblStart > 0xFF) ? 2 : 1;

    QByteArray out;
    out.reserve(static_cast<int>(offTblStart) + n * offSz + 32);
    out.append("bplist00", 8);
    for (const auto &s : ser)
      out.append(s);

    // Offset table.
    for (int64_t off : offsets) {
      if (offSz == 1)
        out.append(static_cast<char>(off & 0xFF));
      else if (offSz == 2)
        be16(out, static_cast<uint16_t>(off));
      else
        be32(out, static_cast<uint32_t>(off));
    }

    // Trailer (32 bytes).
    out.append(6, '\0');
    out.append(static_cast<char>(offSz));
    out.append(static_cast<char>(refSz));
    be64(out, static_cast<uint64_t>(n));
    be64(out, static_cast<uint64_t>(rootIdx));
    be64(out, static_cast<uint64_t>(offTblStart));
    return out;
  }

private:
  QVector<QByteArray> objs_;
  QVector<QPair<int, QVector<int>>> refs_; // objIdx → refs

  static void sizePrefix(QByteArray &o, uint8_t type, int count) {
    if (count < 15) {
      o.append(static_cast<char>(type | count));
    } else {
      o.append(static_cast<char>(type | 0x0F));
      if (count <= 0xFF) {
        o.append('\x10');
        o.append(static_cast<char>(count & 0xFF));
      } else {
        o.append('\x11');
        be16(o, static_cast<uint16_t>(count));
      }
    }
  }
  static void be16(QByteArray &b, uint16_t v) {
    b.append(static_cast<char>((v >> 8) & 0xFF));
    b.append(static_cast<char>(v & 0xFF));
  }
  static void be32(QByteArray &b, uint32_t v) {
    for (int i = 3; i >= 0; --i)
      b.append(static_cast<char>((v >> (i * 8)) & 0xFF));
  }
  static void be64(QByteArray &b, uint64_t v) {
    for (int i = 7; i >= 0; --i)
      b.append(static_cast<char>((v >> (i * 8)) & 0xFF));
  }
};

} // anonymous namespace

void AirPlayReceiver::handleGetInfo(QTcpSocket *socket) {
  BPlistWriter bp;

  // ── Root dict keys ────────────────────────────────────────────────────
  int kDeviceID = bp.addString("deviceID");
  int kMacAddress = bp.addString("macAddress");
  int kPk = bp.addString("pk");
  int kFeatures = bp.addString("features");
  int kName = bp.addString("name");
  int kPi = bp.addString("pi");
  int kVv = bp.addString("vv");
  int kStatusFlags = bp.addString("statusFlags");
  int kKeepAliveLowPower = bp.addString("keepAliveLowPower");
  int kSourceVersion = bp.addString("sourceVersion");
  int kKeepAliveBody = bp.addString("keepAliveSendStatsAsBody");
  int kModel = bp.addString("model");
  int kInitialVolume = bp.addString("initialVolume");
  int kDisplays = bp.addString("displays");
  int kAudioFormats = bp.addString("audioFormats");
  int kAudioLatencies = bp.addString("audioLatencies");

  // ── Root dict values ──────────────────────────────────────────────────
  int vDeviceID = bp.addString(deviceId_.toUtf8());
  int vMacAddress = bp.addString(deviceId_.toUtf8());
  int vPk = bp.addData(pairing_.publicKey()); // raw binary, not hex
  int vFeatures = bp.addInt(kAirPlayFeaturesInt64);
  int vName = bp.addString(deviceName_.toUtf8());
  int vPi = bp.addString(pairingUuid_.toUtf8());
  int vVv = bp.addInt(2);
  // statusFlags 4 = device ready, no PIN required.
  // Bit 6 (0x40) must NOT be set — that signals PIN authentication to iOS.
  int vStatusFlags = bp.addInt(4);
  int vKeepAliveLowPower = bp.addInt(1);
  int vSourceVersion = bp.addString("220.68");
  int vKeepAliveBody = bp.addBool(true);
  int vModel = bp.addString("AppleTV3,2");
  int vInitialVolume = bp.addReal(-30.0);

  // ── displays array ────────────────────────────────────────────────────
  int dkF = bp.addString("features");
  int dkH = bp.addString("height");
  int dkHP = bp.addString("heightPhysical");
  int dkHPx = bp.addString("heightPixels");
  int dkOver = bp.addString("overscanned");
  int dkRR = bp.addString("refreshRate");
  int dkRot = bp.addString("rotation");
  int dkUuid = bp.addString("uuid");
  int dkW = bp.addString("width");
  int dkWP = bp.addString("widthPhysical");
  int dkWPx = bp.addString("widthPixels");

  int dvF = bp.addInt(14);
  int dvH = bp.addInt(1080);
  int dvHP = bp.addInt(0);
  int dvHPx = bp.addInt(1080);
  int dvOver = bp.addBool(true);
  int dvRR = bp.addReal(60.0);
  int dvRot = bp.addBool(true);
  int dvUuid = bp.addString(pairingUuid_.toUtf8());
  int dvW = bp.addInt(1920);
  int dvWP = bp.addInt(0);
  int dvWPx = bp.addInt(1920);

  int displayDict = bp.addDict(
      {dkF, dkH, dkHP, dkHPx, dkOver, dkRR, dkRot, dkUuid, dkW, dkWP, dkWPx},
      {dvF, dvH, dvHP, dvHPx, dvOver, dvRR, dvRot, dvUuid, dvW, dvWP, dvWPx});
  int vDisplays = bp.addArray({displayDict});

  // ── audioFormats array ────────────────────────────────────────────────
  int afkT = bp.addString("type");
  int afkIn = bp.addString("audioInputFormats");
  int afkOut = bp.addString("audioOutputFormats");
  int afvT = bp.addInt(96);
  int afvIn = bp.addInt(67108860);
  int afvOut = bp.addInt(67108860);
  int afDict = bp.addDict({afkT, afkIn, afkOut}, {afvT, afvIn, afvOut});
  int vAudioFormats = bp.addArray({afDict});

  // ── audioLatencies array ──────────────────────────────────────────────
  int alkT = bp.addString("type");
  int alkAT = bp.addString("audioType");
  int alkIn = bp.addString("inputLatencyMicros");
  int alkOut = bp.addString("outputLatencyMicros");
  int alvT = bp.addInt(96);
  int alvAT = bp.addString("default");
  int alvIn = bp.addInt(0);
  int alvOut = bp.addInt(400000);
  int alDict =
      bp.addDict({alkT, alkAT, alkIn, alkOut}, {alvT, alvAT, alvIn, alvOut});
  int vAudioLatencies = bp.addArray({alDict});

  // ── Root dict ─────────────────────────────────────────────────────────
  int root = bp.addDict(
      {kDeviceID, kMacAddress, kPk, kFeatures, kName, kPi, kVv, kStatusFlags,
       kKeepAliveLowPower, kSourceVersion, kKeepAliveBody, kModel,
       kInitialVolume, kDisplays, kAudioFormats, kAudioLatencies},
      {vDeviceID, vMacAddress, vPk, vFeatures, vName, vPi, vVv, vStatusFlags,
       vKeepAliveLowPower, vSourceVersion, vKeepAliveBody, vModel,
       vInitialVolume, vDisplays, vAudioFormats, vAudioLatencies});

  sendHttpResponse(socket, 200, "application/x-apple-binary-plist",
                   bp.serialize(root));
}

void AirPlayReceiver::handleServerInfo(QTcpSocket *socket) {
  handleGetInfo(socket);
}

void AirPlayReceiver::handlePairSetup(QTcpSocket *socket,
                                      const QByteArray &data) {
  const int sep = data.indexOf("\r\n\r\n");
  const QByteArray body = (sep >= 0) ? data.mid(sep + 4) : QByteArray();
  qInfo("AirPlayReceiver: pair-setup body %lld bytes hex=%s",
        static_cast<long long>(body.size()),
        body.left(48).toHex(' ').constData());
  const QByteArray response = pairing_.handlePairSetup(body);
  qInfo("AirPlayReceiver: pair-setup response %lld bytes hex=%s",
        static_cast<long long>(response.size()),
        response.left(48).toHex(' ').constData());
  sendHttpResponse(socket, 200, "application/octet-stream", response);
}

void AirPlayReceiver::handlePairVerify(QTcpSocket *socket,
                                       const QByteArray &data) {
  const int sep = data.indexOf("\r\n\r\n");
  const QByteArray body = (sep >= 0) ? data.mid(sep + 4) : QByteArray();
  qInfo("AirPlayReceiver: pair-verify body %lld bytes hex=%s",
        static_cast<long long>(body.size()),
        body.left(48).toHex(' ').constData());
  const bool wasVerified = pairing_.isVerifyComplete();
  const QByteArray response = pairing_.handlePairVerify(body);
  qInfo("AirPlayReceiver: pair-verify response %lld bytes hex=%s",
        static_cast<long long>(response.size()),
        response.left(48).toHex(' ').constData());

  // If this was a verify completion attempt and it failed, return 401.
  if (!wasVerified && !pairing_.isVerifyComplete() && !response.isEmpty()) {
    const auto tlvResp = Tlv8::decode(response);
    if (tlvResp.contains(TlvType::Error)) {
      sendHttpResponse(socket, 401, "application/octet-stream", response);
      return;
    }
  }
  if (!wasVerified && response.isEmpty() && !pairing_.isVerifyComplete()) {
    sendHttpResponse(socket, 401, "application/octet-stream", {});
    return;
  }

  sendHttpResponse(socket, 200, "application/octet-stream", response);

  // After a successful verify, enable encryption on this socket.
  if (!wasVerified && pairing_.isVerifyComplete()) {
    enableEncryption(socket);
  }
}

void AirPlayReceiver::handleFeedback(QTcpSocket *socket) {
  // Heartbeat — iOS sends this periodically; acknowledge with 200 OK.
  sendHttpResponse(socket, 200, "text/plain", QByteArray());
}

void AirPlayReceiver::handleFpSetup(QTcpSocket *socket,
                                    const QByteArray &data) {
  // FairPlay SAP (Secure Association Protocol) stub.
  //
  // Stage 1 (16-byte request): iOS sends a challenge; server returns one of
  // four pre-computed 142-byte certificates keyed by req[14] (mode 0-3).
  // These are the same stub table used by UxPlay/RPiPlay — extracted from
  // Apple TV firmware and sufficient for transient pairing without an MFi
  // chip.
  //
  // Stage 2 (164-byte request): iOS sends the session token; server returns
  // a 32-byte response composed of a fixed 12-byte header followed by the
  // last 20 bytes of the request (offset 144).
  //
  // /fp-setup2 is a variant for a different FairPlay version; return 421 so
  // iOS falls back to an acceptable variant.

  // FairPlay version 3 stage-1 reply table (4 × 142 bytes, mode selected by
  // req[14]).  Source: UxPlay lib/fairplay_playfair.c (LGPL-2.1+).
  static const uint8_t kFpReply[4][142] = {
      {0x46, 0x50, 0x4c, 0x59, 0x03, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x82,
       0x02, 0x00, 0x0f, 0x9f, 0x3f, 0x9e, 0x0a, 0x25, 0x21, 0xdb, 0xdf, 0x31,
       0x2a, 0xb2, 0xbf, 0xb2, 0x9e, 0x8d, 0x23, 0x2b, 0x63, 0x76, 0xa8, 0xc8,
       0x18, 0x70, 0x1d, 0x22, 0xae, 0x93, 0xd8, 0x27, 0x37, 0xfe, 0xaf, 0x9d,
       0xb4, 0xfd, 0xf4, 0x1c, 0x2d, 0xba, 0x9d, 0x1f, 0x49, 0xca, 0xaa, 0xbf,
       0x65, 0x91, 0xac, 0x1f, 0x7b, 0xc6, 0xf7, 0xe0, 0x66, 0x3d, 0x21, 0xaf,
       0xe0, 0x15, 0x65, 0x95, 0x3e, 0xab, 0x81, 0xf4, 0x18, 0xce, 0xed, 0x09,
       0x5a, 0xdb, 0x7c, 0x3d, 0x0e, 0x25, 0x49, 0x09, 0xa7, 0x98, 0x31, 0xd4,
       0x9c, 0x39, 0x82, 0x97, 0x34, 0x34, 0xfa, 0xcb, 0x42, 0xc6, 0x3a, 0x1c,
       0xd9, 0x11, 0xa6, 0xfe, 0x94, 0x1a, 0x8a, 0x6d, 0x4a, 0x74, 0x3b, 0x46,
       0xc3, 0xa7, 0x64, 0x9e, 0x44, 0xc7, 0x89, 0x55, 0xe4, 0x9d, 0x81, 0x55,
       0x00, 0x95, 0x49, 0xc4, 0xe2, 0xf7, 0xa3, 0xf6, 0xd5, 0xba},
      {0x46, 0x50, 0x4c, 0x59, 0x03, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x82,
       0x02, 0x01, 0xcf, 0x32, 0xa2, 0x57, 0x14, 0xb2, 0x52, 0x4f, 0x8a, 0xa0,
       0xad, 0x7a, 0xf1, 0x64, 0xe3, 0x7b, 0xcf, 0x44, 0x24, 0xe2, 0x00, 0x04,
       0x7e, 0xfc, 0x0a, 0xd6, 0x7a, 0xfc, 0xd9, 0x5d, 0xed, 0x1c, 0x27, 0x30,
       0xbb, 0x59, 0x1b, 0x96, 0x2e, 0xd6, 0x3a, 0x9c, 0x4d, 0xed, 0x88, 0xba,
       0x8f, 0xc7, 0x8d, 0xe6, 0x4d, 0x91, 0xcc, 0xfd, 0x5c, 0x7b, 0x56, 0xda,
       0x88, 0xe3, 0x1f, 0x5c, 0xce, 0xaf, 0xc7, 0x43, 0x19, 0x95, 0xa0, 0x16,
       0x65, 0xa5, 0x4e, 0x19, 0x39, 0xd2, 0x5b, 0x94, 0xdb, 0x64, 0xb9, 0xe4,
       0x5d, 0x8d, 0x06, 0x3e, 0x1e, 0x6a, 0xf0, 0x7e, 0x96, 0x56, 0x16, 0x2b,
       0x0e, 0xfa, 0x40, 0x42, 0x75, 0xea, 0x5a, 0x44, 0xd9, 0x59, 0x1c, 0x72,
       0x56, 0xb9, 0xfb, 0xe6, 0x51, 0x38, 0x98, 0xb8, 0x02, 0x27, 0x72, 0x19,
       0x88, 0x57, 0x16, 0x50, 0x94, 0x2a, 0xd9, 0x46, 0x68, 0x8a},
      {0x46, 0x50, 0x4c, 0x59, 0x03, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x82,
       0x02, 0x02, 0xc1, 0x69, 0xa3, 0x52, 0xee, 0xed, 0x35, 0xb1, 0x8c, 0xdd,
       0x9c, 0x58, 0xd6, 0x4f, 0x16, 0xc1, 0x51, 0x9a, 0x89, 0xeb, 0x53, 0x17,
       0xbd, 0x0d, 0x43, 0x36, 0xcd, 0x68, 0xf6, 0x38, 0xff, 0x9d, 0x01, 0x6a,
       0x5b, 0x52, 0xb7, 0xfa, 0x92, 0x16, 0xb2, 0xb6, 0x54, 0x82, 0xc7, 0x84,
       0x44, 0x11, 0x81, 0x21, 0xa2, 0xc7, 0xfe, 0xd8, 0x3d, 0xb7, 0x11, 0x9e,
       0x91, 0x82, 0xaa, 0xd7, 0xd1, 0x8c, 0x70, 0x63, 0xe2, 0xa4, 0x57, 0x55,
       0x59, 0x10, 0xaf, 0x9e, 0x0e, 0xfc, 0x76, 0x34, 0x7d, 0x16, 0x40, 0x43,
       0x80, 0x7f, 0x58, 0x1e, 0xe4, 0xfb, 0xe4, 0x2c, 0xa9, 0xde, 0xdc, 0x1b,
       0x5e, 0xb2, 0xa3, 0xaa, 0x3d, 0x2e, 0xcd, 0x59, 0xe7, 0xee, 0xe7, 0x0b,
       0x36, 0x29, 0xf2, 0x2a, 0xfd, 0x16, 0x1d, 0x87, 0x73, 0x53, 0xdd, 0xb9,
       0x9a, 0xdc, 0x8e, 0x07, 0x00, 0x6e, 0x56, 0xf8, 0x50, 0xce},
      {0x46, 0x50, 0x4c, 0x59, 0x03, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x82,
       0x02, 0x03, 0x90, 0x01, 0xe1, 0x72, 0x7e, 0x0f, 0x57, 0xf9, 0xf5, 0x88,
       0x0d, 0xb1, 0x04, 0xa6, 0x25, 0x7a, 0x23, 0xf5, 0xcf, 0xff, 0x1a, 0xbb,
       0xe1, 0xe9, 0x30, 0x45, 0x25, 0x1a, 0xfb, 0x97, 0xeb, 0x9f, 0xc0, 0x01,
       0x1e, 0xbe, 0x0f, 0x3a, 0x81, 0xdf, 0x5b, 0x69, 0x1d, 0x76, 0xac, 0xb2,
       0xf7, 0xa5, 0xc7, 0x08, 0xe3, 0xd3, 0x28, 0xf5, 0x6b, 0xb3, 0x9d, 0xbd,
       0xe5, 0xf2, 0x9c, 0x8a, 0x17, 0xf4, 0x81, 0x48, 0x7e, 0x3a, 0xe8, 0x63,
       0xc6, 0x78, 0x32, 0x54, 0x22, 0xe6, 0xf7, 0x8e, 0x16, 0x6d, 0x18, 0xaa,
       0x7f, 0xd6, 0x36, 0x25, 0x8b, 0xce, 0x28, 0x72, 0x6f, 0x66, 0x1f, 0x73,
       0x88, 0x93, 0xce, 0x44, 0x31, 0x1e, 0x4b, 0xe6, 0xc0, 0x53, 0x51, 0x93,
       0xe5, 0xef, 0x72, 0xe8, 0x68, 0x62, 0x33, 0x72, 0x9c, 0x22, 0x7d, 0x82,
       0x0c, 0x99, 0x94, 0x45, 0xd8, 0x92, 0x46, 0xc8, 0xc3, 0x59}};

  const int sep = data.indexOf("\r\n\r\n");
  const QByteArray body = (sep >= 0) ? data.mid(sep + 4) : QByteArray();

  if (body.size() == 16) {
    // Stage 1: verify FairPlay version byte and dispatch on mode.
    const uint8_t fp_version = static_cast<uint8_t>(body[4]);
    if (fp_version != 0x03) {
      qWarning(
          "AirPlayReceiver: fp-setup stage1 — unsupported FairPlay version "
          "0x%02X (expected 0x03)",
          fp_version);
      sendHttpResponse(socket, 421, "application/octet-stream", {});
      return;
    }
    const int mode = static_cast<uint8_t>(body[14]);
    if (mode > 3) {
      qWarning("AirPlayReceiver: fp-setup stage1 — invalid mode %d", mode);
      sendHttpResponse(socket, 200, "application/octet-stream", {});
      return;
    }
    qDebug(
        "AirPlayReceiver: fp-setup stage1 — mode=%d, returning 142-byte reply",
        mode);
    sendHttpResponse(
        socket, 200, "application/octet-stream",
        QByteArray(reinterpret_cast<const char *>(kFpReply[mode]), 142));
    return;
  }

  if (body.size() == 164) {
    // Stage 2: build 32-byte response = fp_header + req[144..163].
    const uint8_t fp_version = static_cast<uint8_t>(body[4]);
    if (fp_version != 0x03) {
      qWarning(
          "AirPlayReceiver: fp-setup stage2 — unsupported FairPlay version "
          "0x%02X",
          fp_version);
      sendHttpResponse(socket, 421, "application/octet-stream", {});
      return;
    }
    static const uint8_t kFpHeader[12] = {0x46, 0x50, 0x4c, 0x59, 0x03, 0x01,
                                          0x04, 0x00, 0x00, 0x00, 0x00, 0x14};
    QByteArray response(reinterpret_cast<const char *>(kFpHeader), 12);
    response.append(body.mid(144, 20));
    qDebug("AirPlayReceiver: fp-setup stage2 — returning 32-byte response");
    sendHttpResponse(socket, 200, "application/octet-stream", response);
    return;
  }

  // /fp-setup2 (different FairPlay variant) and any unexpected length:
  // return 200 OK so iOS can decide how to proceed.
  qDebug("AirPlayReceiver: fp-setup — body %lld bytes (fallback 200 OK)",
         static_cast<long long>(body.size()));
  sendHttpResponse(socket, 200, "application/octet-stream", {});
}

void AirPlayReceiver::handleOptions(QTcpSocket *socket) {
  QByteArray response = lastProtocol_ + " 200 OK\r\n"
                                        "Server: AirTunes/220.68\r\n";
  if (lastCSeq_ > 0)
    response += "CSeq: " + QByteArray::number(lastCSeq_) + "\r\n";
  response += "Allow: GET, POST, PUT, OPTIONS, SETUP, RECORD, PAUSE, "
              "FLUSH, TEARDOWN, GET_PARAMETER, SET_PARAMETER\r\n"
              "Content-Length: 0\r\n"
              "Connection: keep-alive\r\n"
              "\r\n";
  socket->write(response);
  socket->flush();
}

void AirPlayReceiver::handleSetup(QTcpSocket *socket, const QByteArray &data) {
  activeClient_ = socket;

  const QString request = QString::fromUtf8(data);
  const int uaIdx = request.indexOf(QLatin1String("User-Agent:"));
  if (uaIdx >= 0) {
    const int lineEnd = request.indexOf(QLatin1String("\r\n"), uaIdx);
    clientName_ = request.mid(uaIdx + 12, lineEnd - uaIdx - 12).trimmed();
  }
  if (clientName_.isEmpty())
    clientName_ = socket->peerAddress().toString();

  // Event channel: iOS makes a TCP connection to us on this port.
  // A UDP socket on eventPort breaks iOS — it must be a TCP server.
  if (!eventServer_) {
    eventServer_ = new QTcpServer(this);
    eventServer_->listen(QHostAddress::AnyIPv4, 0);
    eventPort_ = eventServer_->serverPort();
    connect(eventServer_, &QTcpServer::newConnection, this,
            &AirPlayReceiver::onEventClientConnected);
  }
  if (!timingSocket_) {
    timingSocket_ = new QUdpSocket(this);
    timingSocket_->bind(QHostAddress::Any, 0);
    timingPort_ = timingSocket_->localPort();
    connect(timingSocket_, &QUdpSocket::readyRead, this,
            &AirPlayReceiver::onTimingSocketData);
  }
  // Bind data socket for receiving the video/audio stream.
  if (!dataSocket_) {
    dataSocket_ = new QUdpSocket(this);
    dataSocket_->bind(QHostAddress::Any, 0);
    dataPort_ = dataSocket_->localPort();
    connect(dataSocket_, &QUdpSocket::readyRead, this,
            &AirPlayReceiver::onDataSocketData);
  }

  // Parse the SETUP request body (binary plist) for timingProtocol and iOS
  // timing port.
  const int bodyStart = data.indexOf("\r\n\r\n");
  if (bodyStart >= 0) {
    const QByteArray body = data.mid(bodyStart + 4);
    if (!body.isEmpty()) {
      timingProtocol_ =
          parseBplistString(body, QStringLiteral("timingProtocol"));
      clientTimingPort_ = parseBplistPort(body, QStringLiteral("timingPort"));
      qDebug("AirPlayReceiver: SETUP timingProtocol=%s clientTimingPort=%u",
             qPrintable(timingProtocol_), clientTimingPort_);
    }
  }

  // Store iOS client address for NTP requests.
  clientTimingAddr_ = socket->peerAddress();

  // Start NTP timing if needed (NTP mode — we send requests, iOS responds).
  if (timingProtocol_.isEmpty() || timingProtocol_ == QLatin1String("NTP")) {
    if (!ntpTimer_) {
      ntpTimer_ = new QTimer(this);
      ntpTimer_->setInterval(1000);
      connect(ntpTimer_, &QTimer::timeout, this,
              &AirPlayReceiver::sendNtpRequest);
    }
    if (clientTimingPort_ > 0)
      ntpTimer_->start();
  }

  qDebug("AirPlayReceiver: SETUP — eventPort=%u timingPort=%u dataPort=%u",
         eventPort_, timingPort_, dataPort_);

  // Build binary plist response with port assignments.
  // iOS expects a top-level dict with eventPort, timingPort, and a "streams"
  // array containing dicts with dataPort and type for each requested stream.
  BPlistWriter bp;
  int kEvent = bp.addString("eventPort");
  int kTiming = bp.addString("timingPort");
  int kStreams = bp.addString("streams");
  int vEvent = bp.addInt(eventPort_);
  int vTiming = bp.addInt(timingPort_);

  // Stream entry: { dataPort: N, type: 110 }
  // Type 110 = screen mirroring, type 96 = audio.
  int skDataPort = bp.addString("dataPort");
  int skType = bp.addString("type");
  int svDataPort = bp.addInt(dataPort_);
  int svType = bp.addInt(110);
  int streamDict = bp.addDict({skDataPort, skType}, {svDataPort, svType});
  int vStreams = bp.addArray({streamDict});

  int root =
      bp.addDict({kEvent, kTiming, kStreams}, {vEvent, vTiming, vStreams});

  setState(State::Connected);
  emit clientConnected(clientName_);

  sendHttpResponse(socket, 200, "application/x-apple-binary-plist",
                   bp.serialize(root));
}

void AirPlayReceiver::handleRecord(QTcpSocket *socket) {
  qDebug("AirPlayReceiver: RECORD — starting stream");
  setState(State::Mirroring);
  sendHttpResponse(socket, 200, "application/octet-stream", {});
}

void AirPlayReceiver::handleTeardown(QTcpSocket *socket) {
  qDebug("AirPlayReceiver: TEARDOWN — ending session");

  if (eventClient_) {
    eventClient_->close();
    eventClient_->deleteLater();
    eventClient_ = nullptr;
  }
  if (eventServer_) {
    eventServer_->close();
    eventServer_->deleteLater();
    eventServer_ = nullptr;
    eventPort_ = 0;
  }
  if (ntpTimer_) {
    ntpTimer_->stop();
    ntpTimer_->deleteLater();
    ntpTimer_ = nullptr;
  }
  if (timingSocket_) {
    timingSocket_->close();
    timingSocket_->deleteLater();
    timingSocket_ = nullptr;
    timingPort_ = 0;
  }
  if (dataSocket_) {
    dataSocket_->close();
    dataSocket_->deleteLater();
    dataSocket_ = nullptr;
    dataPort_ = 0;
  }

  clientTimingPort_ = 0;
  timingProtocol_.clear();
  teardownVideoToolbox();

  activeClient_ = nullptr;
  clientName_.clear();
  setState(State::Advertising);
  emit clientDisconnected();

  sendHttpResponse(socket, 200, "text/plain", {});
}

void AirPlayReceiver::handleGetParameter(QTcpSocket *socket) {
  sendHttpResponse(socket, 200, "text/plain", {});
}

void AirPlayReceiver::handleSetParameter(QTcpSocket *socket) {
  sendHttpResponse(socket, 200, "text/plain", {});
}

void AirPlayReceiver::handleFlush(QTcpSocket *socket) {
  sendHttpResponse(socket, 200, "text/plain", {});
}

void AirPlayReceiver::handleConfigure(QTcpSocket *socket,
                                      const QByteArray & /*data*/) {
  // Return a valid empty binary-plist dict so iOS can parse the response.
  // An empty body with plist content-type causes a parse error on iOS.
  BPlistWriter bp;
  int root = bp.addDict({}, {});
  sendHttpResponse(socket, 200, "application/x-apple-binary-plist",
                   bp.serialize(root));
}

void AirPlayReceiver::handleAuthSetup(QTcpSocket *socket) {
  sendHttpResponse(socket, 200, "application/octet-stream", {});
}

// ---------------------------------------------------------------------------
// HTTP response helper
// ---------------------------------------------------------------------------

void AirPlayReceiver::sendHttpResponse(QTcpSocket *socket, int statusCode,
                                       const QByteArray &contentType,
                                       const QByteArray &body) {
  QByteArray status;
  switch (statusCode) {
  case 200:
    status = "200 OK";
    break;
  case 401:
    status = "401 Unauthorized";
    break;
  case 404:
    status = "404 Not Found";
    break;
  case 421:
    status = "421 Misdirected Request";
    break;
  default:
    status = QByteArray::number(statusCode) + " Unknown";
    break;
  }

  QByteArray cseqHeader;
  if (lastCSeq_ > 0)
    cseqHeader = "CSeq: " + QByteArray::number(lastCSeq_) + "\r\n";

  // Echo the protocol version from the request (HTTP/1.1 or RTSP/1.0).
  QByteArray resp = lastProtocol_ + " " + status +
                    "\r\n"
                    "Server: AirTunes/220.68\r\n" +
                    cseqHeader + "Content-Type: " + contentType +
                    "\r\n"
                    "Content-Length: " +
                    QByteArray::number(body.size()) +
                    "\r\n"
                    "Connection: keep-alive\r\n"
                    "\r\n" +
                    body;

  // If encryption is active on this socket, wrap the response in an
  // encrypted frame.
  if (encryptionState_.contains(socket) && encryptionState_[socket].active) {
    socket->write(encryptFrame(socket, resp));
  } else {
    socket->write(resp);
  }
  socket->flush();
}

// ---------------------------------------------------------------------------
// Post-pairing encryption (ChaCha20-Poly1305)
// ---------------------------------------------------------------------------

void AirPlayReceiver::enableEncryption(QTcpSocket *socket) {
  const QByteArray key =
      pairing_.deriveKey(QByteArrayLiteral("Pair-Verify-Encrypt-Salt"),
                         QByteArrayLiteral("Pair-Verify-Encrypt-Info"));
  if (key.size() != 32) {
    qWarning("AirPlayReceiver: failed to derive encryption key");
    return;
  }
  EncryptionState es;
  es.key = key;
  es.readNonce = 0;
  es.writeNonce = 0;
  es.active = true;
  encryptionState_[socket] = es;
  qDebug("AirPlayReceiver: encryption enabled on socket %p",
         static_cast<void *>(socket));
}

QByteArray AirPlayReceiver::decryptFrame(QTcpSocket *socket, QByteArray &buf) {
  // Encrypted frame format: 2-byte LE payload length, then (length + 16) bytes
  // of ChaCha20-Poly1305 ciphertext+tag.
  if (buf.size() < 2)
    return {};

  const uint16_t payloadLen =
      static_cast<uint8_t>(buf[0]) | (static_cast<uint8_t>(buf[1]) << 8);
  const int frameLen = 2 + payloadLen + 16; // header + ciphertext + tag
  if (buf.size() < frameLen)
    return {};

  auto &es = encryptionState_[socket];
  const QByteArray aad = buf.left(2); // length bytes are AAD
  const QByteArray ciphertext = buf.mid(2, payloadLen);
  const QByteArray tag = buf.mid(2 + payloadLen, 16);

  // Build 12-byte nonce: 4 zero bytes + 8-byte LE counter.
  uint8_t nonce[12] = {};
  for (int i = 0; i < 8; ++i)
    nonce[4 + i] = static_cast<uint8_t>((es.readNonce >> (i * 8)) & 0xFF);

  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  EVP_DecryptInit_ex(
      ctx, EVP_chacha20_poly1305(), nullptr,
      reinterpret_cast<const unsigned char *>(es.key.constData()), nonce);
  EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, 16,
                      const_cast<char *>(tag.constData()));
  EVP_DecryptUpdate(ctx, nullptr, nullptr,
                    reinterpret_cast<const unsigned char *>(aad.constData()),
                    aad.size());

  QByteArray plaintext(payloadLen, '\0');
  int outLen = 0;
  EVP_DecryptUpdate(
      ctx, reinterpret_cast<unsigned char *>(plaintext.data()), &outLen,
      reinterpret_cast<const unsigned char *>(ciphertext.constData()),
      payloadLen);
  int finalLen = 0;
  if (EVP_DecryptFinal_ex(
          ctx, reinterpret_cast<unsigned char *>(plaintext.data()) + outLen,
          &finalLen) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    qWarning("AirPlayReceiver: decryption failed (nonce=%llu)",
             static_cast<unsigned long long>(es.readNonce));
    return {};
  }
  EVP_CIPHER_CTX_free(ctx);

  es.readNonce++;
  buf.remove(0, frameLen);
  return plaintext;
}

QByteArray AirPlayReceiver::encryptFrame(QTcpSocket *socket,
                                         const QByteArray &plaintext) {
  if (!encryptionState_.contains(socket))
    return plaintext; // fallback: unencrypted
  auto &es = encryptionState_[socket];
  if (!es.active)
    return plaintext;

  const uint16_t payloadLen = static_cast<uint16_t>(plaintext.size());

  // AAD = 2-byte LE length.
  QByteArray aad(2, '\0');
  aad[0] = static_cast<char>(payloadLen & 0xFF);
  aad[1] = static_cast<char>((payloadLen >> 8) & 0xFF);

  // 12-byte nonce.
  uint8_t nonce[12] = {};
  for (int i = 0; i < 8; ++i)
    nonce[4 + i] = static_cast<uint8_t>((es.writeNonce >> (i * 8)) & 0xFF);

  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  EVP_EncryptInit_ex(
      ctx, EVP_chacha20_poly1305(), nullptr,
      reinterpret_cast<const unsigned char *>(es.key.constData()), nonce);
  EVP_EncryptUpdate(ctx, nullptr, nullptr,
                    reinterpret_cast<const unsigned char *>(aad.constData()),
                    aad.size());

  QByteArray ciphertext(plaintext.size(), '\0');
  int outLen = 0;
  EVP_EncryptUpdate(
      ctx, reinterpret_cast<unsigned char *>(ciphertext.data()), &outLen,
      reinterpret_cast<const unsigned char *>(plaintext.constData()),
      plaintext.size());
  int finalLen = 0;
  EVP_EncryptFinal_ex(
      ctx, reinterpret_cast<unsigned char *>(ciphertext.data()) + outLen,
      &finalLen);

  QByteArray tag(16, '\0');
  EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, 16, tag.data());
  EVP_CIPHER_CTX_free(ctx);

  es.writeNonce++;

  // Frame: 2-byte LE length + ciphertext + 16-byte tag.
  return aad + ciphertext + tag;
}

// ---------------------------------------------------------------------------
// State helpers
// ---------------------------------------------------------------------------

void AirPlayReceiver::setState(State s) {
  if (state_ == s)
    return;
  state_ = s;
  emit stateChanged(s);
}

QString AirPlayReceiver::generateDeviceId() const {
  const QByteArray seed =
      QHostInfo::localHostName().toUtf8() + QByteArrayLiteral("_aio");
  uint32_t hash = 0;
  for (char c : seed)
    hash = hash * 31 + static_cast<unsigned char>(c);

  return QStringLiteral("%1:%2:%3:%4:%5:%6")
      .arg((hash >> 0) & 0xFF, 2, 16, QLatin1Char('0'))
      .arg((hash >> 8) & 0xFF, 2, 16, QLatin1Char('0'))
      .arg((hash >> 16) & 0xFF, 2, 16, QLatin1Char('0'))
      .arg((hash >> 24) & 0xFF, 2, 16, QLatin1Char('0'))
      .arg(((hash >> 4) ^ 0xAB) & 0xFF, 2, 16, QLatin1Char('0'))
      .arg(((hash >> 12) ^ 0xCD) & 0xFF, 2, 16, QLatin1Char('0'))
      .toUpper();
}

// ---------------------------------------------------------------------------
// Event channel (TCP)
// ---------------------------------------------------------------------------

void AirPlayReceiver::onEventClientConnected() {
  while (eventServer_ && eventServer_->hasPendingConnections()) {
    QTcpSocket *client = eventServer_->nextPendingConnection();
    if (!client)
      continue;
    if (eventClient_) {
      eventClient_->close();
      eventClient_->deleteLater();
    }
    eventClient_ = client;
    connect(eventClient_, &QTcpSocket::readyRead, this,
            &AirPlayReceiver::onEventClientData);
    qDebug("AirPlayReceiver: event client connected from %s",
           qPrintable(client->peerAddress().toString()));
  }
}

void AirPlayReceiver::onEventClientData() {
  if (!eventClient_)
    return;
  const QByteArray data = eventClient_->readAll();
  qDebug("AirPlayReceiver: event data (%lld bytes)",
         static_cast<long long>(data.size()));
  // Event messages from iOS are informational; ACK with 200 OK if RTSP/HTTP.
  if (data.contains("RTSP/1.0") || data.contains("HTTP/1.1")) {
    const int cseqIdx = data.toLower().indexOf("cseq:");
    int cseq = 0;
    if (cseqIdx >= 0) {
      const int lineEnd = data.indexOf('\n', cseqIdx);
      cseq = data.mid(cseqIdx + 5, lineEnd - cseqIdx - 5).trimmed().toInt();
    }
    const bool isRtsp = data.contains("RTSP/1.0");
    QByteArray resp = (isRtsp ? "RTSP/1.0 200 OK\r\n" : "HTTP/1.1 200 OK\r\n");
    resp += "Server: AirTunes/220.68\r\n";
    if (cseq > 0)
      resp += "CSeq: " + QByteArray::number(cseq) + "\r\n";
    resp += "Content-Length: 0\r\n\r\n";
    eventClient_->write(resp);
    eventClient_->flush();
  }
}

// ---------------------------------------------------------------------------
// NTP timing exchange
// ---------------------------------------------------------------------------

void AirPlayReceiver::sendNtpRequest() {
  if (!timingSocket_ || clientTimingPort_ == 0)
    return;

  // 48-byte NTP v4 client request: LI=0, VN=4, Mode=3 → first byte = 0x23
  QByteArray pkt(48, '\0');
  pkt[0] = static_cast<char>(0x23);

  // Transmit timestamp (bytes 40-47): seconds since Jan 1, 1900 (NTP epoch).
  const int64_t ntpEpochOffset = 2208988800LL;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  const uint64_t ntpSec = static_cast<uint64_t>(ts.tv_sec) + ntpEpochOffset;
  const uint64_t ntpFrac =
      static_cast<uint64_t>(ts.tv_nsec) * (1ULL << 32) / 1000000000ULL;
  auto *p = reinterpret_cast<unsigned char *>(pkt.data());
  p[40] = (ntpSec >> 24) & 0xFF;
  p[41] = (ntpSec >> 16) & 0xFF;
  p[42] = (ntpSec >> 8) & 0xFF;
  p[43] = ntpSec & 0xFF;
  p[44] = (ntpFrac >> 24) & 0xFF;
  p[45] = (ntpFrac >> 16) & 0xFF;
  p[46] = (ntpFrac >> 8) & 0xFF;
  p[47] = ntpFrac & 0xFF;

  timingSocket_->writeDatagram(pkt, clientTimingAddr_, clientTimingPort_);
}

void AirPlayReceiver::onTimingSocketData() {
  if (!timingSocket_)
    return;
  while (timingSocket_->hasPendingDatagrams()) {
    QByteArray buf(static_cast<int>(timingSocket_->pendingDatagramSize()),
                   '\0');
    QHostAddress senderAddr;
    quint16 senderPort = 0;
    timingSocket_->readDatagram(buf.data(), buf.size(), &senderAddr,
                                &senderPort);

    if (buf.size() < 48)
      continue;

    const auto *p = reinterpret_cast<const unsigned char *>(buf.constData());
    const uint8_t mode = p[0] & 0x07;

    if (mode == 3) {
      // iOS sent an NTP CLIENT request → respond as NTP server.
      if (clientTimingPort_ == 0) {
        clientTimingAddr_ = senderAddr;
        clientTimingPort_ = senderPort;
        if (ntpTimer_ && !ntpTimer_->isActive())
          ntpTimer_->start();
      }

      QByteArray resp(48, '\0');
      auto *r = reinterpret_cast<unsigned char *>(resp.data());
      r[0] = 0x24;                             // LI=0, VN=4, Mode=4 (server)
      r[1] = 0x01;                             // stratum=1
      r[2] = 0x02;                             // poll=2
      r[3] = static_cast<unsigned char>(0xE8); // precision≈-24

      // Reference ID "AIRP"
      r[12] = 'A';
      r[13] = 'I';
      r[14] = 'R';
      r[15] = 'P';

      // Origin = Transmit from request (bytes 40-47)
      memcpy(r + 24, p + 40, 8);

      // Receive and Transmit = current time
      const int64_t ntpEpochOfs = 2208988800LL;
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      const uint64_t ntpSec = static_cast<uint64_t>(ts.tv_sec) + ntpEpochOfs;
      const uint64_t ntpFrac =
          static_cast<uint64_t>(ts.tv_nsec) * (1ULL << 32) / 1000000000ULL;
      r[32] = (ntpSec >> 24) & 0xFF;
      r[33] = (ntpSec >> 16) & 0xFF;
      r[34] = (ntpSec >> 8) & 0xFF;
      r[35] = ntpSec & 0xFF;
      r[36] = (ntpFrac >> 24) & 0xFF;
      r[37] = (ntpFrac >> 16) & 0xFF;
      r[38] = (ntpFrac >> 8) & 0xFF;
      r[39] = ntpFrac & 0xFF;
      memcpy(r + 40, r + 32, 8); // transmit = receive

      timingSocket_->writeDatagram(resp, senderAddr, senderPort);
    } else if (mode == 4) {
      qDebug("AirPlayReceiver: NTP response received from %s:%u",
             qPrintable(senderAddr.toString()), senderPort);
    }
  }
}

// ---------------------------------------------------------------------------
// Binary plist key-scanner helpers
// ---------------------------------------------------------------------------

uint16_t AirPlayReceiver::parseBplistPort(const QByteArray &plist,
                                          const QString &key) const {
  const QByteArray needle = key.toUtf8();
  const auto *data = reinterpret_cast<const uint8_t *>(plist.constData());
  const int len = plist.size();

  for (int i = 8; i < len - static_cast<int>(needle.size()) - 3; ++i) {
    if (data[i] == (0x50 | (needle.size() & 0x0F)) &&
        i + 1 + needle.size() <= len &&
        memcmp(data + i + 1, needle.constData(), needle.size()) == 0) {
      int j = i + 1 + static_cast<int>(needle.size());
      while (j < len) {
        if (data[j] == 0x10 && j + 1 < len)
          return data[j + 1];
        if (data[j] == 0x11 && j + 2 < len)
          return (static_cast<uint16_t>(data[j + 1]) << 8) | data[j + 2];
        if (data[j] == 0x12 && j + 4 < len)
          return static_cast<uint16_t>(
              (static_cast<uint32_t>(data[j + 1]) << 24) |
              (static_cast<uint32_t>(data[j + 2]) << 16) |
              (static_cast<uint32_t>(data[j + 3]) << 8) | data[j + 4]);
        j++;
        if (j > i + 1 + static_cast<int>(needle.size()) + 20)
          break;
      }
    }
  }
  return 0;
}

QString AirPlayReceiver::parseBplistString(const QByteArray &plist,
                                           const QString &key) const {
  const QByteArray needle = key.toUtf8();
  const auto *data = reinterpret_cast<const uint8_t *>(plist.constData());
  const int len = plist.size();

  for (int i = 8; i < len - static_cast<int>(needle.size()) - 3; ++i) {
    if (data[i] == (0x50 | (needle.size() & 0x0F)) &&
        i + 1 + needle.size() <= len &&
        memcmp(data + i + 1, needle.constData(), needle.size()) == 0) {
      int j = i + 1 + static_cast<int>(needle.size());
      while (j < len) {
        if ((data[j] & 0xF0) == 0x50) {
          const int sLen = data[j] & 0x0F;
          if (j + 1 + sLen <= len)
            return QString::fromLatin1(
                reinterpret_cast<const char *>(data + j + 1), sLen);
        }
        j++;
        if (j > i + 1 + static_cast<int>(needle.size()) + 30)
          break;
      }
    }
  }
  return {};
}

// ---------------------------------------------------------------------------
// Video stream receive
// ---------------------------------------------------------------------------

void AirPlayReceiver::onDataSocketData() {
  if (!dataSocket_)
    return;
  while (dataSocket_->hasPendingDatagrams()) {
    const qint64 pending = dataSocket_->pendingDatagramSize();
    QByteArray buf(static_cast<int>(pending), '\0');
    dataSocket_->readDatagram(buf.data(), buf.size());

    if (buf.size() < 128)
      continue;

    // 128-byte header, LE fields:
    const auto *h = reinterpret_cast<const uint8_t *>(buf.constData());
    const uint32_t payloadSize =
        h[0] | (h[1] << 8) | (h[2] << 16) | (h[3] << 24);
    const uint16_t payloadType = h[4] | (h[5] << 8);

    // Bytes 8-15: 8-byte NTP timestamp (BE)
    uint64_t ntpTs = 0;
    for (int k = 0; k < 8; ++k)
      ntpTs = (ntpTs << 8) | h[8 + k];

    const QByteArray payload = buf.mid(128, static_cast<int>(payloadSize));

    switch (payloadType) {
    case 0: // video bitstream
      if (!payload.isEmpty())
        processVideoFrame(payload, ntpTs);
      break;
    case 1: // codec data (SPS/PPS in avcC format)
      if (!payload.isEmpty())
        processVideoCodecData(payload);
      break;
    case 2: // heartbeat
      break;
    default:
      qDebug("AirPlayReceiver: unknown stream packet type %u", payloadType);
    }
  }
}

void AirPlayReceiver::processVideoCodecData(const QByteArray &payload) {
  // avcC format (ISO/IEC 14496-15):
  // byte 5: numSPS (lower 5 bits)
  // For each SPS: 2 bytes length (BE) + SPS bytes
  // Then: 1 byte numPPS
  // For each PPS: 2 bytes length (BE) + PPS bytes
  if (payload.size() < 8)
    return;
  const auto *p = reinterpret_cast<const uint8_t *>(payload.constData());
  int offset = 5;
  const int numSps = p[offset++] & 0x1F;
  for (int i = 0; i < numSps && offset + 2 <= payload.size(); ++i) {
    const int spsLen = (p[offset] << 8) | p[offset + 1];
    offset += 2;
    if (offset + spsLen > payload.size())
      break;
    videoSps_ = payload.mid(offset, spsLen);
    offset += spsLen;
  }
  if (offset < payload.size()) {
    const int numPps = p[offset++];
    for (int i = 0; i < numPps && offset + 2 <= payload.size(); ++i) {
      const int ppsLen = (p[offset] << 8) | p[offset + 1];
      offset += 2;
      if (offset + ppsLen > payload.size())
        break;
      videoPps_ = payload.mid(offset, ppsLen);
      offset += ppsLen;
    }
  }
  qDebug("AirPlayReceiver: codec data — SPS=%lld PPS=%lld bytes",
         static_cast<long long>(videoSps_.size()),
         static_cast<long long>(videoPps_.size()));

  if (!videoSps_.isEmpty() && !videoPps_.isEmpty())
    initVideoToolbox();
}

// ---------------------------------------------------------------------------
// VideoToolbox H.264 decode (macOS)
// ---------------------------------------------------------------------------

#ifdef __APPLE__

static void vtDecodeCallback(void *decompressionOutputRefCon,
                             void * /*sourceFrameRefCon*/, OSStatus status,
                             VTDecodeInfoFlags /*infoFlags*/,
                             CVImageBufferRef imageBuffer, CMTime /*pts*/,
                             CMTime /*duration*/) {
  if (status != noErr || !imageBuffer)
    return;
  auto *self = static_cast<AirPlayReceiver *>(decompressionOutputRefCon);
  const QImage frame = self->convertCVImageBufferToQImage(imageBuffer);
  if (!frame.isNull())
    QMetaObject::invokeMethod(
        self, [self, frame]() { emit self->frameReceived(frame); },
        Qt::QueuedConnection);
}

void AirPlayReceiver::initVideoToolbox() {
  teardownVideoToolbox();

  if (videoSps_.isEmpty() || videoPps_.isEmpty())
    return;

  const uint8_t *paramSets[2] = {
      reinterpret_cast<const uint8_t *>(videoSps_.constData()),
      reinterpret_cast<const uint8_t *>(videoPps_.constData())};
  const size_t paramSizes[2] = {static_cast<size_t>(videoSps_.size()),
                                static_cast<size_t>(videoPps_.size())};
  CMVideoFormatDescriptionRef fmtDesc = nullptr;
  OSStatus st = CMVideoFormatDescriptionCreateFromH264ParameterSets(
      kCFAllocatorDefault, 2, paramSets, paramSizes, 4, &fmtDesc);
  if (st != noErr) {
    qWarning("AirPlayReceiver: CMVideoFormatDescription failed: %d", (int)st);
    return;
  }
  vtFormatDesc_ =
      static_cast<void *>(const_cast<opaqueCMFormatDescription *>(fmtDesc));

  VTDecompressionOutputCallbackRecord cb;
  cb.decompressionOutputCallback = vtDecodeCallback;
  cb.decompressionOutputRefCon = this;

  // Build pixel format attributes dict using pure C CoreFoundation API.
  int pixFmt = kCVPixelFormatType_32BGRA;
  CFNumberRef pixFmtVal =
      CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pixFmt);
  const void *keys[] = {kCVPixelBufferPixelFormatTypeKey};
  const void *vals[] = {pixFmtVal};
  CFDictionaryRef destAttr = CFDictionaryCreate(
      kCFAllocatorDefault, keys, vals, 1, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);

  VTDecompressionSessionRef session = nullptr;
  st = VTDecompressionSessionCreate(kCFAllocatorDefault, fmtDesc, nullptr,
                                    destAttr, &cb, &session);
  CFRelease(destAttr);
  CFRelease(pixFmtVal);
  if (st != noErr) {
    qWarning("AirPlayReceiver: VTDecompressionSession failed: %d", (int)st);
    CFRelease(fmtDesc);
    vtFormatDesc_ = nullptr;
    return;
  }
  vtDecompSession_ = session;
  qDebug("AirPlayReceiver: VideoToolbox session initialized");
}

void AirPlayReceiver::teardownVideoToolbox() {
  if (vtDecompSession_) {
    VTDecompressionSessionInvalidate(
        static_cast<VTDecompressionSessionRef>(vtDecompSession_));
    CFRelease(vtDecompSession_);
    vtDecompSession_ = nullptr;
  }
  if (vtFormatDesc_) {
    CFRelease(vtFormatDesc_);
    vtFormatDesc_ = nullptr;
  }
  videoSps_.clear();
  videoPps_.clear();
}

QImage AirPlayReceiver::convertCVImageBufferToQImage(void *cvBuf) {
  CVImageBufferRef buf = static_cast<CVImageBufferRef>(cvBuf);
  CVPixelBufferLockBaseAddress(buf, kCVPixelBufferLock_ReadOnly);
  const int w = static_cast<int>(CVPixelBufferGetWidth(buf));
  const int h = static_cast<int>(CVPixelBufferGetHeight(buf));
  void *base = CVPixelBufferGetBaseAddress(buf);
  const size_t stride = CVPixelBufferGetBytesPerRow(buf);
  // kCVPixelFormatType_32BGRA → QImage::Format_ARGB32
  QImage img(static_cast<const uchar *>(base), w, h, static_cast<int>(stride),
             QImage::Format_ARGB32);
  QImage result = img.copy(); // deep copy before unlock
  CVPixelBufferUnlockBaseAddress(buf, kCVPixelBufferLock_ReadOnly);
  return result;
}

void AirPlayReceiver::processVideoFrame(const QByteArray &payload,
                                        uint64_t /*ntpTimestamp*/) {
  if (!vtDecompSession_ || !vtFormatDesc_)
    return;

  CMBlockBufferRef blockBuf = nullptr;
  OSStatus st = CMBlockBufferCreateWithMemoryBlock(
      kCFAllocatorDefault, const_cast<char *>(payload.constData()),
      static_cast<size_t>(payload.size()), kCFAllocatorNull, nullptr, 0,
      payload.size(), 0, &blockBuf);
  if (st != noErr)
    return;

  CMSampleBufferRef sampleBuf = nullptr;
  const size_t sampleSizes[] = {static_cast<size_t>(payload.size())};
  st = CMSampleBufferCreateReady(
      kCFAllocatorDefault, blockBuf,
      static_cast<CMVideoFormatDescriptionRef>(vtFormatDesc_), 1, 0, nullptr, 1,
      sampleSizes, &sampleBuf);
  CFRelease(blockBuf);
  if (st != noErr)
    return;

  VTDecompressionSessionDecodeFrame(
      static_cast<VTDecompressionSessionRef>(vtDecompSession_), sampleBuf, 0,
      nullptr, nullptr);
  CFRelease(sampleBuf);
}

#else
// Non-Apple stubs
void AirPlayReceiver::initVideoToolbox() {}
void AirPlayReceiver::teardownVideoToolbox() {}
QImage AirPlayReceiver::convertCVImageBufferToQImage(void *) { return {}; }
void AirPlayReceiver::processVideoFrame(const QByteArray &, uint64_t) {}
#endif

} // namespace AIO::ScreenMirror
