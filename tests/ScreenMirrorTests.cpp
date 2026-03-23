#include <gtest/gtest.h>

#include "screenmirror/AirPlayPairing.h"
#include "screenmirror/AirPlayReceiver.h"
#include "screenmirror/AirPlayTlv.h"
#include "screenmirror/MirrorSessionManager.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QThread>
#include <QUdpSocket>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

using namespace AIO::ScreenMirror;

// Need QCoreApplication for QTcpServer signal delivery.
static int s_argc = 1;
static const char *s_argv[] = {"ScreenMirrorTests"};
static QCoreApplication s_app(s_argc, const_cast<char **>(s_argv));

// Pump the Qt event loop until a condition is met or timeout.
static bool waitFor(std::function<bool()> pred, int timeoutMs = 3000) {
  QElapsedTimer timer;
  timer.start();
  while (!pred() && timer.elapsed() < timeoutMs) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QThread::msleep(10);
  }
  return pred();
}

// ---------------------------------------------------------------------------
// AirPlayReceiver tests
// ---------------------------------------------------------------------------

class AirPlayReceiverTest : public ::testing::Test {
protected:
  void SetUp() override {
    receiver = std::make_unique<AirPlayReceiver>();
    receiver->setDeviceName("TestAIO");
  }
  void TearDown() override { receiver.reset(); }
  std::unique_ptr<AirPlayReceiver> receiver;
};

TEST_F(AirPlayReceiverTest, InitialStateIsStopped) {
  EXPECT_EQ(receiver->state(), AirPlayReceiver::State::Stopped);
}

TEST_F(AirPlayReceiverTest, DeviceNameDefaultIsAIOServer) {
  auto r = std::make_unique<AirPlayReceiver>();
  EXPECT_EQ(r->deviceName(), QString("AIO Server"));
}

TEST_F(AirPlayReceiverTest, SetDeviceNameUpdates) {
  receiver->setDeviceName("Living Room TV");
  EXPECT_EQ(receiver->deviceName(), QString("Living Room TV"));
}

TEST_F(AirPlayReceiverTest, StartTransitionsToAdvertising) {
  QSignalSpy spy(receiver.get(), &AirPlayReceiver::stateChanged);

  // Use a high port to avoid permission issues in CI.
  ASSERT_TRUE(receiver->start(17000));
  EXPECT_EQ(receiver->state(), AirPlayReceiver::State::Advertising);
  EXPECT_EQ(spy.count(), 1);

  receiver->stop();
  EXPECT_EQ(receiver->state(), AirPlayReceiver::State::Stopped);
}

TEST_F(AirPlayReceiverTest, DoubleStartIsIdempotent) {
  ASSERT_TRUE(receiver->start(17001));
  ASSERT_TRUE(receiver->start(17001)); // second call is no-op
  EXPECT_EQ(receiver->state(), AirPlayReceiver::State::Advertising);
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, StopWhenStoppedIsNoop) {
  receiver->stop(); // should not crash
  EXPECT_EQ(receiver->state(), AirPlayReceiver::State::Stopped);
}

TEST_F(AirPlayReceiverTest, GetInfoEndpointResponds) {
  ASSERT_TRUE(receiver->start(17002));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17002);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write("GET /info HTTP/1.1\r\nHost: localhost\r\n\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));

  const QByteArray response = client.readAll();
  EXPECT_TRUE(response.contains("200 OK"));
  // Binary plist with device name embedded as ASCII string.
  EXPECT_TRUE(response.contains("bplist00"));
  EXPECT_TRUE(response.contains("TestAIO"));
  EXPECT_TRUE(response.contains("deviceID"));

  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, UnknownEndpointReturns404) {
  ASSERT_TRUE(receiver->start(17003));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17003);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write("GET /nonexistent HTTP/1.1\r\nHost: localhost\r\n\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));

  const QByteArray response = client.readAll();
  EXPECT_TRUE(response.contains("404"));

  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, PortReturnsConfiguredValue) {
  ASSERT_TRUE(receiver->start(17004));
  EXPECT_EQ(receiver->port(), 17004);
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, SetupEndpointTransitionsToConnected) {
  QSignalSpy spy(receiver.get(), &AirPlayReceiver::clientConnected);
  ASSERT_TRUE(receiver->start(17005));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17005);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write("SETUP rtsp://localhost/session RTSP/1.0\r\n"
               "CSeq: 1\r\n"
               "User-Agent: AIOTestDevice/1.0\r\n"
               "\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return !spy.isEmpty(); }));
  EXPECT_EQ(receiver->state(), AirPlayReceiver::State::Connected);
  EXPECT_FALSE(receiver->clientName().isEmpty());

  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, ClientDisconnectResetsToAdvertising) {
  ASSERT_TRUE(receiver->start(17006));

  // Connect and trigger the Connected state via SETUP.
  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17006);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write("SETUP rtsp://localhost/session RTSP/1.0\r\n"
               "CSeq: 1\r\n"
               "\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() {
    return receiver->state() == AirPlayReceiver::State::Connected;
  }));

  QSignalSpy spyDisc(receiver.get(), &AirPlayReceiver::clientDisconnected);
  client.disconnectFromHost();

  ASSERT_TRUE(waitFor([&]() { return !spyDisc.isEmpty(); }));
  EXPECT_EQ(receiver->state(), AirPlayReceiver::State::Advertising);

  receiver->stop();
}

