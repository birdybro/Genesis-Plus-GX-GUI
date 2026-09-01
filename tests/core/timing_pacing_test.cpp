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
          loadedMetrics.speedPercent == 100U && !loadedMetrics.fastForward &&
          !loadedMetrics.slowMotion,
        "Worker did not adopt the core's exact NTSC rate") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::updateSpeedSettings(2U, {
            .normalPercent = 75U,
            .slowMotionPercent = 25U,
            .fastForwardPercent = 800U,
          })),
        "Custom emulation speed settings were rejected") ||
      !check(worker.metrics().speedPercent == 75U &&
          std::abs(worker.metrics().targetFramesPerSecond -
            loadedMetrics.targetFramesPerSecond * 0.75) < 0.000001,
        "Custom normal speed was not applied exactly") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::start, 3U)),
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
  if (!(normalElapsed > 400ms && normalElapsed < 800ms)) {
    std::cerr << "Measured normal cadence: " << normalSeconds << " s, "
              << normalRate << " fps\n";
  }
  if (!check(normalElapsed > 400ms && normalElapsed < 800ms,
        "Normal pacing ran wildly outside the NTSC frame interval") ||
      !check(normalRate > 40.0 && normalRate < 80.0,
        "Measured normal frame rate is outside its integration tolerance") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::pause, 4U)),
        "Timed emulation did not pause")) {
    return 4;
  }

  const auto pausedCount = worker.metrics().pacedFrameCount;
  std::this_thread::sleep_for(80ms);
  if (!check(worker.metrics().pacedFrameCount == pausedCount,
        "Paused emulation continued scheduling frames") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::frameAdvance, 5U)),
        "Paused frame advance failed") ||
      !check(worker.metrics().pacedFrameCount == pausedCount,
        "Frame advance incorrectly restarted the continuous scheduler")) {
    return 5;
  }
  worker.audioFrames()->clear();

  if (!check(submitAndSucceed(
          worker, genplusgx::EmulationCommand::fastForward(6U, true)),
        "Fast-forward mode did not enable") ||
      !check(std::abs(worker.metrics().targetFramesPerSecond -
          loadedMetrics.targetFramesPerSecond * 8.0) < 0.000001 &&
          worker.metrics().speedPercent == 800U &&
          worker.metrics().fastForward && !worker.metrics().slowMotion,
        "Configured fast-forward target is not exactly eight times the core rate") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::resume, 7U)),
        "Fast-forward emulation did not resume")) {
    return 6;
  }

  const auto fastBaseline = worker.metrics().pacedFrameCount;
  const auto fastStart = std::chrono::steady_clock::now();
  constexpr std::uint64_t fastFrames = 60U;
  if (!check(waitForPacedFrames(
          worker, fastBaseline + fastFrames, fastStart + 1s),
        "Fast-forward pacing did not produce 40 frames")) {
    return 7;
  }
  const auto fastElapsed = std::chrono::steady_clock::now() - fastStart;
  const auto fastRate =
    static_cast<double>(fastFrames) /
    std::chrono::duration<double>(fastElapsed).count();
  if (!(fastElapsed > 60ms && fastElapsed < 400ms)) {
    std::cerr << "Measured fast-forward cadence: "
              << std::chrono::duration<double>(fastElapsed).count() << " s, "
              << fastRate << " fps\n";
  }
  if (!check(fastElapsed > 60ms && fastElapsed < 400ms,
        "Fast-forward pacing ran outside its bounded 8x interval") ||
      !check(fastRate > normalRate * 5.0 && fastRate < 700.0,
        "Fast-forward did not materially accelerate without running wild") ||
      !check(worker.audioFrames()->occupancyFrames() == 0U,
        "Fast-forward accumulated host audio that cannot play in real time") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::pause, 8U)),
        "Fast-forward emulation did not pause") ||
      !check(worker.metrics().maximumLatenessMicroseconds < 1'000'000,
        "Pacing instrumentation reported an unbounded late frame")) {
    return 8;
  }

  worker.audioFrames()->clear();
  if (!check(submitAndSucceed(
          worker, genplusgx::EmulationCommand::slowMotion(9U, true)),
        "Slow-motion mode did not enable") ||
      !check(std::abs(worker.metrics().targetFramesPerSecond -
          loadedMetrics.targetFramesPerSecond * 0.25) < 0.000001 &&
          worker.metrics().speedPercent == 25U &&
          worker.metrics().slowMotion && !worker.metrics().fastForward,
        "Configured slow-motion target is not exactly one quarter of the core rate") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::resume, 10U)),
        "Slow-motion emulation did not resume")) {
    return 9;
  }
  const auto slowBaseline = worker.metrics().pacedFrameCount;
  const auto slowStart = std::chrono::steady_clock::now();
  constexpr std::uint64_t slowFrames = 10U;
  if (!check(waitForPacedFrames(
          worker, slowBaseline + slowFrames, slowStart + 2s),
        "Slow-motion pacing did not produce 10 frames")) {
    return 10;
  }
  const auto slowElapsed = std::chrono::steady_clock::now() - slowStart;
  const auto slowRate = static_cast<double>(slowFrames) /
    std::chrono::duration<double>(slowElapsed).count();
  if (!(slowElapsed > 500ms && slowElapsed < 1'200ms)) {
    std::cerr << "Measured slow-motion cadence: "
              << std::chrono::duration<double>(slowElapsed).count() << " s, "
              << slowRate << " fps\n";
  }
  if (!check(slowElapsed > 500ms && slowElapsed < 1'200ms,
        "Slow-motion pacing ran outside its bounded quarter-speed interval") ||
      !check(slowRate > 8.0 && slowRate < normalRate * 0.6,
        "Slow motion did not materially reduce the frame rate") ||
      !check(worker.audioFrames()->occupancyFrames() == 0U,
        "Slow motion accumulated host audio that cannot play in real time") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::pause, 11U)),
        "Slow-motion emulation did not pause") ||
      !check(submitAndSucceed(
          worker, genplusgx::EmulationCommand::slowMotion(12U, false)),
        "Slow-motion mode did not disable")) {
    return 11;
  }

  constexpr std::uint64_t invalidId = 13U;
  if (!check(worker.submit(genplusgx::EmulationCommand::updateSpeedSettings(
          invalidId, {
            .normalPercent = 49U,
            .slowMotionPercent = 25U,
            .fastForwardPercent = 800U,
          })),
        "Invalid speed command could not be submitted for validation")) {
    return 12;
  }
  const auto invalidEvent = waitForOperation(worker, invalidId);
  if (!check(invalidEvent && !invalidEvent->succeeded() &&
          invalidEvent->error == genplusgx::EmulationWorkerError::coreFailure &&
          invalidEvent->coreError == genplusgx::CoreError::invalidTiming &&
          worker.metrics().speedPercent == 75U,
        "Invalid speed settings were not rejected without changing pacing")) {
    return 13;
  }

  if (!check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::unloadGame, 14U)),
        "NTSC timing fixture did not unload") ||
      !check(submitAndSucceed(
          worker, genplusgx::EmulationCommand::load(15U, palFixture.path())),
        "PAL timing fixture did not load") ||
      !check(worker.metrics().targetFramesPerSecond > 37.27 &&
          worker.metrics().targetFramesPerSecond < 37.29 &&
          worker.metrics().speedPercent == 75U &&
          !worker.metrics().fastForward && !worker.metrics().slowMotion,
        "Worker did not propagate custom normal speed to the PAL cadence") ||
      !check(worker.stop(), "Timing worker did not stop cleanly")) {
    return 14;
  }

  return 0;
}
