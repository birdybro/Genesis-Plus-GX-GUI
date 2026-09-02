#include "genplusgx/emulation_worker.h"
#include "genplusgx/netplay/netplay_session.h"
#include "synthetic_rom.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QTcpServer>
#include <QTest>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace {

using namespace std::chrono_literals;

genplusgx::netplay::NetplaySessionDescriptor descriptor()
{
  return {
    .gameSha256 = std::string(64U, 'a'),
    .settingsSha256 = std::string(64U, 'b'),
    .coreVersion = "netplay-end-to-end-test",
  };
}

std::optional<genplusgx::EmulationEvent> waitForOperation(
  genplusgx::EmulationWorker& worker,
  std::uint64_t operationId,
  int timeoutMilliseconds = 4'000)
{
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeoutMilliseconds) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    if (auto event = worker.waitForEvent(5ms);
        event && event->operationId == operationId) {
      return event;
    }
  }
  return std::nullopt;
}

bool submitAndWait(
  genplusgx::EmulationWorker& worker,
  genplusgx::EmulationCommand command)
{
  const auto operationId = command.operationId;
  if (!worker.submit(std::move(command))) {
    return false;
  }
  const auto event = waitForOperation(worker, operationId);
  return event && event->succeeded();
}

struct RuntimeObservation final {
  std::uint64_t latestFrame{0U};
  std::uint64_t latestRemoteFrame{0U};
  std::string failure;
};

void observeWorker(
  genplusgx::EmulationWorker& worker,
  RuntimeObservation& observation)
{
  while (auto event = worker.pollEvent()) {
    if (event->type == genplusgx::EmulationEventType::frameCompleted) {
      observation.latestFrame = std::max(
        observation.latestFrame, event->frameNumber);
    } else if (!event->succeeded() && observation.failure.empty()) {
      observation.failure = event->message.empty()
        ? "An emulation operation failed." : event->message;
    }
  }
}

int peerFailure(const std::string& message)
{
  std::cerr << "PEER FAILURE: " << message << '\n';
  return 1;
}

