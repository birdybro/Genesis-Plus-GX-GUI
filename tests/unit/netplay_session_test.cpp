#include "genplusgx/netplay/netplay_session.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include <functional>

namespace {

genplusgx::netplay::NetplaySessionDescriptor descriptor(char game = 'a')
{
  return {
    .gameSha256 = std::string(64U, game),
    .settingsSha256 = std::string(64U, 'b'),
    .coreVersion = "netplay-test",
  };
}

bool waitUntil(const std::function<bool()>& predicate, int timeout = 5'000)
{
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QTest::qWait(1);
  }
  return predicate();
}

} // namespace

class NetplaySessionTest final : public QObject {
  Q_OBJECT

private slots:
  void authenticatedLoopbackExchange()
  {
    using namespace genplusgx::netplay;
    NetplaySession host;
    NetplaySession guest;
    QSignalSpy hostConnected{&host, &NetplaySession::peerConnected};
    QSignalSpy guestConnected{&guest, &NetplaySession::peerConnected};
    QSignalSpy received{&guest, &NetplaySession::inputReceived};

    QVERIFY(host.host(descriptor(), "shared-secret", 0U, 2U, 8U,
      QHostAddress::LocalHost));
    QVERIFY(host.listeningPort() != 0U);
    QVERIFY(guest.join(QStringLiteral("127.0.0.1"), descriptor(),
      "shared-secret", host.listeningPort(), 2U, 8U));
    QVERIFY(waitUntil([&] {
      return host.state() == NetplaySessionState::connected &&
        guest.state() == NetplaySessionState::connected;
    }));
    QCOMPARE(hostConnected.size(), 1);
    QCOMPARE(guestConnected.size(), 1);
    QCOMPARE(host.configuration().localPlayer, 0U);
    QCOMPARE(guest.configuration().localPlayer, 1U);

    const NetplayInputFrame frame{
      .frameNumber = 23U,
      .state = {.connected = true, .buttons = 0x55U,
        .analogX = -10, .analogY = 20},
    };
    QVERIFY(host.sendInput(frame));
    QVERIFY(waitUntil([&] { return received.size() == 1; }));
    const auto delivered = qvariant_cast<NetplayInputFrame>(received.at(0).at(0));
    QCOMPARE(delivered, frame);
    QCOMPARE(host.metrics().sentPackets, 3U);
    QCOMPARE(guest.metrics().receivedPackets, 3U);

    host.disconnectFromPeer("test complete");
    QVERIFY(waitUntil([&] {
      return guest.state() == NetplaySessionState::disconnected;
    }));
  }

  void rejectsWrongCodeAndIdentity()
  {
    using namespace genplusgx::netplay;
    {
      NetplaySession host;
      NetplaySession guest;
      QSignalSpy errors{&host, &NetplaySession::sessionError};
      QVERIFY(host.host(descriptor(), "right-code", 0U, 2U, 8U,
        QHostAddress::LocalHost));
      QVERIFY(guest.join(QStringLiteral("127.0.0.1"), descriptor(),
        "wrong-code", host.listeningPort(), 2U, 8U));
      QVERIFY(waitUntil([&] { return !errors.isEmpty(); }));
      QCOMPARE(host.state(), NetplaySessionState::disconnected);
      QVERIFY(host.metrics().authenticationFailures > 0U);
    }
    {
      NetplaySession host;
      NetplaySession guest;
      QSignalSpy errors{&host, &NetplaySession::sessionError};
      QVERIFY(host.host(descriptor(), "same-code", 0U, 2U, 8U,
        QHostAddress::LocalHost));
      QVERIFY(guest.join(QStringLiteral("127.0.0.1"), descriptor('c'),
        "same-code", host.listeningPort(), 2U, 8U));
      QVERIFY(waitUntil([&] { return !errors.isEmpty(); }));
      const auto error = qvariant_cast<NetplaySessionError>(errors.at(0).at(0));
      QCOMPARE(error, NetplaySessionError::incompatiblePeer);
    }
  }

  void boundsTransportOutputQueue()
  {
    using namespace genplusgx::netplay;
    NetplaySession host;
    NetplaySession guest;
    QSignalSpy errors{&host, &NetplaySession::sessionError};
    QVERIFY(host.host(descriptor(), "bounded-output", 0U, 2U, 8U,
      QHostAddress::LocalHost));
    QVERIFY(guest.join(QStringLiteral("127.0.0.1"), descriptor(),
      "bounded-output", host.listeningPort(), 2U, 8U));
    QVERIFY(waitUntil([&] {
      return host.state() == NetplaySessionState::connected &&
        guest.state() == NetplaySessionState::connected;
    }));

    NetplaySessionStatus status;
    std::uint64_t frame = 0U;
    while (status && frame < 10'000U) {
      status = host.sendInput({.frameNumber = frame++, .state = {}});
    }
    QVERIFY(!status);
    QCOMPARE(status.error, NetplaySessionError::transportFailure);
    QCOMPARE(host.state(), NetplaySessionState::disconnected);
    QCOMPARE(errors.size(), 1);
    QVERIFY(host.metrics().sentBytes <=
      static_cast<std::uint64_t>(maximumQueuedWireBytes +
        maximumWirePacketBytes));
  }

  void validatesConfiguration()
  {
    using namespace genplusgx::netplay;
    NetplaySession session;
    QVERIFY(!session.host(descriptor(), "short", 0U));
    QVERIFY(!session.join(QString{}, descriptor(), "long-enough", 1U));
    QVERIFY(!session.join(QString(256, QLatin1Char('a')), descriptor(),
      "long-enough", 1U));
    QVERIFY(!session.host(descriptor(), "long-enough", 0U, 9U, 8U));
  }
};

QTEST_GUILESS_MAIN(NetplaySessionTest)
#include "netplay_session_test.moc"
