#include "genplusgx/emulation_worker.h"
#include "synthetic_rom.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

namespace {

using namespace std::chrono_literals;

bool check(bool condition, const std::string& message)
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
    auto event = worker.waitForEvent(100ms);
    if (event && event->operationId == operationId) {
      return event;
    }
  }
  return std::nullopt;
}

std::optional<genplusgx::EmulationEvent> waitForFrameAtLeast(
  genplusgx::EmulationWorker& worker,
  std::uint64_t frame)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = worker.waitForEvent(100ms);
    if (event && event->type == genplusgx::EmulationEventType::frameCompleted &&
        event->frameNumber >= frame) {
      return event;
    }
  }
  return std::nullopt;
}

std::optional<genplusgx::EmulationEvent> waitForEarlierFrame(
  genplusgx::EmulationWorker& worker,
  std::uint64_t frame)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = worker.waitForEvent(100ms);
    if (event && event->type == genplusgx::EmulationEventType::frameCompleted &&
        event->rewinding && event->frameNumber < frame) {
      return event;
    }
  }
  return std::nullopt;
}

bool submitAndSucceed(
  genplusgx::EmulationWorker& worker,
  genplusgx::EmulationCommand command,
  genplusgx::EmulationEvent& output)
{
  const auto operationId = command.operationId;
  if (!worker.submit(std::move(command))) {
    return false;
  }
  auto event = waitForOperation(worker, operationId);
  if (!event || !event->succeeded()) {
    return false;
  }
  output = std::move(*event);
  return true;
}

} // namespace