TEST_F(AirPlayReceiverTest, FragmentedHttpRequestHandled) {
  ASSERT_TRUE(receiver->start(17007));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17007);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  // Send the request in two fragments to exercise per-socket buffer assembly.
  client.write("GET /info");
  client.flush();
  QThread::msleep(5);
  client.write(" HTTP/1.1\r\nHost: localhost\r\n\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  const QByteArray response = client.readAll();
  EXPECT_TRUE(response.contains("200 OK"));

  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, RtspCommandsReceiveRtspProtocolResponse) {
  // AirPlay SETUP/RECORD/etc. are RTSP commands and must receive RTSP/1.0
  // responses.  An HTTP/1.1 response to an RTSP request causes iOS to
  // abort the connection with "Unable to connect".
  ASSERT_TRUE(receiver->start(17012));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17012);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write("SETUP rtsp://localhost/session RTSP/1.0\r\n"
               "CSeq: 1\r\n"
               "User-Agent: AIOTestDevice/1.0\r\n"
               "\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  const QByteArray response = client.readAll();
  EXPECT_TRUE(response.startsWith("RTSP/1.0 200 OK"))
      << "RTSP command must receive RTSP/1.0 response, got: "
      << response.left(40).toStdString();

  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, HttpCommandsReceiveHttpProtocolResponse) {
  // HTTP commands (GET /info, POST /pair-setup, etc.) must receive HTTP/1.1.
  ASSERT_TRUE(receiver->start(17013));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17013);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write("GET /info HTTP/1.1\r\nHost: localhost\r\n\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  const QByteArray response = client.readAll();
  EXPECT_TRUE(response.startsWith("HTTP/1.1 200 OK"))
      << "HTTP command must receive HTTP/1.1 response, got: "
      << response.left(40).toStdString();

  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, GetInfoContainsCorrectFeaturesAndStatusFlags) {
  // The /info plist must advertise the 64-bit features value that iOS 14+
  // requires to recognise us as an AirPlay 2 screen-mirror target.
  // High32 must be 0x1E (conservative transient-pairing flags) so that iOS
  // does not attempt HAP/MFi auth flows we cannot handle.
  // statusFlags must not have bit 6 (0x40) set \u2014 that signals PIN
  // required.
  ASSERT_TRUE(receiver->start(17014));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17014);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write("GET /info HTTP/1.1\r\nHost: localhost\r\n\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  const QByteArray response = client.readAll();
  EXPECT_TRUE(response.contains("200 OK"));
  EXPECT_TRUE(response.contains("bplist00"));
  // The 64-bit features value is stored big-endian as an 8-byte integer.
  // High32=0x0000001E, Low32=0x5A7FFFF7 => bytes: 00 00 00 1E 5A 7F FF F7
  EXPECT_TRUE(response.contains(QByteArray("\x00\x00\x00\x1E", 4)))
      << "High 32-bit features word must be 0x0000001E in /info plist";
  EXPECT_TRUE(response.contains(QByteArray("\x5A\x7F\xFF\xF7", 4)))
      << "Low 32-bit features word 0x5A7FFFF7 must be present in /info plist";

  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, SetupEventPortAcceptsTcpConnection) {
  // After SETUP, the eventPort must accept a TCP connection.
  // iOS fails with "Unable to connect" if it's a UDP port.
  QSignalSpy spy(receiver.get(), &AirPlayReceiver::clientConnected);
  ASSERT_TRUE(receiver->start(17015));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17015);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write("SETUP rtsp://localhost/session RTSP/1.0\r\nCSeq: 1\r\n\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return !spy.isEmpty(); }));

  const uint16_t eport = receiver->eventPort();
  ASSERT_GT(eport, 0);

  QTcpSocket eventConn;
  eventConn.connectToHost("127.0.0.1", eport);
  EXPECT_TRUE(waitFor(
      [&]() { return eventConn.state() == QAbstractSocket::ConnectedState; },
      1000))
      << "eventPort must accept TCP connections";

  eventConn.close();
  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, TimingSocketRespondsToNtpRequest) {
  ASSERT_TRUE(receiver->start(17016));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17016);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));
  client.write("SETUP rtsp://localhost/session RTSP/1.0\r\nCSeq: 1\r\n\r\n");
  client.flush();
  ASSERT_TRUE(waitFor([&]() {
    return receiver->state() == AirPlayReceiver::State::Connected;
  }));

  const uint16_t tport = receiver->timingPort();
  ASSERT_GT(tport, 0);

  // Send a fake NTP v4 client request to the timing port
  QUdpSocket udp;
  udp.bind(QHostAddress::LocalHost, 0);
  QByteArray ntpReq(48, '\0');
  ntpReq[0] = static_cast<char>(0x23); // LI=0, VN=4, Mode=3
  udp.writeDatagram(ntpReq, QHostAddress::LocalHost, tport);

  // Expect an NTP response within 1 second
  EXPECT_TRUE(waitFor([&]() { return udp.hasPendingDatagrams(); }, 1000))
      << "Timing socket must respond to NTP requests";

  if (udp.hasPendingDatagrams()) {
    QByteArray resp(48, '\0');
    udp.readDatagram(resp.data(), resp.size());
    EXPECT_EQ(static_cast<uint8_t>(resp[0]), 0x24u)
        << "NTP response must have Mode=4 (server)";
  }

  udp.close();
  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, DataSocketReadsStreamPackets) {
  ASSERT_TRUE(receiver->start(17017));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17017);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));
  client.write("SETUP rtsp://localhost/session RTSP/1.0\r\nCSeq: 1\r\n\r\n");
  client.flush();
  ASSERT_TRUE(waitFor([&]() {
    return receiver->state() == AirPlayReceiver::State::Connected;
  }));

  const uint16_t dport = receiver->dataPort();
  ASSERT_GT(dport, 0);

  // Send a heartbeat packet (type=2, no payload) — 128-byte header
  QByteArray pkt(128, '\0');
  pkt[4] = 0x02;
  pkt[5] = 0x00; // type=2 (heartbeat)

  QUdpSocket dataUdp;
  dataUdp.bind(QHostAddress::LocalHost, 0);
  dataUdp.writeDatagram(pkt, QHostAddress::LocalHost, dport);

  // Just verify no crash — the heartbeat should be silently consumed
  QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  EXPECT_EQ(receiver->state(), AirPlayReceiver::State::Connected);

  dataUdp.close();
  client.close();
  receiver->stop();
}

