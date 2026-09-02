#include "genplusgx/emulation_worker.h"
#include "synthetic_rom.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

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
  const auto deadline = std::chrono::steady_clock::now() + 4s;
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

bool waitForFrame(genplusgx::EmulationWorker& worker, std::uint64_t minimum)
{
  const auto deadline = std::chrono::steady_clock::now() + 4s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = worker.waitForEvent(50ms);
    if (event && event->type == genplusgx::EmulationEventType::frameCompleted &&
        event->frameNumber >= minimum) {
      return true;
    }
  }
  return false;
}

} // namespace

int main()
{
  using namespace genplusgx;
  using namespace genplusgx::netplay;
  const test::TemporaryFixture fixture{
    test::makeGenesisRamMarkerRom(), ".bin"};
  auto bridge = std::make_shared<NetplayBridge>(64U);
  EmulationWorker worker{64U, 64U, 48'000, {}, {}, {}, {}, bridge};
  if (!check(worker.start(), "Netplay worker did not start")) {
    return 1;
  }
  static_cast<void>(worker.waitForEvent(2s));
  if (!check(worker.submit(EmulationCommand::load(1U, fixture.path())),
        "Synthetic game load was not queued")) {
    return 1;
  }
  const auto loaded = waitForOperation(worker, 1U);
  if (!check(loaded && loaded->succeeded(), "Synthetic game did not load")) {
    return 1;
  }

  InputSnapshot local;
  local.players[0] = {
    .connected = true,
    .buttons = buttonMask(InputButton::a),
  };
  if (!check(worker.submit(EmulationCommand::updateInput(2U, local)),
      "Local netplay input was not queued")) {
    return 1;
  }
  const auto inputAccepted = waitForOperation(worker, 2U);
  if (!check(inputAccepted && inputAccepted->succeeded(),
      "Local netplay input was not accepted")) {
    return 1;
  }
  const NetplayConfiguration configuration{
    .role = NetplayRole::host,
    .localPlayer = 0U,
    .remotePlayer = 1U,
    .inputDelayFrames = 2U,
    .rollbackFrames = 8U,
  };
  auto invalidConfiguration = configuration;
  invalidConfiguration.rollbackFrames = 0U;
  if (!check(worker.submit(
        EmulationCommand::startNetplaySession(3U, invalidConfiguration)),
      "Invalid netplay configuration was not queued")) {
    return 1;
  }
  const auto invalidConfigurationResult = waitForOperation(worker, 3U);
  if (!check(invalidConfigurationResult &&
        !invalidConfigurationResult->succeeded() &&
        invalidConfigurationResult->workerState ==
          EmulationWorkerState::paused,
      "Invalid atomic netplay startup did not fail without running")) {
    return 1;
  }
  if (!check(worker.submit(
        EmulationCommand::startNetplaySession(4U, configuration)),
      "Netplay configuration was not queued")) {
    return 1;
  }
  const auto configured = waitForOperation(worker, 4U);
  if (!check(configured && configured->succeeded() &&
        configured->type == EmulationEventType::netplayStarted &&
        configured->workerState == EmulationWorkerState::running,
      "Atomic netplay reset/start did not activate") ||
      !check(waitForFrame(worker, 4U),
        "Netplay emulation did not execute frames")) {
    return 1;
  }

  auto outgoing = bridge->pollOutgoing();
  if (!check(outgoing && outgoing->frameNumber >= 2U,
      "The worker did not publish delayed local input")) {
    return 1;
  }
  NetplayInputFrame correction{
    .frameNumber = 0U,
    .state = {.connected = true, .buttons = buttonMask(InputButton::b)},
  };
  if (!check(worker.submit(
        EmulationCommand::remoteNetplayFrame(5U, correction)),
      "A late peer input was not queued")) {
    return 1;
  }
  const auto remoteAccepted = waitForOperation(worker, 5U);
  if (!check(remoteAccepted && remoteAccepted->succeeded(),
      "A correctable late peer input was rejected")) {
    return 1;
  }
  const auto rollbackDeadline = std::chrono::steady_clock::now() + 3s;
  while (worker.metrics().netplayRollbacks == 0U &&
         std::chrono::steady_clock::now() < rollbackDeadline) {
    const auto event = worker.waitForEvent(50ms);
    if (event && !event->succeeded()) {
      std::cerr << "netplay worker event failure: " << event->message << '\n';
    }
  }
  const auto metrics = worker.metrics();
  if (metrics.netplayRollbacks == 0U) {
    std::cerr << "netplay metrics: active=" << metrics.netplayActive
              << " predicted=" << metrics.netplayPredictedFrames
              << " requests=" << metrics.netplayRollbackRequests
              << " history=" << metrics.netplayHistoryFrames
              << " bytes=" << metrics.netplayHistoryBytes
              << " worker=" << static_cast<int>(worker.state()) << '\n';
  }
  if (!check(metrics.netplayActive && metrics.netplayRollbacks > 0U &&
        metrics.netplayHistoryFrames <= 9U &&
        metrics.netplayHistoryBytes <= 64U * 1024U * 1024U,
      "The bounded rollback worker did not correct the prediction")) {
    return 1;
  }

  RunAheadConfiguration runAhead{.enabled = true, .frames = 1U};
  if (!check(worker.submit(
        EmulationCommand::updateRunAheadSettings(6U, runAhead)),
      "The deterministic-operation guard command was not queued")) {
    return 1;
  }
  const auto blocked = waitForOperation(worker, 6U);
  if (!check(blocked && !blocked->succeeded(),
      "Run-ahead was incorrectly mutable during netplay")) {
    return 1;
  }
  if (!check(worker.submit(EmulationCommand::simple(
        EmulationCommandType::stopNetplay, 7U)),
      "Netplay stop was not queued")) {
    return 1;
  }
  const auto stopped = waitForOperation(worker, 7U);
  if (!check(stopped && stopped->succeeded() &&
        stopped->type == EmulationEventType::netplayStopped,
      "Netplay did not stop cleanly")) {
    return 1;
  }
  if (!check(worker.submit(EmulationCommand::simple(
        EmulationCommandType::pause, 8U)),
      "Post-netplay pause was not queued")) {
    return 1;
  }
  const auto paused = waitForOperation(worker, 8U);
  if (!check(paused && paused->succeeded(),
      "Post-netplay pause did not complete") ||
      !check(worker.submit(EmulationCommand::simple(
        EmulationCommandType::captureState, 9U)),
      "Post-netplay checkpoint was not queued")) {
    return 1;
  }
  const auto checkpoint = waitForOperation(worker, 9U);
  if (!check(checkpoint && checkpoint->succeeded() &&
        checkpoint->type == EmulationEventType::stateCaptured &&
        !checkpoint->rawState.empty(),
      "Post-netplay shutdown checkpoint could not be captured") ||
      !check(worker.stop(), "Worker did not shut down after netplay")) {
    return 1;
  }

  auto constrainedBridge = std::make_shared<NetplayBridge>(1U);
  EmulationWorker constrainedWorker{
    32U, 32U, 48'000, {}, {}, {}, {}, constrainedBridge};
  if (!check(constrainedWorker.start(),
        "Constrained netplay worker did not start")) {
    return 1;
  }
  static_cast<void>(constrainedWorker.waitForEvent(2s));
  if (!check(submitAndSucceed(constrainedWorker,
        EmulationCommand::load(20U, fixture.path())),
      "Constrained netplay game did not load") ||
      !check(submitAndSucceed(constrainedWorker,
        EmulationCommand::startNetplaySession(21U, configuration)),
      "Constrained atomic netplay startup failed")) {
    return 1;
  }
  std::optional<EmulationEvent> runtimeFailure;
  const auto failureDeadline = std::chrono::steady_clock::now() + 3s;
  while (!runtimeFailure && std::chrono::steady_clock::now() < failureDeadline) {
    auto event = constrainedWorker.waitForEvent(50ms);
    if (event && !event->succeeded()) {
      runtimeFailure = std::move(event);
    }
  }
  if (!check(runtimeFailure && runtimeFailure->netplayActive &&
        !runtimeFailure->command &&
        constrainedWorker.state() == EmulationWorkerState::paused,
      "A bounded netplay runtime failure was not surfaced for peer teardown") ||
      !check(submitAndSucceed(constrainedWorker, EmulationCommand::simple(
        EmulationCommandType::stopNetplay, 23U)),
      "Constrained netplay stop failed") ||
      !check(constrainedWorker.stop(),
      "Constrained netplay worker did not shut down")) {
    return 1;
  }
  return 0;
}