int main()
{
  using namespace genplusgx;
  constexpr std::size_t mebibyte = 1024U * 1024U;
  const test::TemporaryFixture fixture{
    test::makeGenesisRamMarkerRom(), ".bin"};
  EmulationWorker worker;
  EmulationEvent event;
  if (!check(worker.start(), "Rewind worker did not start")) {
    return EXIT_FAILURE;
  }
  const auto startup = worker.waitForEvent(3s);
  if (!check(startup && startup->type == EmulationEventType::workerStarted,
        "Rewind worker did not publish startup completion")) {
    static_cast<void>(worker.stop());
    return EXIT_FAILURE;
  }

  const RewindConfiguration settings{
    .enabled = true,
    .captureIntervalFrames = 1U,
    .memoryLimitBytes = 16U * mebibyte,
  };
  if (!check(submitAndSucceed(worker,
        EmulationCommand::updateRewindSettings(2U, settings), event),
        "Rewind configuration command failed") ||
      !check(submitAndSucceed(worker, EmulationCommand::load(3U, fixture.path()), event),
        "Rewind fixture did not load") ||
      !check(!event.rewindAvailable,
        "A newly loaded frame-zero state claimed an earlier rewind point") ||
      !check(submitAndSucceed(worker,
        EmulationCommand::simple(EmulationCommandType::resume, 4U), event),
        "Rewind fixture did not start")) {
    static_cast<void>(worker.stop());
    return EXIT_FAILURE;
  }

  const auto forward = waitForFrameAtLeast(worker, 10U);
  const auto beforeMetrics = worker.metrics();
  if (!check(forward && forward->rewindAvailable,
        "Running frames never exposed rewind availability") ||
      !check(beforeMetrics.rewindSnapshotCount >= 10U &&
        beforeMetrics.rewindPayloadBytes > 0U &&
        beforeMetrics.rewindPayloadBytes <= beforeMetrics.rewindMemoryLimitBytes,
        "Worker rewind history was missing or exceeded its byte cap") ||
      !check(submitAndSucceed(worker,
        EmulationCommand::rewinding(5U, true), event),
        "Rewind hold could not start") ||
      !check(event.rewinding && !event.fastForward,
        "Rewind command did not report mutually exclusive runtime state")) {
    static_cast<void>(worker.stop());
    return EXIT_FAILURE;
  }

  const auto earlier = waitForEarlierFrame(worker, forward->frameNumber);
  if (!check(earlier && earlier->rewinding &&
        earlier->frameNumber < forward->frameNumber,
        "The first rewind frame did not move to an earlier emulated frame") ||
      !check(worker.audioFrames()->occupancyFrames() == 0U,
        "Rewind left stale or newly generated audio in the playback ring") ||
      !check(submitAndSucceed(worker,
        EmulationCommand::rewinding(6U, false), event),
        "Rewind hold could not stop") ||
      !check(!event.rewinding,
        "Rewind release did not restore forward runtime state")) {
    static_cast<void>(worker.stop());
    return EXIT_FAILURE;
  }

  const auto resumed = waitForFrameAtLeast(worker, earlier->frameNumber + 2U);
  if (!check(resumed && !resumed->rewinding,
        "Forward execution did not resume after rewind release")) {
    static_cast<void>(worker.stop());
    return EXIT_FAILURE;
  }

  CoreAudioSettings changedAudio;
  changedAudio.fmLevelPercent = 80;
  if (!check(submitAndSucceed(worker,
        EmulationCommand::updateAudioSettings(7U, changedAudio), event),
        "State-changing audio settings were rejected") ||
      !check(!event.rewindAvailable,
        "State-changing audio settings did not invalidate old rewind history")) {
    static_cast<void>(worker.stop());
    return EXIT_FAILURE;
  }

  const auto rebuilt = waitForFrameAtLeast(worker, event.frameNumber + 2U);
  if (!check(rebuilt && rebuilt->rewindAvailable,
        "Rewind history did not rebuild after the audio setting change") ||
      !check(submitAndSucceed(worker,
        EmulationCommand::simple(EmulationCommandType::pause, 8U), event),
        "Rewind fixture could not pause for a debugger write")) {
    static_cast<void>(worker.stop());
    return EXIT_FAILURE;
  }

  CoreDebugRequest writeMemory;
  writeMemory.type = CoreDebugRequestType::writeMemory;
  writeMemory.region = CoreDebugMemoryRegion::m68kRam;
  writeMemory.offset = 0x20U;
  writeMemory.bytes = {0x5AU};
  if (!check(submitAndSucceed(worker,
        EmulationCommand::debug(9U, std::move(writeMemory)), event),
        "Debugger memory write was rejected") ||
      !check(!event.rewindAvailable &&
        worker.metrics().rewindSnapshotCount == 1U,
        "Debugger memory write did not invalidate old rewind history") ||
      !check(submitAndSucceed(worker,
        EmulationCommand::simple(EmulationCommandType::resume, 10U), event),
        "Rewind fixture did not resume after its debugger write") ||
      !check(waitForFrameAtLeast(worker, event.frameNumber + 2U).has_value(),
        "Rewind history did not rebuild after the debugger write") ||
      !check(submitAndSucceed(worker,
        EmulationCommand::updateRewindSettings(11U, {
          .enabled = false,
          .captureIntervalFrames = 6U,
          .memoryLimitBytes = 16U * mebibyte,
        }), event), "Rewind disable command failed") ||
      !check(worker.metrics().rewindSnapshotCount == 0U &&
        !worker.metrics().rewindAvailable,
        "Disabling rewind did not release its bounded history")) {
    static_cast<void>(worker.stop());
    return EXIT_FAILURE;
  }

  if (!check(worker.submit(EmulationCommand::rewinding(12U, true)),
        "Disabled rewind request could not be queued for rejection")) {
    static_cast<void>(worker.stop());
    return EXIT_FAILURE;
  }
  const auto rejected = waitForOperation(worker, 12U);
  if (!check(rejected && !rejected->succeeded() &&
        rejected->type == EmulationEventType::commandFailed &&
        rejected->message.find("disabled") != std::string::npos,
        "Disabled rewind did not fail with a descriptive result") ||
      !check(worker.stop(), "Rewind worker did not stop cleanly")) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