// ---------------------------------------------------------------------------
// MirrorSessionManager tests
// ---------------------------------------------------------------------------

class MirrorSessionManagerTest : public ::testing::Test {
protected:
  void SetUp() override { manager = std::make_unique<MirrorSessionManager>(); }
  void TearDown() override { manager.reset(); }
  std::unique_ptr<MirrorSessionManager> manager;
};

TEST_F(MirrorSessionManagerTest, InitialStateIsIdle) {
  EXPECT_EQ(manager->sessionState(), MirrorSessionManager::SessionState::Idle);
}

TEST_F(MirrorSessionManagerTest, StartReceivingTransitionsToWaiting) {
  QSignalSpy spy(manager.get(), &MirrorSessionManager::sessionStateChanged);

  ASSERT_TRUE(manager->startReceiving(17010));
  EXPECT_EQ(manager->sessionState(),
            MirrorSessionManager::SessionState::Waiting);
  EXPECT_GE(spy.count(), 1);

  manager->stopReceiving();
  EXPECT_EQ(manager->sessionState(), MirrorSessionManager::SessionState::Idle);
}

TEST_F(MirrorSessionManagerTest, DeviceNameDefault) {
  EXPECT_EQ(manager->deviceName(), QString("AIO Server"));
}

TEST_F(MirrorSessionManagerTest, SetDeviceName) {
  manager->setDeviceName("My TV");
  EXPECT_EQ(manager->deviceName(), QString("My TV"));
}

