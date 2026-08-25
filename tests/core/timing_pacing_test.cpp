#include "genplusgx/emulation_worker.h"
#include "synthetic_rom.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <thread>
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

std::optional<genplusgx::EmulationEvent> waitForOperation(
  genplusgx::EmulationWorker& worker,
  std::uint64_t operationId)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = worker.waitForEvent(50ms);
    if (event && event->operationId == operationId) {
      return event;
    }
  }
  return std::nullopt;
}

bool submitAndSucceed(
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

bool waitForPacedFrames(
  genplusgx::EmulationWorker& worker,
  std::uint64_t target,
  std::chrono::steady_clock::time_point deadline)
{
  while (std::chrono::steady_clock::now() < deadline) {
    if (worker.metrics().pacedFrameCount >= target) {
      return true;
    }
    static_cast<void>(worker.waitForEvent(20ms));
  }
  return worker.metrics().pacedFrameCount >= target;
}

} // namespace

int main()
{
  const genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".bin"};
  auto palRom = genplusgx::test::makeGenesisRamMarkerRom();
  palRom.at(0x1F0U) = static_cast<std::uint8_t>('E');
  const genplusgx::test::TemporaryFixture palFixture{std::move(palRom), ".bin"};
  genplusgx::EmulationWorker worker;
  if (!check(worker.start(), "Timing worker did not start") ||
      !check(worker.waitForEvent(2s).has_value(), "Worker start event was missing") ||
      !check(submitAndSucceed(
          worker, genplusgx::EmulationCommand::load(1U, fixture.path())),
        "Timing fixture did not load")) {
    return 1;
  }

  const auto loadedMetrics = worker.metrics();
  if (!check(loadedMetrics.targetFramesPerSecond > 59.92 &&
          loadedMetrics.targetFramesPerSecond < 59.93 &&
          !loadedMetrics.fastForward,
        "Worker did not adopt the core's exact NTSC rate") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::start, 2U)),
        "Timed emulation did not start")) {
    return 2;
  }

  const auto normalBaseline = worker.metrics().pacedFrameCount;
  const auto normalStart = std::chrono::steady_clock::now();
  constexpr std::uint64_t normalFrames = 30U;
  if (!check(waitForPacedFrames(
          worker, normalBaseline + normalFrames, normalStart + 2s),
        "Normal pacing did not produce 30 frames")) {
    return 3;
  }
  const auto normalElapsed = std::chrono::steady_clock::now() - normalStart;
  const auto normalSeconds = std::chrono::duration<double>(normalElapsed).count();
  const auto normalRate = static_cast<double>(normalFrames) / normalSeconds;
  if (!check(normalElapsed > 400ms && normalElapsed < 800ms,
        "Normal pacing ran wildly outside the NTSC frame interval") ||
      !check(normalRate > 40.0 && normalRate < 80.0,
        "Measured normal frame rate is outside its integration tolerance") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::pause, 3U)),
        "Timed emulation did not pause")) {
    return 4;
  }

  const auto pausedCount = worker.metrics().pacedFrameCount;
  std::this_thread::sleep_for(80ms);
  if (!check(worker.metrics().pacedFrameCount == pausedCount,
        "Paused emulation continued scheduling frames") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::frameAdvance, 4U)),
        "Paused frame advance failed") ||
      !check(worker.metrics().pacedFrameCount == pausedCount,
        "Frame advance incorrectly restarted the continuous scheduler")) {
    return 5;
  }
  worker.audioFrames()->clear();

  if (!check(submitAndSucceed(
          worker, genplusgx::EmulationCommand::fastForward(5U, true)),
        "Fast-forward mode did not enable") ||
      !check(std::abs(worker.metrics().targetFramesPerSecond -
          loadedMetrics.targetFramesPerSecond * 4.0) < 0.000001,
        "Fast-forward target is not exactly four times the core rate") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::resume, 6U)),
        "Fast-forward emulation did not resume")) {
    return 6;
  }

  const auto fastBaseline = worker.metrics().pacedFrameCount;
  const auto fastStart = std::chrono::steady_clock::now();
  constexpr std::uint64_t fastFrames = 40U;
  if (!check(waitForPacedFrames(
          worker, fastBaseline + fastFrames, fastStart + 1s),
        "Fast-forward pacing did not produce 40 frames")) {
    return 7;
  }
  const auto fastElapsed = std::chrono::steady_clock::now() - fastStart;
  const auto fastRate =
    static_cast<double>(fastFrames) /
    std::chrono::duration<double>(fastElapsed).count();
  if (!check(fastElapsed > 100ms && fastElapsed < 400ms,
        "Fast-forward pacing ran outside its bounded 4x interval") ||
      !check(fastRate > normalRate * 2.5 && fastRate < 350.0,
        "Fast-forward did not materially accelerate without running wild") ||
      !check(worker.audioFrames()->occupancyFrames() == 0U,
        "Fast-forward accumulated host audio that cannot play in real time") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::pause, 7U)),
        "Fast-forward emulation did not pause") ||
      !check(worker.metrics().maximumLatenessMicroseconds < 1'000'000,
        "Pacing instrumentation reported an unbounded late frame")) {
    return 8;
  }

  if (!check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::unloadGame, 8U)),
        "NTSC timing fixture did not unload") ||
      !check(submitAndSucceed(
          worker, genplusgx::EmulationCommand::load(9U, palFixture.path())),
        "PAL timing fixture did not load") ||
      !check(worker.metrics().targetFramesPerSecond > 49.70 &&
          worker.metrics().targetFramesPerSecond < 49.71 &&
          !worker.metrics().fastForward,
        "Worker did not propagate the PAL core cadence") ||
      !check(worker.stop(), "Timing worker did not stop cleanly")) {
    return 9;
  }

  return 0;
}
