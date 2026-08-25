#include "genplusgx/emulation_worker.h"
#include "synthetic_rom.h"

#include <chrono>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <thread>
#include <vector>

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
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = worker.waitForEvent(100ms);
    if (event && event->operationId == operationId) {
      return event;
    }
  }
  return std::nullopt;
}

std::optional<genplusgx::EmulationEvent> waitForType(
  genplusgx::EmulationWorker& worker,
  genplusgx::EmulationEventType type)
{
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = worker.waitForEvent(100ms);
    if (event && event->type == type) {
      return event;
    }
  }
  return std::nullopt;
}

bool submitAndSucceed(
  genplusgx::EmulationWorker& worker,
  genplusgx::EmulationCommand command,
  genplusgx::EmulationEvent& event)
{
  const auto operationId = command.operationId;
  if (!worker.submit(std::move(command))) {
    return false;
  }
  auto result = waitForOperation(worker, operationId);
  if (!result || !result->succeeded()) {
    return false;
  }
  event = std::move(*result);
  return true;
}

} // namespace

int main()
{
  const genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".bin"};
  const genplusgx::test::TemporaryFixture invalidFixture{{}, ".bin"};
  genplusgx::EmulationWorker worker;
  const auto callerThread = std::this_thread::get_id();

  if (!check(worker.state() == genplusgx::EmulationWorkerState::stopped,
        "New worker did not start stopped") ||
      !check(worker.submit(genplusgx::EmulationCommand::simple(
          genplusgx::EmulationCommandType::pause, 1U)).error ==
          genplusgx::EmulationWorkerError::notRunning,
        "Stopped worker accepted a command") ||
      !check(worker.submit(genplusgx::EmulationCommand::simple(
          genplusgx::EmulationCommandType::pause, 0U)).error ==
          genplusgx::EmulationWorkerError::invalidCommand,
        "Zero operation ID was accepted") ||
      !check(worker.start(), "Worker start failed") ||
      !check(worker.start().error == genplusgx::EmulationWorkerError::alreadyRunning,
        "Repeated worker start was accepted")) {
    return 1;
  }

  const auto started = waitForType(worker, genplusgx::EmulationEventType::workerStarted);
  if (!check(started && started->workerState == genplusgx::EmulationWorkerState::idle,
        "Worker did not publish its idle start") ||
      !check(started->workerThreadId != callerThread,
        "Core worker started on the caller thread")) {
    return 2;
  }

  if (!check(worker.submit(genplusgx::EmulationCommand::simple(
          genplusgx::EmulationCommandType::resume, 2U)),
        "Invalid-transition command could not be queued")) {
    return 3;
  }
  const auto invalidResume = waitForOperation(worker, 2U);
  if (!check(invalidResume && invalidResume->type ==
          genplusgx::EmulationEventType::commandFailed &&
          invalidResume->error == genplusgx::EmulationWorkerError::invalidTransition &&
          invalidResume->workerState == genplusgx::EmulationWorkerState::idle,
        "Idle resume did not produce a typed asynchronous failure")) {
    return 4;
  }

  genplusgx::EmulationEvent event;
  if (!check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::load(3U, fixture.path()), event),
        "Worker game load failed") ||
      !check(event.workerState == genplusgx::EmulationWorkerState::paused &&
          event.workerThreadId == started->workerThreadId &&
          event.hardware == 0x80U,
        "Loaded worker state or ownership was incorrect") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::start, 4U), event),
        "Worker emulation start failed") ||
      !check(event.workerState == genplusgx::EmulationWorkerState::running,
        "Worker did not enter running state")) {
    return 5;
  }

  auto runningFrame = waitForType(
    worker, genplusgx::EmulationEventType::frameCompleted);
  while (runningFrame && runningFrame->frameNumber < 2U) {
    runningFrame = waitForType(
      worker, genplusgx::EmulationEventType::frameCompleted);
  }
  std::vector<std::uint16_t> videoPixels(
    genplusgx::VideoFrameExchange::maximumSurfacePixels, 0U);
  genplusgx::CoreVideoFrameInfo videoInfo;
  std::uint64_t videoGeneration = 0;
  if (!check(runningFrame && runningFrame->frameNumber >= 1U,
        "Running worker did not publish a frame") ||
      !check(worker.audioFrames()->occupancyFrames() > 0U,
        "Worker did not transfer core audio into its bounded output ring") ||
      !check(worker.videoFrames()->copyLatest(
          videoPixels, videoInfo, videoGeneration),
        "Worker frame was not transferred through the bounded exchange") ||
      !check(videoInfo.width == 320U && videoInfo.height == 224U &&
          videoGeneration >= runningFrame->videoGeneration &&
          genplusgx::hashVideoFrame(
            std::span<const std::uint16_t>{videoPixels}.first(videoInfo.pixelCount())) ==
            0x0cfd2d0b9af92325ULL,
        "Worker exchange changed the deterministic core framebuffer") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::pause, 5U), event),
        "Worker pause failed") ||
      !check(event.workerState == genplusgx::EmulationWorkerState::paused,
        "Worker did not enter paused state")) {
    return 6;
  }

  genplusgx::CoreVideoSettings overscanSettings;
  overscanSettings.overscan = genplusgx::CoreOverscanMode::full;
  overscanSettings.ntscFilter = genplusgx::CoreNtscFilter::composite;
  if (!check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::updateVideoSettings(
            51U, overscanSettings), event),
        "Worker video settings update failed") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::frameAdvance, 52U), event),
        "Worker settings propagation frame failed") ||
      !check(worker.videoFrames()->copyLatest(
          videoPixels, videoInfo, videoGeneration),
        "Worker settings frame was not published") ||
      !check(videoInfo.width > 320U && videoInfo.height > 224U,
        "Worker did not propagate overscan settings to the core") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::updateVideoSettings(
            53U, genplusgx::CoreVideoSettings{}), event),
        "Worker could not restore default video settings") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::frameAdvance, 54U), event),
        "Default settings propagation frame failed")) {
    return 7;
  }

  genplusgx::CoreAudioSettings silentAudio;
  silentAudio.filter = genplusgx::CoreAudioFilter::disabled;
  silentAudio.psgLevelPercent = 0;
  if (!check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::updateAudioSettings(55U, silentAudio), event),
        "Worker audio settings update failed") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::hardReset, 56U), event),
        "Worker audio settings reset failed") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::frameAdvance, 57U), event),
        "Worker audio settings propagation frame failed")) {
    return 7;
  }
  const auto audioFrameCount = worker.audioFrames()->occupancyFrames();
  std::vector<genplusgx::StereoAudioFrame> mutedFrames(audioFrameCount);
  const auto mutedRead = worker.audioFrames()->read(mutedFrames);
  if (!check(mutedRead.providedFrames == audioFrameCount && audioFrameCount > 0U &&
        genplusgx::hashAudioFrames(mutedFrames) != 0x3FDB01D7287AE391ULL,
        "Worker did not propagate core mixer settings at a frame boundary") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::updateAudioSettings(
            58U, genplusgx::CoreAudioSettings{}), event),
        "Worker could not restore default audio settings")) {
    return 7;
  }
  genplusgx::CoreSystemSettings nextLoadSystem;
  nextLoadSystem.region = genplusgx::CoreSystemRegion::palEurope;
  nextLoadSystem.videoStandard = genplusgx::CoreVideoStandard::pal;
  if (!check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::updateSystemSettings(
            59U, nextLoadSystem), event),
        "Worker system settings update failed") ||
      !check(event.hardware == 0x80U,
        "Reload-required system settings changed the active hardware") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::updateSystemSettings(
            60U, genplusgx::CoreSystemSettings{}), event),
        "Worker could not restore automatic system settings")) {
    return 7;
  }

  genplusgx::InputSnapshot input;
  input.sequence = 22U;
  input.players[0].connected = true;
  input.players[0].buttons = genplusgx::buttonMask(genplusgx::InputButton::b);
  if (!check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::updateInput(6U, input), event),
        "Worker input update failed") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::frameAdvance, 7U), event),
        "Worker frame advance failed") ||
      !check(event.workerState == genplusgx::EmulationWorkerState::paused &&
          event.appliedInputSequence == 22U,
        "Frame advance did not consume input at its frame boundary")) {
    return 8;
  }

  if (!check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::captureState, 8U), event),
        "Worker state capture failed") ||
      !check(event.type == genplusgx::EmulationEventType::stateCaptured &&
          event.rawState.size() > 16U,
        "Worker did not return a raw state payload")) {
    return 9;
  }
  const auto capturedState = event.rawState;

  if (!check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::frameAdvance, 9U), event),
        "Mutation frame advance failed") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::restore(10U, capturedState), event),
        "Worker state restore failed") ||
      !check(event.frameNumber == 0U,
        "Worker state restore did not reset frontend frame generation") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::hardReset, 11U), event),
        "Worker hard reset failed") ||
      !check(event.frameNumber == 0U, "Hard reset retained a frame count") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::frameAdvance, 12U), event),
        "Post-reset frame advance failed") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::softReset, 13U), event),
        "Worker soft reset failed") ||
      !check(event.frameNumber == 0U, "Soft reset retained a frame count")) {
    return 10;
  }

  if (!check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::fastForward(14U, true), event),
        "Fast-forward enable failed") ||
      !check(event.fastForward, "Fast-forward state was not reported") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::resume, 15U), event),
        "Fast-forward resume failed")) {
    return 11;
  }
  const auto fastFrame = waitForType(worker, genplusgx::EmulationEventType::frameCompleted);
  std::this_thread::sleep_for(20ms);
  if (!check(fastFrame && fastFrame->fastForward,
        "Fast-forward frame was not marked") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::pause, 16U), event),
        "Fast-forward pause failed") ||
      !check(worker.metrics().replacedFrameEvents > 0U &&
          worker.metrics().eventQueueDepth <= 65U,
        "Frame notifications were not coalesced within bounded event storage") ||
      !check(worker.submit(genplusgx::EmulationCommand::load(
          17U, invalidFixture.path())),
        "Invalid replacement load could not be queued")) {
    return 12;
  }
  const auto invalidLoad = waitForOperation(worker, 17U);
  if (!check(invalidLoad && invalidLoad->type ==
          genplusgx::EmulationEventType::commandFailed &&
          invalidLoad->error == genplusgx::EmulationWorkerError::coreFailure &&
          invalidLoad->coreError == genplusgx::CoreError::loadFailed &&
          invalidLoad->workerState == genplusgx::EmulationWorkerState::idle,
        "Failed replacement load retained a half-loaded worker state") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::load(18U, fixture.path()), event),
        "Worker did not recover from a failed replacement load") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::unloadGame, 19U), event),
        "Worker unload failed") ||
      !check(event.workerState == genplusgx::EmulationWorkerState::idle,
        "Worker unload did not return to idle")) {
    return 13;
  }

  if (!check(worker.stop(), "Worker stop failed") ||
      !check(worker.state() == genplusgx::EmulationWorkerState::stopped,
        "Worker did not synchronously stop") ||
      !check(worker.submit(genplusgx::EmulationCommand::simple(
          genplusgx::EmulationCommandType::pause, 20U)).error ==
          genplusgx::EmulationWorkerError::notRunning,
        "Stopped worker retained command acceptance")) {
    return 14;
  }
  const auto stopped = waitForType(worker, genplusgx::EmulationEventType::workerStopped);
  if (!check(stopped && stopped->succeeded(), "Worker did not publish clean shutdown")) {
    return 15;
  }

  for (int cycle = 0; cycle < 10; ++cycle) {
    if (!check(worker.start(), "Repeated worker start failed") ||
        !check(waitForType(worker, genplusgx::EmulationEventType::workerStarted).has_value(),
          "Repeated worker start event was missing") ||
        !check(worker.stop(), "Repeated worker stop failed") ||
        !check(worker.state() == genplusgx::EmulationWorkerState::stopped,
          "Repeated worker stop left a live state")) {
      return 16;
    }
  }

  {
    genplusgx::EmulationWorker automaticShutdown;
    if (!check(automaticShutdown.start(), "RAII worker start failed") ||
        !check(waitForType(automaticShutdown,
          genplusgx::EmulationEventType::workerStarted).has_value(),
          "RAII worker start event was missing") ||
        !check(automaticShutdown.submit(
          genplusgx::EmulationCommand::load(21U, fixture.path())),
          "RAII worker load could not be queued") ||
        !check(waitForOperation(automaticShutdown, 21U).has_value(),
          "RAII worker load did not complete")) {
      return 17;
    }
  }

  genplusgx::CoreAdapter leaseProbe;
  return check(leaseProbe.initialize(), "Worker destruction retained the global core lease") &&
      check(leaseProbe.shutdown(), "Core lease probe shutdown failed")
    ? 0
    : 18;
}