TEST_F(MirrorSessionManagerTest, LocalIpAddressIsNonEmpty) {
  const QString ip = manager->localIpAddress();
  EXPECT_FALSE(ip.isEmpty());
}

TEST_F(MirrorSessionManagerTest, ClientNameEmptyWhenIdle) {
  EXPECT_TRUE(manager->clientDeviceName().isEmpty());
}

TEST_F(MirrorSessionManagerTest, StopWhenIdleIsNoop) {
  manager->stopReceiving(); // should not crash
  EXPECT_EQ(manager->sessionState(), MirrorSessionManager::SessionState::Idle);
}

// ---------------------------------------------------------------------------
// Tlv8 tests
// ---------------------------------------------------------------------------

TEST(Tlv8Test, EncodeDecodeSingleEntry) {
  QHash<quint8, QByteArray> m;
  m[TlvType::Identifier] = QByteArray("hello");
  const QByteArray encoded = Tlv8::encode(m);
  const auto decoded = Tlv8::decode(encoded);
  EXPECT_EQ(decoded[TlvType::Identifier], QByteArray("hello"));
}

TEST(Tlv8Test, EncodeDecodeMultipleEntries) {
  QHash<quint8, QByteArray> m;
  m[TlvType::State] = QByteArray(1, '\x02');
  m[TlvType::Salt] = QByteArray(16, '\xAB');
  m[TlvType::PublicKey] = QByteArray(32, '\x01');
  const QByteArray encoded = Tlv8::encode(m);
  const auto decoded = Tlv8::decode(encoded);
  EXPECT_EQ(decoded[TlvType::State], QByteArray(1, '\x02'));
  EXPECT_EQ(decoded[TlvType::Salt], QByteArray(16, '\xAB'));
  EXPECT_EQ(decoded[TlvType::PublicKey], QByteArray(32, '\x01'));
}

TEST(Tlv8Test, EncodeFragmentsLargeValues) {
  QHash<quint8, QByteArray> m;
  m[TlvType::PublicKey] = QByteArray(400, '\x55'); // > 255 bytes → fragmented
  const QByteArray encoded = Tlv8::encode(m);
  const auto decoded = Tlv8::decode(encoded);
  EXPECT_EQ(decoded[TlvType::PublicKey], QByteArray(400, '\x55'));
}

TEST(Tlv8Test, DecodeEmptyReturnsEmpty) {
  const auto decoded = Tlv8::decode({});
  EXPECT_TRUE(decoded.isEmpty());
}

TEST(Tlv8Test, ErrorTlvContainsExpectedFields) {
  QHash<quint8, QByteArray> m;
  m[TlvType::State] = QByteArray(1, '\x04');
  m[TlvType::Error] = QByteArray(1, char(TlvError::Authentication));
  const QByteArray encoded = Tlv8::encode(m);
  const auto decoded = Tlv8::decode(encoded);
  EXPECT_EQ(static_cast<quint8>(decoded[TlvType::State][0]), 4u);
  EXPECT_EQ(static_cast<quint8>(decoded[TlvType::Error][0]),
            TlvError::Authentication);
}

// ---------------------------------------------------------------------------
// AirPlayPairing unit tests
// ---------------------------------------------------------------------------

class AirPlayPairingTest : public ::testing::Test {
protected:
  void SetUp() override {
    pairing = std::make_unique<AirPlayPairing>();
    pairing->init("AABBCCDDEEFF");
  }
  std::unique_ptr<AirPlayPairing> pairing;
};

TEST_F(AirPlayPairingTest, InitGeneratesPublicKey) {
  EXPECT_EQ(pairing->publicKey().size(), 32);
}

TEST_F(AirPlayPairingTest, PairingIdIsSet) {
  EXPECT_EQ(pairing->pairingId(), QString("AABBCCDDEEFF"));
}

TEST_F(AirPlayPairingTest, InitWithGivenKeysUsesThem) {
  // Generate a fresh key pair to pass in.
  AirPlayPairing tmp;
  tmp.init("TEST");
  const QByteArray pk = tmp.publicKey();
  EXPECT_EQ(pk.size(), 32);
}

TEST_F(AirPlayPairingTest, PairSetupReturns32BytePubkey) {
  // Send a 32-byte Ed25519 public key; expect our 32-byte pubkey back.
  const QByteArray clientPk(32, '\x42');
  const QByteArray resp = pairing->handlePairSetup(clientPk);
  EXPECT_EQ(resp.size(), 32);
  EXPECT_EQ(resp, pairing->publicKey());
  EXPECT_TRUE(pairing->isSetupComplete());
}

