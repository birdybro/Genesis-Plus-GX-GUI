#include "genplusgx/capture/capture_fanout.h"
#include "genplusgx/capture/streaming_service.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

namespace {

using namespace std::chrono_literals;

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::uint16_t availablePort()
{
  QTcpServer probe;
  if (!probe.listen(QHostAddress::LocalHost, 0U)) {
    return 0U;
  }
  return probe.serverPort();
}

class ProbeSink final : public genplusgx::EmulationCaptureSink {
public:
  [[nodiscard]] bool active() const noexcept override { return enabled; }
  [[nodiscard]] bool submitFrame(
    const genplusgx::CoreVideoFrameInfo&,
    std::span<const std::uint16_t>,
    const genplusgx::CoreAudioBatchInfo&,
    std::span<const genplusgx::StereoAudioFrame>) noexcept override
  {
    ++calls;
    return accepts;
  }

  bool enabled{true};
  bool accepts{true};
  std::size_t calls{0U};
};

} // namespace

int main(int argc, char** argv)
{
  QCoreApplication application{argc, argv};
  auto first = std::make_shared<ProbeSink>();
  auto second = std::make_shared<ProbeSink>();
  second->accepts = false;
  genplusgx::capture::CaptureFanout fanout{{first, second}};
  genplusgx::CoreVideoFrameInfo video{
    .format = genplusgx::CorePixelFormat::rgb565,
    .width = 2U,
    .height = 2U,
    .frameNumber = 17U,
  };
  const std::array<std::uint16_t, 4U> pixels{
    0xf800U, 0x07e0U, 0x001fU, 0xffffU};
  const std::array<genplusgx::StereoAudioFrame, 2U> audio{{
    {.left = -100, .right = 100}, {.left = -200, .right = 200}}};
  const genplusgx::CoreAudioBatchInfo audioInfo{
    .sampleRate = 48'000U,
    .channels = 2U,
    .frameCount = audio.size(),
    .emulatedFrameNumber = 17U,
  };
  if (!check(fanout.active() &&
        fanout.submitFrame(video, pixels, audioInfo, audio),
        "Capture fan-out rejected an active sink") ||
      !check(first->calls == 1U && second->calls == 1U,
        "Capture fan-out did not call every active sink")) {
    return 1;
  }

  genplusgx::capture::StreamingService service{2U, 8U};
  if (!check(service.start({.port = 0U}).error ==
        genplusgx::capture::StreamingError::invalidConfiguration,
        "Invalid stream configuration was accepted")) {
    return 1;
  }
  const auto port = availablePort();
  if (!check(port != 0U && service.start({.port = port, .maximumClients = 1U}),
        "Loopback stream could not start")) {
    return 1;
  }
  const auto started = service.waitForEvent(2s);
  if (!check(started &&
        started->type == genplusgx::capture::StreamingEventType::started &&
        started->metrics.active && started->metrics.port == port,
        "Loopback stream did not report readiness") ||
      !check(service.start({.port = port}).error ==
        genplusgx::capture::StreamingError::alreadyRunning,
        "Duplicate stream start was accepted")) {
    return 1;
  }

  QTcpSocket client;
  client.connectToHost(QHostAddress::LocalHost, port);
  if (!check(client.waitForConnected(2'000),
        "Loopback stream client could not connect") ||
      !check(client.waitForReadyRead(2'000),
        "Loopback stream greeting was not delivered")) {
    return 1;
  }
  QByteArray received = client.readAll();
  if (!check(received.startsWith("GPGX-AV/1\n"),
        "Loopback stream greeting was invalid") ||
      !check(service.submitFrame(video, pixels, audioInfo, audio),
        "Valid A/V frame was rejected")) {
    return 1;
  }
  QTcpSocket excessClient;
  excessClient.connectToHost(QHostAddress::LocalHost, port);
  if (!check(excessClient.waitForConnected(2'000),
        "Excess-client connection did not reach the listener")) {
    return 1;
  }
  const auto excessDeadline = std::chrono::steady_clock::now() + 2s;
  while (excessClient.state() != QAbstractSocket::UnconnectedState &&
         std::chrono::steady_clock::now() < excessDeadline) {
    static_cast<void>(excessClient.waitForDisconnected(50));
  }
  if (!check(excessClient.state() == QAbstractSocket::UnconnectedState &&
        service.metrics().connectedClients <= 1U,
        "The configured stream client limit was not enforced")) {
    return 1;
  }
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (!received.contains("GXF1") &&
         std::chrono::steady_clock::now() < deadline) {
    static_cast<void>(client.waitForReadyRead(50));
    received.append(client.readAll());
  }
  if (!check(received.contains("GXF1"),
        "Framed A/V packet was not delivered")) {
    return 1;
  }
  for (std::size_t index = 0U; index < 5'000U; ++index) {
    static_cast<void>(service.submitFrame(video, pixels, audioInfo, audio));
  }
  const auto metrics = service.metrics();
  if (!check(metrics.queueDepth <= metrics.queueCapacity &&
        metrics.peakQueueDepth <= metrics.queueCapacity &&
        metrics.acceptedFrames + metrics.droppedFrames >= 5'001U,
        "Stream frame queue did not stay bounded")) {
    return 1;
  }
  if (!check(service.stop(), "Loopback stream did not stop cleanly") ||
      !check(!service.active() && !service.metrics().active,
        "Loopback stream remained active after stop")) {
    return 1;
  }
  const auto restartPort = availablePort();
  if (!check(restartPort != 0U &&
        service.start({.port = restartPort, .maximumClients = 1U}),
        "Loopback stream could not restart") ||
      !check(service.waitForEvent(2s).has_value(),
        "Restarted stream did not publish readiness") ||
      !check(service.stop(), "Restarted stream did not stop cleanly")) {
    return 1;
  }

  QTcpServer occupied;
  const auto occupiedPort = availablePort();
  if (!check(occupiedPort != 0U &&
        occupied.listen(QHostAddress::LocalHost, occupiedPort),
        "Port-collision fixture could not listen")) {
    return 1;
  }
  genplusgx::capture::StreamingService collision;
  if (!check(collision.start({.port = occupiedPort}),
        "Collision service thread could not start")) {
    return 1;
  }
  const auto failed = collision.waitForEvent(2s);
  if (!check(failed &&
        failed->type == genplusgx::capture::StreamingEventType::failed &&
        failed->status.error == genplusgx::capture::StreamingError::listenFailed,
        "Port collision did not fail descriptively") ||
      !check(collision.stop().error ==
        genplusgx::capture::StreamingError::listenFailed,
        "Port collision failure was lost during shutdown")) {
    return 1;
  }
  return 0;
}
