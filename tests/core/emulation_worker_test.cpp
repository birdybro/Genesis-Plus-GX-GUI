#include "genplusgx/emulation_worker.h"
#include "synthetic_rom.h"

#include <chrono>
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
          event.workerThreadId == started->workerThreadId,
        "Loaded worker state or ownership was incorrect") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::start, 4U), event),
        "Worker emulation start failed") ||
      !check(event.workerState == genplusgx::EmulationWorkerState::running,
        "Worker did not enter running state")) {
    return 5;
  }

  const auto runningFrame = waitForType(
    worker, genplusgx::EmulationEventType::frameCompleted);
  if (!check(runningFrame && runningFrame->frameNumber >= 1U,
        "Running worker did not publish a frame") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::pause, 5U), event),
        "Worker pause failed") ||
      !check(event.workerState == genplusgx::EmulationWorkerState::paused,
        "Worker did not enter paused state")) {
    return 6;
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
    return 7;
  }

  if (!check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::captureState, 8U), event),
        "Worker state capture failed") ||
      !check(event.type == genplusgx::EmulationEventType::stateCaptured &&
          event.rawState.size() > 16U,
        "Worker did not return a raw state payload")) {
    return 8;
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
    return 9;
  }

  if (!check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::fastForward(14U, true), event),
        "Fast-forward enable failed") ||
      !check(event.fastForward, "Fast-forward state was not reported") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::resume, 15U), event),
        "Fast-forward resume failed")) {
    return 10;
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
    return 11;
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
    return 12;
  }

  if (!check(worker.stop(), "Worker stop failed") ||
      !check(worker.state() == genplusgx::EmulationWorkerState::stopped,
        "Worker did not synchronously stop") ||
      !check(worker.submit(genplusgx::EmulationCommand::simple(
          genplusgx::EmulationCommandType::pause, 20U)).error ==
          genplusgx::EmulationWorkerError::notRunning,
        "Stopped worker retained command acceptance")) {
    return 13;
  }
  const auto stopped = waitForType(worker, genplusgx::EmulationEventType::workerStopped);
  if (!check(stopped && stopped->succeeded(), "Worker did not publish clean shutdown")) {
    return 14;
  }

  for (int cycle = 0; cycle < 10; ++cycle) {
    if (!check(worker.start(), "Repeated worker start failed") ||
        !check(waitForType(worker, genplusgx::EmulationEventType::workerStarted).has_value(),
          "Repeated worker start event was missing") ||
        !check(worker.stop(), "Repeated worker stop failed") ||
        !check(worker.state() == genplusgx::EmulationWorkerState::stopped,
          "Repeated worker stop left a live state")) {
      return 15;
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
      return 16;
    }
  }

  genplusgx::CoreAdapter leaseProbe;
  return check(leaseProbe.initialize(), "Worker destruction retained the global core lease") &&
      check(leaseProbe.shutdown(), "Core lease probe shutdown failed")
    ? 0
    : 17;
}