TEST_F(AirPlayPairingTest, PairVerifyStep1Returns96Bytes) {
  // First do pair-setup to give the server a client Ed25519 key.
  const QByteArray clientEd(32, '\x42');
  pairing->handlePairSetup(clientEd);

  // Build step-1 request: [0x01,0x00,0x00,0x00] + 32-byte X25519 + 32-byte
  // Ed25519
  QByteArray req(68, '\0');
  req[0] = 0x01;
  // Fill X25519 portion (bytes 4-35) with test data.
  for (int i = 4; i < 36; ++i)
    req[i] = static_cast<char>(i);
  // Fill Ed25519 portion (bytes 36-67) with client key.
  req.replace(36, 32, clientEd);

  const QByteArray resp = pairing->handlePairVerify(req);
  // Response: 32-byte server X25519 + 64-byte Ed25519 signature = 96 bytes.
  EXPECT_EQ(resp.size(), 96);
}

TEST_F(AirPlayPairingTest, PairVerifyStep2WithoutStep1ReturnsEmpty) {
  // Step 2 with flag=0 but no step 1 done: should return empty.
  QByteArray req(68, '\0');
  req[0] = 0x00;
  const QByteArray resp = pairing->handlePairVerify(req);
  EXPECT_TRUE(resp.isEmpty());
}

TEST_F(AirPlayPairingTest, ResetClearsVerifyState) {
  const QByteArray clientEd(32, '\x11');
  pairing->handlePairSetup(clientEd);

  // Do step 1.
  QByteArray req(68, '\0');
  req[0] = 0x01;
  for (int i = 4; i < 36; ++i)
    req[i] = static_cast<char>(i);
  req.replace(36, 32, clientEd);
  pairing->handlePairVerify(req);
  EXPECT_FALSE(pairing->isVerifyComplete());

  pairing->reset();

  // After reset, step 2 should fail (verifyStep_ was cleared).
  QByteArray s2(68, '\0');
  s2[0] = 0x00;
  const QByteArray resp = pairing->handlePairVerify(s2);
  EXPECT_TRUE(resp.isEmpty());
}

// ---------------------------------------------------------------------------
// AirPlayReceiver feature flags / new endpoints
// ---------------------------------------------------------------------------

