#include "genplusgx/capture/streaming_service.h"
#include "genplusgx/emulation_worker.h"
#include "synthetic_rom.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <utility>

namespace {

using namespace std::chrono_literals;

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::uint32_t readU32(const QByteArray& bytes, qsizetype offset)
{
  std::uint32_t value = 0U;
  for (unsigned index = 0U; index < 4U; ++index) {
    value |= static_cast<std::uint32_t>(
      static_cast<std::uint8_t>(bytes[offset + index])) << (index * 8U);
  }
  return value;
}

std::optional<genplusgx::EmulationEvent> waitForWorkerOperation(
  genplusgx::EmulationWorker& worker, std::uint64_t operationId)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = worker.waitForEvent(100ms);
    if (event && event->operationId == operationId) {
      return event;
    }
  }
  return std::nullopt;
}

bool submitAndSucceed(
  genplusgx::EmulationWorker& worker, genplusgx::EmulationCommand command)
{
  const auto operationId = command.operationId;
  if (!worker.submit(std::move(command))) {
    return false;
  }
  const auto event = waitForWorkerOperation(worker, operationId);
  return event && event->succeeded();
}

} // namespace

int main(int argc, char** argv)
{
  QCoreApplication application{argc, argv};
  const genplusgx::test::TemporaryFixture game{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  QTcpServer portProbe;
  if (!check(portProbe.listen(QHostAddress::LocalHost, 0U),
        "An ephemeral loopback test port was unavailable")) {
    return 1;
  }
  const auto port = portProbe.serverPort();
  portProbe.close();
  auto stream = std::make_shared<genplusgx::capture::StreamingService>(8U);
  if (!check(stream->start({.port = port, .maximumClients = 1U}),
        "Loopback stream could not start")) {
    return 1;
  }
  const auto started = stream->waitForEvent(3s);
  if (!check(started &&
        started->type == genplusgx::capture::StreamingEventType::started &&
        started->metrics.port == port,
        "Loopback stream did not publish its listening endpoint")) {
    return 1;
  }

  QTcpSocket client;
  client.connectToHost(QHostAddress::LocalHost, started->metrics.port);
  if (!check(client.waitForConnected(3'000),
        "A real loopback stream client could not connect") ||
      !check(client.waitForReadyRead(3'000),
        "The loopback stream greeting was not delivered")) {
    return 1;
  }
  QByteArray received = client.readAll();
  while (received.size() < 10 && client.waitForReadyRead(500)) {
    received += client.readAll();
  }
  if (!check(received.startsWith("GPGX-AV/1\n"),
        "The stream protocol greeting was invalid")) {
    return 1;
  }
  received.remove(0, 10);

  genplusgx::EmulationWorker worker{64U, 64U, 48'000, {}, {}, {}, stream};
  if (!check(worker.start(), "Streaming workflow worker could not start") ||
      !check(worker.waitForEvent(2s).has_value(),
        "Streaming workflow worker start event was missing") ||
      !check(submitAndSucceed(
        worker, genplusgx::EmulationCommand::load(1U, game.path())),
        "Streaming workflow game could not load")) {
    return 1;
  }
  for (std::uint64_t operation = 2U; operation < 6U; ++operation) {
    if (!check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::frameAdvance, operation)),
          "A streamed frame advance failed")) {
      return 1;
    }
  }

  const auto deadline = std::chrono::steady_clock::now() + 5s;
  qsizetype packetBytes = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    if (client.bytesAvailable() > 0 || client.waitForReadyRead(250)) {
      received += client.readAll();
    }
    if (received.size() >= 8 && received.startsWith("GXF1")) {
      packetBytes = 8 + static_cast<qsizetype>(readU32(received, 4));
      if (received.size() >= packetBytes) {
        break;
      }
    }
  }
  if (!check(packetBytes >= 40 && received.size() >= packetBytes,
        "A complete native A/V packet did not reach the real client") ||
      !check(readU32(received, 16) > 0U && readU32(received, 20) > 0U,
        "The streamed native video geometry was invalid") ||
      !check(readU32(received, 28) == 48'000U &&
        readU32(received, 32) > 0U && readU32(received, 36) > 0U,
        "The streamed native audio/video counts were invalid")) {
    return 1;
  }
  const auto metrics = stream->metrics();
  if (!check(metrics.active && metrics.connectedClients == 1U &&
        metrics.acceptedFrames >= 1U && metrics.broadcastFrames >= 1U &&
        metrics.queueDepth <= metrics.queueCapacity &&
        metrics.peakQueueDepth <= metrics.queueCapacity,
        "The bounded stream metrics were inconsistent")) {
    return 1;
  }
  if (!check(submitAndSucceed(worker,
        genplusgx::EmulationCommand::simple(
          genplusgx::EmulationCommandType::unloadGame, 10U)),
        "Streaming workflow game could not unload") ||
      !check(worker.stop(), "Streaming workflow worker could not stop") ||
      !check(stream->stop(), "Loopback stream could not stop")) {
    return 1;
  }
  return 0;
}