int runPeer(bool hosting, std::uint16_t port)
{
  using namespace genplusgx;
  using namespace genplusgx::netplay;

  const test::TemporaryFixture fixture{
    test::makeGenesisRamMarkerRom(), ".bin"};
  auto bridge = std::make_shared<NetplayBridge>(256U);
  EmulationWorker worker{128U, 128U, 48'000, {}, {}, {}, {}, bridge};
  if (!worker.start()) {
    return peerFailure("worker start failed");
  }
  static_cast<void>(worker.waitForEvent(1s));
  if (!submitAndWait(worker, EmulationCommand::load(1U, fixture.path()))) {
    return peerFailure("generated game load failed");
  }

  NetplaySession session;
  std::uint64_t remoteOperation = 10'000U;
  std::string sessionFailure;
  NetplaySessionError sessionError = NetplaySessionError::none;
  bool peerDisconnected = false;
  RuntimeObservation observation;
  QObject::connect(&session, &NetplaySession::inputReceived, &session,
    [&worker, &remoteOperation, &sessionFailure,
     &observation](NetplayInputFrame frame) {
      observation.latestRemoteFrame = std::max(
        observation.latestRemoteFrame, frame.frameNumber);
      const auto status = worker.submit(EmulationCommand::remoteNetplayFrame(
        ++remoteOperation, frame));
      if (!status && sessionFailure.empty()) {
        sessionFailure = status.message;
      }
    });
  QObject::connect(&session, &NetplaySession::sessionError, &session,
    [&sessionError, &sessionFailure](NetplaySessionError error,
                                    const QString& detail) {
      sessionError = error;
      if (sessionFailure.empty()) {
        sessionFailure = detail.toStdString();
      }
    });
  QObject::connect(&session, &NetplaySession::peerDisconnected, &session,
    [&peerDisconnected](const QString&) { peerDisconnected = true; });

  const auto connectionStatus = hosting
    ? session.host(descriptor(), "end-to-end-code", port, 2U, 8U,
        QHostAddress::LocalHost)
    : session.join(QStringLiteral("127.0.0.1"), descriptor(),
        "end-to-end-code", port, 2U, 8U);
  if (!connectionStatus) {
    return peerFailure(connectionStatus.message);
  }
  if (hosting) {
    std::cout << "READY\n" << std::flush;
  }
  QElapsedTimer connectTimer;
  connectTimer.start();
  while (session.state() != NetplaySessionState::connected &&
         sessionFailure.empty() && connectTimer.elapsed() < 5'000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QTest::qWait(1);
  }
  if (session.state() != NetplaySessionState::connected) {
    return peerFailure(sessionFailure.empty()
      ? "authenticated connection timed out" : sessionFailure);
  }

  InputSnapshot localInput;
  const auto localPlayer = hosting ? 0U : 1U;
  localInput.players[localPlayer] = {
    .connected = true,
    .buttons = buttonMask(hosting ? InputButton::a : InputButton::b),
  };
  if (!submitAndWait(worker,
        EmulationCommand::updateInput(3U, localInput))) {
    return peerFailure("local input application failed");
  }
  const NetplayConfiguration configuration{
    .role = hosting ? NetplayRole::host : NetplayRole::guest,
    .localPlayer = localPlayer,
    .remotePlayer = hosting ? 1U : 0U,
    .inputDelayFrames = 2U,
    .rollbackFrames = 8U,
  };
  if (!submitAndWait(worker,
        EmulationCommand::startNetplaySession(4U, configuration))) {
    return peerFailure("atomic netplay worker startup failed");
  }

  std::optional<NetplayInputFrame> delayedFrame;
  bool delayedFrameSent = hosting;
  QElapsedTimer delayedFrameTimer;
  QElapsedTimer runtime;
  runtime.start();
  bool runtimeValid = false;
  while (runtime.elapsed() < 10'000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    observeWorker(worker, observation);
    while (auto frame = bridge->pollOutgoing()) {
      // Exercise a real rollback only after both paced peers are exchanging
      // steady-state frames. Delaying frame 2 tied this test to process startup
      // speed and could exceed the rollback window under Intel emulation on an
      // Apple Silicon CI host.
      if (!hosting && !delayedFrame &&
          observation.latestRemoteFrame >= 30U &&
          frame->frameNumber >= 30U) {
        delayedFrame = *frame;
        delayedFrame->state.buttons = static_cast<InputButtonSet>(
          delayedFrame->state.buttons ^ buttonMask(InputButton::c));
        delayedFrameTimer.start();
        continue;
      }
      if (const auto sent = session.sendInput(*frame); !sent) {
        sessionFailure = sent.message;
        break;
      }
    }
    if (!hosting && delayedFrame && !delayedFrameSent &&
        delayedFrameTimer.elapsed() >= 80) {
      if (const auto sent = session.sendInput(*delayedFrame); !sent) {
        sessionFailure = sent.message;
      } else {
        delayedFrameSent = true;
      }
    }
    if (!sessionFailure.empty() || !observation.failure.empty() ||
        session.state() == NetplaySessionState::disconnected) {
      break;
    }
    const auto workerMetrics = worker.metrics();
    const auto transportMetrics = session.metrics();
    runtimeValid = delayedFrameSent && observation.latestFrame >= 120U &&
      workerMetrics.netplayActive &&
      (!hosting || workerMetrics.netplayRollbacks > 0U) &&
      workerMetrics.netplayHistoryFrames <= 9U &&
      bridge->metrics().outgoingDepth <= bridge->metrics().outgoingCapacity &&
      transportMetrics.sentPackets > 100U &&
      transportMetrics.receivedPackets > 100U;
    if (runtimeValid) {
      break;
    }
    QTest::qWait(1);
  }

  const auto workerMetrics = worker.metrics();
  const auto transportMetrics = session.metrics();
  if (!runtimeValid || !sessionFailure.empty() ||
      !observation.failure.empty() || peerDisconnected ||
      sessionError != NetplaySessionError::none) {
    session.disconnectFromPeer("End-to-end test failed.");
    static_cast<void>(submitAndWait(worker, EmulationCommand::simple(
      EmulationCommandType::stopNetplay, 30'001U)));
    static_cast<void>(worker.stop());
    return peerFailure("runtime validation failed: " +
      (sessionFailure.empty() ? observation.failure : sessionFailure) +
      " frame=" + std::to_string(observation.latestFrame) +
      " remote=" + std::to_string(observation.latestRemoteFrame) +
      " rollbacks=" + std::to_string(workerMetrics.netplayRollbacks) +
      " sent=" + std::to_string(transportMetrics.sentPackets) +
      " received=" + std::to_string(transportMetrics.receivedPackets));
  }

  std::cout << "VERIFIED frame=" << observation.latestFrame
            << " rollbacks=" << workerMetrics.netplayRollbacks << '\n'
            << std::flush;
  std::string control;
  if (!std::getline(std::cin, control) || control != "STOP") {
    return peerFailure("parent did not release the verified peer");
  }
  session.disconnectFromPeer("End-to-end test complete.");
  const bool stopped = submitAndWait(worker, EmulationCommand::simple(
    EmulationCommandType::stopNetplay, 30'001U)) && worker.stop();
  if (!stopped) {
    return peerFailure("worker shutdown failed");
  }
  std::cout << "PASS frame=" << observation.latestFrame
            << " rollbacks=" << workerMetrics.netplayRollbacks << '\n';
  return 0;
}

std::string processOutput(QProcess& process)
{
  return process.readAll().toStdString();
}

bool waitForBothVerified(
  QProcess& host,
  QProcess& guest,
  std::string& hostLog,
  std::string& guestLog)
{
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 15'000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    hostLog += processOutput(host);
    guestLog += processOutput(guest);
    if (hostLog.find("VERIFIED frame=") != std::string::npos &&
        guestLog.find("VERIFIED frame=") != std::string::npos) {
      return true;
    }
    if (host.state() == QProcess::NotRunning ||
        guest.state() == QProcess::NotRunning) {
      return false;
    }
    QTest::qWait(1);
  }
  hostLog += processOutput(host);
  guestLog += processOutput(guest);
  return false;
}

} // namespace

class NetplayEndToEndTest final : public QObject {
  Q_OBJECT

private slots:
  void twoAuthenticatedProcessesExchangeAndRollback()
  {
    QTcpServer reservation;
    QVERIFY(reservation.listen(QHostAddress::LocalHost, 0U));
    const auto port = reservation.serverPort();
    reservation.close();

    const auto executable = QCoreApplication::applicationFilePath();
    QProcess host;
    host.setProcessChannelMode(QProcess::MergedChannels);
    host.start(executable,
      {QStringLiteral("--peer"), QStringLiteral("host"),
       QString::number(port)});
    QVERIFY(host.waitForStarted(5'000));
    QVERIFY2(host.waitForReadyRead(5'000), "Host did not publish readiness");
    const auto readiness = host.readAllStandardOutput();
    QVERIFY2(readiness.contains("READY"), readiness.constData());
    std::string hostLog = readiness.toStdString();

    QProcess guest;
    guest.setProcessChannelMode(QProcess::MergedChannels);
    guest.start(executable,
      {QStringLiteral("--peer"), QStringLiteral("guest"),
       QString::number(port)});
    QVERIFY(guest.waitForStarted(5'000));
    std::string guestLog;
    const bool bothVerified = waitForBothVerified(
      host, guest, hostLog, guestLog);
    const auto verificationLog =
      "host:\n" + hostLog + "\nguest:\n" + guestLog;
    QVERIFY2(bothVerified, verificationLog.c_str());
    QCOMPARE(host.write("STOP\n"), qint64{5});
    QCOMPARE(guest.write("STOP\n"), qint64{5});
    QVERIFY(host.waitForBytesWritten(5'000));
    QVERIFY(guest.waitForBytesWritten(5'000));
    const bool guestFinished = guest.waitForFinished(15'000);
    const bool hostFinished = host.waitForFinished(15'000);
    guestLog += processOutput(guest);
    hostLog += processOutput(host);
    QVERIFY2(guestFinished, guestLog.c_str());
    QVERIFY2(hostFinished, hostLog.c_str());
    QCOMPARE(guest.exitStatus(), QProcess::NormalExit);
    QCOMPARE(host.exitStatus(), QProcess::NormalExit);
    QVERIFY2(guest.exitCode() == 0, guestLog.c_str());
    QVERIFY2(host.exitCode() == 0, hostLog.c_str());
    QVERIFY2(guestLog.find("PASS frame=") != std::string::npos,
      guestLog.c_str());
    QVERIFY2(hostLog.find("PASS frame=") != std::string::npos,
      hostLog.c_str());
    QVERIFY2(hostLog.find("rollbacks=0") == std::string::npos,
      hostLog.c_str());
  }
};

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  const auto arguments = application.arguments();
  if (arguments.size() == 4 && arguments[1] == QStringLiteral("--peer")) {
    bool portValid = false;
    const auto port = arguments[3].toUShort(&portValid);
    if (!portValid ||
        (arguments[2] != QStringLiteral("host") &&
         arguments[2] != QStringLiteral("guest"))) {
      return 2;
    }
    return runPeer(arguments[2] == QStringLiteral("host"), port);
  }
  NetplayEndToEndTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "netplay_end_to_end_test.moc"