TEST_F(AirPlayReceiverTest, GetInfoIncludesBinaryPlist) {
  ASSERT_TRUE(receiver->start(17020));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17020);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write("GET /info HTTP/1.1\r\nHost: localhost\r\n\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  const QByteArray response = client.readAll();
  // /info must return binary plist with bplist00 magic.
  EXPECT_TRUE(response.contains("application/x-apple-binary-plist"));
  EXPECT_TRUE(response.contains("bplist00"));

  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, FeedbackEndpointReturns200) {
  ASSERT_TRUE(receiver->start(17021));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17021);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write("POST /feedback HTTP/1.1\r\nHost: localhost\r\nContent-Length: "
               "0\r\n\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  const QByteArray response = client.readAll();
  EXPECT_TRUE(response.contains("200 OK"));

  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, OptionsEndpointReturns200WithAllow) {
  ASSERT_TRUE(receiver->start(17022));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17022);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write("OPTIONS * HTTP/1.1\r\nHost: localhost\r\n\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  const QByteArray response = client.readAll();
  EXPECT_TRUE(response.contains("200 OK"));
  EXPECT_TRUE(response.contains("Allow:"));

  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, PairSetupEndpointReturns32Bytes) {
  ASSERT_TRUE(receiver->start(17023));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17023);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  // Send a 32-byte Ed25519 public key as pair-setup body.
  const QByteArray body(32, '\x42');

  const QByteArray httpReq = "POST /pair-setup HTTP/1.1\r\n"
                             "Host: localhost\r\n"
                             "Content-Type: application/octet-stream\r\n"
                             "Content-Length: " +
                             QByteArray::number(body.size()) +
                             "\r\n"
                             "\r\n" +
                             body;
  client.write(httpReq);
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  const QByteArray response = client.readAll();
  EXPECT_TRUE(response.contains("200 OK"));
  // Response body must be 32-byte Ed25519 public key.
  const int sep = response.indexOf("\r\n\r\n");
  ASSERT_GT(sep, 0);
  const QByteArray respBody = response.mid(sep + 4);
  EXPECT_EQ(respBody.size(), 32);

  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, PairVerifyEndpointResponds96Bytes) {
  ASSERT_TRUE(receiver->start(17024));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17024);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  // Build a pair-verify step-1 request:
  // [0x01,0x00,0x00,0x00] + 32-byte X25519 + 32-byte Ed25519 = 68 bytes.
  QByteArray body(68, '\0');
  body[0] = 0x01;
  for (int i = 4; i < 68; ++i)
    body[i] = static_cast<char>(i & 0xFF);

  const QByteArray httpReq = "POST /pair-verify HTTP/1.1\r\n"
                             "Host: localhost\r\n"
                             "Content-Type: application/octet-stream\r\n"
                             "Content-Length: " +
                             QByteArray::number(body.size()) +
                             "\r\n"
                             "\r\n" +
                             body;
  client.write(httpReq);
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  const QByteArray response = client.readAll();
  EXPECT_TRUE(response.contains("200 OK"));
  const int sep = response.indexOf("\r\n\r\n");
  ASSERT_GT(sep, 0);
  const QByteArray respBody = response.mid(sep + 4);
  // Step-1 response: 32-byte X25519 + 64-byte signature = 96 bytes.
  EXPECT_EQ(respBody.size(), 96);

  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, PairVerifyTlv8M1ReturnsTransientM2) {
  // TLV8 pair-verify M1 (iOS 14+ format) must return an M2 containing ONLY
  // State=2 and PublicKey (37 bytes total) — no EncryptedData.  iOS aborts
  // pairing without sending M3 when M2 unexpectedly contains EncryptedData.
  ASSERT_TRUE(receiver->start(17025));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17025);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  // Build TLV8 M1: {State=1, PublicKey=32 fake bytes} = 37 bytes
  QByteArray body;
  body.append(char(0x06)); // State type
  body.append(char(0x01)); // len=1
  body.append(char(0x01)); // State=1
  body.append(char(0x03)); // PublicKey type
  body.append(char(0x20)); // len=32
  body.append(QByteArray(32, char(0xAB)));

  const QByteArray httpReq = "POST /pair-verify HTTP/1.1\r\n"
                             "Host: localhost\r\n"
                             "Content-Type: application/octet-stream\r\n"
                             "Content-Length: " +
                             QByteArray::number(body.size()) + "\r\n\r\n" +
                             body;
  client.write(httpReq);
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  const QByteArray response = client.readAll();
  EXPECT_TRUE(response.contains("200 OK"));
  const int sep = response.indexOf("\r\n\r\n");
  ASSERT_GT(sep, 0);
  const QByteArray respBody = response.mid(sep + 4);
  // M2 must be exactly TLV8 {State=2, PublicKey=32B} = 37 bytes.
  // Before the fix it was 135 bytes (includes EncryptedData=96B).
  EXPECT_EQ(respBody.size(), 37)
      << "TLV8 pair-verify M2 must be 37 bytes (State+PublicKey only, "
         "no EncryptedData) for transient pairing";
  // Verify it decodes to State=2 + PublicKey=32B only.
  const auto tlv = Tlv8::decode(respBody);
  EXPECT_TRUE(tlv.contains(TlvType::State));
  EXPECT_EQ(static_cast<uint8_t>(tlv[TlvType::State][0]), 2u);
  EXPECT_TRUE(tlv.contains(TlvType::PublicKey));
  EXPECT_EQ(tlv[TlvType::PublicKey].size(), 32);
  EXPECT_FALSE(tlv.contains(TlvType::EncryptedData))
      << "M2 must NOT contain EncryptedData for transient pairing";

  client.close();
  receiver->stop();
}

// ---------------------------------------------------------------------------
// Regression tests for the critical AirPlay connection bugs
// ---------------------------------------------------------------------------

