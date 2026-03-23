#include "screenmirror/MirrorSessionManager.h"
#include "screenmirror/AirPlayReceiver.h"

#include <QNetworkInterface>

namespace AIO::ScreenMirror {

MirrorSessionManager::MirrorSessionManager(QObject *parent)
    : QObject(parent), airplay_(std::make_unique<AirPlayReceiver>(this)) {

  connect(airplay_.get(), &AirPlayReceiver::stateChanged, this,
          [this](AirPlayReceiver::State s) {
            switch (s) {
            case AirPlayReceiver::State::Stopped:
              setSessionState(SessionState::Idle);
              break;
            case AirPlayReceiver::State::Advertising:
              setSessionState(SessionState::Waiting);
              break;
            case AirPlayReceiver::State::Connected:
              setSessionState(SessionState::Connecting);
              break;
            case AirPlayReceiver::State::Mirroring:
              setSessionState(SessionState::Mirroring);
              break;
            }
          });

  connect(airplay_.get(), &AirPlayReceiver::clientConnected, this,
          [this](const QString &name) {
            clientDeviceName_ = name;
            emit clientDeviceNameChanged(name);
          });

  connect(airplay_.get(), &AirPlayReceiver::clientDisconnected, this, [this]() {
    clientDeviceName_.clear();
    emit clientDeviceNameChanged(QString());
  });

  connect(airplay_.get(), &AirPlayReceiver::frameReceived, this,
          &MirrorSessionManager::frameReceived);

  connect(airplay_.get(), &AirPlayReceiver::errorOccurred, this,
          [this](const QString &msg) {
            lastError_ = msg;
            setSessionState(SessionState::Error);
            emit errorOccurred(msg);
          });
}

MirrorSessionManager::~MirrorSessionManager() { stopReceiving(); }

bool MirrorSessionManager::startReceiving(uint16_t port) {
  if (sessionState_ != SessionState::Idle &&
      sessionState_ != SessionState::Error)
    return true;

  lastError_.clear();
  return airplay_->start(port);
}

void MirrorSessionManager::stopReceiving() { airplay_->stop(); }

QString MirrorSessionManager::deviceName() const {
  return airplay_->deviceName();
}

void MirrorSessionManager::setDeviceName(const QString &name) {
  airplay_->setDeviceName(name);
}

QString MirrorSessionManager::localIpAddress() const {
  const auto interfaces = QNetworkInterface::allInterfaces();
  for (const auto &iface : interfaces) {
    if (iface.flags().testFlag(QNetworkInterface::IsLoopBack))
      continue;
    if (!iface.flags().testFlag(QNetworkInterface::IsUp))
      continue;
    const auto entries = iface.addressEntries();
    for (const auto &entry : entries) {
      if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
        return entry.ip().toString();
    }
  }
  return QStringLiteral("127.0.0.1");
}

uint16_t MirrorSessionManager::serverPort() const { return airplay_->port(); }

void MirrorSessionManager::setSessionState(SessionState s) {
  if (sessionState_ == s)
    return;
  sessionState_ = s;
  emit sessionStateChanged(s);
}

} // namespace AIO::ScreenMirror