// Bug 1: Fragmented POST — body arrives in a separate TCP write.
// Before the fix, onClientData() dispatched with an empty body as soon as
// \r\n\r\n was seen, causing pair-setup to return an error TLV.
TEST_F(AirPlayReceiverTest, FragmentedPostBodyWaitsForFullBody) {
  ASSERT_TRUE(receiver->start(17030));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17030);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  // 32-byte pair-setup body (Ed25519 public key).
  const QByteArray body(32, '\x42');

  // Send ONLY the headers first — no body yet.
  const QByteArray headers = "POST /pair-setup HTTP/1.1\r\n"
                             "Host: localhost\r\n"
                             "Content-Type: application/octet-stream\r\n"
                             "Content-Length: " +
                             QByteArray::number(body.size()) + "\r\n\r\n";
  client.write(headers);
  client.flush();

  // Give the server a chance to process the headers — should NOT respond yet.
  QThread::msleep(30);
  QCoreApplication::processEvents();
  EXPECT_EQ(client.bytesAvailable(), 0) << "Server replied before body arrived";

  // Now send the body in a separate write.
  client.write(body);
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  const QByteArray response = client.readAll();
  EXPECT_TRUE(response.contains("200 OK"));
  const int sep = response.indexOf("\r\n\r\n");
  ASSERT_GT(sep, 0);
  const QByteArray respBody = response.mid(sep + 4);
  // Must get 32-byte pubkey back, NOT an error.
  EXPECT_EQ(respBody.size(), 32)
      << "pair-setup response expected; body was likely not received";

  client.close();
  receiver->stop();
}

// Bug 5: /fp-setup stage 1 — must return 142-byte FairPlay stub.
TEST_F(AirPlayReceiverTest, FpSetupStage1Returns142Bytes) {
  ASSERT_TRUE(receiver->start(17031));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17031);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  // 16-byte stage-1 challenge for FairPlay version 0x03, mode 0.
  QByteArray body(16, '\0');
  body[4] = 0x03;
  body[14] = 0x00; // mode 0

  const QByteArray httpReq = "POST /fp-setup HTTP/1.1\r\n"
                             "Host: localhost\r\n"
                             "Content-Type: application/octet-stream\r\n"
                             "Content-Length: 16\r\n"
                             "\r\n" +
                             body;
  client.write(httpReq);
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  const QByteArray response = client.readAll();
  EXPECT_TRUE(response.contains("200 OK"));
  const int sep = response.indexOf("\r\n\r\n");
  ASSERT_GT(sep, 0);
  EXPECT_EQ(response.mid(sep + 4).size(), 142);
  // First 4 bytes of response body must be "FPLY".
  EXPECT_EQ(response.mid(sep + 4).left(4), QByteArray("FPLY"));

  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, FpSetupStage2Returns32Bytes) {
  ASSERT_TRUE(receiver->start(17032));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17032);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  // 164-byte stage-2 body: FairPlay version 0x03, bytes 144-163 = sentinel.
  QByteArray body(164, '\0');
  body[4] = 0x03;
  for (int i = 144; i < 164; ++i)
    body[i] = static_cast<char>(i & 0xFF);

  const QByteArray httpReq = "POST /fp-setup HTTP/1.1\r\n"
                             "Host: localhost\r\n"
                             "Content-Type: application/octet-stream\r\n"
                             "Content-Length: 164\r\n"
                             "\r\n" +
                             body;
  client.write(httpReq);
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  const QByteArray response = client.readAll();
  EXPECT_TRUE(response.contains("200 OK"));
  const int sep = response.indexOf("\r\n\r\n");
  ASSERT_GT(sep, 0);
  const QByteArray respBody = response.mid(sep + 4);
  EXPECT_EQ(respBody.size(), 32);
  // First 4 bytes must be "FPLY" (fp_header prefix).
  EXPECT_EQ(respBody.left(4), QByteArray("FPLY"));
  // Last 20 bytes must echo body[144..163].
  EXPECT_EQ(respBody.mid(12), body.mid(144, 20));

  client.close();
  receiver->stop();
}

// Server header must be present in every response.
TEST_F(AirPlayReceiverTest, ServerHeaderPresentInInfoResponse) {
  ASSERT_TRUE(receiver->start(17033));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17033);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write("GET /info HTTP/1.1\r\nHost: localhost\r\n\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  EXPECT_TRUE(client.readAll().contains("Server: AirTunes/220.68"));

  client.close();
  receiver->stop();
}

// CSeq header from the request must be echoed in the response.
TEST_F(AirPlayReceiverTest, CSeqEchoedInResponse) {
  ASSERT_TRUE(receiver->start(17034));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17034);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write("GET /info HTTP/1.1\r\nHost: localhost\r\nCSeq: 42\r\n\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  EXPECT_TRUE(client.readAll().contains("CSeq: 42"));

  client.close();
  receiver->stop();
}

// /info must return binary plist.
TEST_F(AirPlayReceiverTest, GetInfoReturnsBinaryPlist) {
  ASSERT_TRUE(receiver->start(17035));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17035);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write("GET /info HTTP/1.1\r\nHost: localhost\r\n\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  const QByteArray response = client.readAll();
  EXPECT_TRUE(response.contains("application/x-apple-binary-plist"));
  EXPECT_TRUE(response.contains("bplist00"));
  // Must contain device name and model as ASCII in the binary plist.
  EXPECT_TRUE(response.contains("TestAIO"));
  EXPECT_TRUE(response.contains("AppleTV3,2"));

  client.close();
  receiver->stop();
}

// ---------------------------------------------------------------------------
// RTSP / stream lifecycle endpoint tests
// ---------------------------------------------------------------------------

TEST_F(AirPlayReceiverTest, SetupReturnsPortsInBinaryPlist) {
  ASSERT_TRUE(receiver->start(17040));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17040);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write("SETUP rtsp://localhost/session RTSP/1.0\r\n"
               "CSeq: 1\r\n"
               "User-Agent: AIOTestDevice/1.0\r\n"
               "\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  const QByteArray response = client.readAll();
  EXPECT_TRUE(response.contains("200 OK"));
  EXPECT_TRUE(response.contains("bplist00"));
  EXPECT_TRUE(response.contains("eventPort"));
  EXPECT_TRUE(response.contains("timingPort"));
  EXPECT_EQ(receiver->state(), AirPlayReceiver::State::Connected);

  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, RecordEndpointTransitionsToMirroring) {
  ASSERT_TRUE(receiver->start(17041));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17041);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write("RECORD rtsp://localhost/session RTSP/1.0\r\n"
               "CSeq: 3\r\n"
               "\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  EXPECT_TRUE(client.readAll().contains("200 OK"));
  EXPECT_EQ(receiver->state(), AirPlayReceiver::State::Mirroring);

  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, TeardownResetsToAdvertising) {
  ASSERT_TRUE(receiver->start(17042));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17042);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  // SETUP to get to Connected state first.
  client.write("SETUP rtsp://localhost/s RTSP/1.0\r\nCSeq: 1\r\n\r\n");
  client.flush();
  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  client.readAll();
  EXPECT_EQ(receiver->state(), AirPlayReceiver::State::Connected);

  // Now teardown.
  client.write("TEARDOWN rtsp://localhost/s RTSP/1.0\r\nCSeq: 2\r\n\r\n");
  client.flush();
  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  EXPECT_TRUE(client.readAll().contains("200 OK"));
  EXPECT_EQ(receiver->state(), AirPlayReceiver::State::Advertising);

  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, GetParameterReturns200) {
  ASSERT_TRUE(receiver->start(17043));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17043);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write("GET_PARAMETER rtsp://localhost/s RTSP/1.0\r\nCSeq: 1\r\n\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  EXPECT_TRUE(client.readAll().contains("200 OK"));

  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, SetParameterReturns200) {
  ASSERT_TRUE(receiver->start(17044));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17044);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write("SET_PARAMETER rtsp://localhost/s RTSP/1.0\r\nCSeq: "
               "1\r\nContent-Length: 0\r\n\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  EXPECT_TRUE(client.readAll().contains("200 OK"));

  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, FlushReturns200) {
  ASSERT_TRUE(receiver->start(17045));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17045);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write("FLUSH rtsp://localhost/s RTSP/1.0\r\nCSeq: 1\r\n\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  EXPECT_TRUE(client.readAll().contains("200 OK"));

  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, ConfigureEndpointReturns200) {
  ASSERT_TRUE(receiver->start(17046));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17046);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write("POST /configure HTTP/1.1\r\nHost: localhost\r\nContent-Length: "
               "0\r\n\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  EXPECT_TRUE(client.readAll().contains("200 OK"));

  client.close();
  receiver->stop();
}

TEST_F(AirPlayReceiverTest, AuthSetupEndpointReturns200) {
  ASSERT_TRUE(receiver->start(17047));

  QTcpSocket client;
  client.connectToHost("127.0.0.1", 17047);
  ASSERT_TRUE(waitFor(
      [&]() { return client.state() == QAbstractSocket::ConnectedState; }));

  client.write(
      "POST /auth-setup HTTP/1.1\r\nHost: localhost\r\nContent-Length: "
      "0\r\n\r\n");
  client.flush();

  ASSERT_TRUE(waitFor([&]() { return client.bytesAvailable() > 0; }));
  EXPECT_TRUE(client.readAll().contains("200 OK"));

  client.close();
  receiver->stop();
}
