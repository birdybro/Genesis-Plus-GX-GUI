#include "genplusgx/netplay/netplay_bridge.h"
#include "genplusgx/netplay/netplay_timeline.h"

#include <iostream>
#include <limits>
#include <string>

namespace {

bool check(bool condition, const std::string& message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

genplusgx::InputDeviceState state(genplusgx::InputButton button)
{
  return {
    .connected = true,
    .buttons = genplusgx::buttonMask(button),
  };
}

} // namespace

int main()
{
  using namespace genplusgx;
  using namespace genplusgx::netplay;

  const NetplaySessionDescriptor descriptor{
    .gameSha256 = std::string(64U, 'a'),
    .settingsSha256 = std::string(64U, 'b'),
    .coreVersion = "test-core",
  };
  if (!check(descriptor.valid(), "A canonical session descriptor was rejected") ||
      !check(!NetplaySessionDescriptor{
          .gameSha256 = "A", .settingsSha256 = {}, .coreVersion = {}}.valid(),
        "An invalid session descriptor was accepted")) {
    return 1;
  }

  NetplayTimeline timeline;
  const NetplayConfiguration configuration{
    .role = NetplayRole::host,
    .localPlayer = 0U,
    .remotePlayer = 1U,
    .inputDelayFrames = 2U,
    .rollbackFrames = 4U,
  };
  if (!check(timeline.configure(configuration), "Timeline configuration failed")) {
    return 1;
  }
  InputSnapshot local;
  local.players[0] = state(InputButton::a);
  auto frame0 = timeline.prepareFrame(0U, local);
  if (!check(frame0.outgoing.frameNumber == 2U,
        "Local input was not stamped with the configured delay") ||
      !check(frame0.combined.players[0] == InputDeviceState{},
        "Startup-delay input was not neutral") ||
      !check(frame0.remotePredicted,
        "Missing remote input was not marked predicted")) {
    return 1;
  }

  if (!check(timeline.submitRemote({.frameNumber = 0U,
        .state = state(InputButton::b)}, 1U),
      "Late remote input inside the rollback window was rejected")) {
    return 1;
  }
  const auto rollback = timeline.takeRollbackRequest();
  if (!check(rollback && *rollback == 0U,
        "A changed prediction did not request rollback")) {
    return 1;
  }
  timeline.discardFrom(0U);
  const auto replay = timeline.replayInput(0U);
  if (!check(hasButton(replay.players[1].buttons, InputButton::b),
        "Rollback replay did not consume authoritative remote input")) {
    return 1;
  }
  const auto duplicate = timeline.submitRemote({.frameNumber = 0U,
    .state = state(InputButton::c)}, 1U);
  if (!check(duplicate.error == NetplayTimelineError::duplicateConflict,
        "Conflicting duplicate remote input was accepted")) {
    return 1;
  }
  const auto tooFar = timeline.submitRemote(
    {.frameNumber = 100U, .state = {}}, 1U);
  if (!check(tooFar.error == NetplayTimelineError::frameTooFarAhead,
        "An implausible future input frame was accepted")) {
    return 1;
  }

  for (std::uint64_t frame = 1U; frame < 200U; ++frame) {
    static_cast<void>(timeline.prepareFrame(frame, local));
    static_cast<void>(timeline.submitRemote(
      {.frameNumber = frame, .state = {}}, frame + 1U));
    timeline.prune(frame + 1U);
  }
  const auto metrics = timeline.metrics();
  if (metrics.storedLocalFrames > 10U || metrics.storedRemoteFrames > 9U ||
      metrics.usedFrames > 9U) {
    std::cerr << "timeline sizes: local=" << metrics.storedLocalFrames
              << " remote=" << metrics.storedRemoteFrames
              << " used=" << metrics.usedFrames << '\n';
  }
  if (!check(metrics.storedLocalFrames <= 10U &&
        metrics.storedRemoteFrames <= 9U && metrics.usedFrames <= 9U,
      "The timeline histories grew beyond their bounded retention")) {
    return 1;
  }

  NetplayTimeline boundary;
  constexpr auto maximumFrame = std::numeric_limits<std::uint64_t>::max();
  if (!check(boundary.configure(configuration, maximumFrame - 2U),
        "Boundary timeline configuration failed") ||
      !check(boundary.submitRemote(
          {.frameNumber = maximumFrame, .state = {}}, maximumFrame - 1U),
        "A valid boundary frame was rejected after an integer overflow") ||
      !check(boundary.submitRemote(
          {.frameNumber = maximumFrame - 3U, .state = {}}, maximumFrame - 1U)
          .error == NetplayTimelineError::frameTooOld,
        "An input predating the session start was accepted")) {
    return 1;
  }
  const auto boundaryFrame = boundary.prepareFrame(maximumFrame, local);
  if (!check(boundaryFrame.outgoing.frameNumber == maximumFrame &&
        boundaryFrame.combined.sequence == maximumFrame,
      "Boundary frame arithmetic wrapped around")) {
    return 1;
  }

  NetplayBridge bridge{2U};
  if (!check(bridge.submitOutgoing({.frameNumber = 1U, .state = {}}) &&
        bridge.submitOutgoing({.frameNumber = 2U, .state = {}}),
      "The bridge rejected frames below capacity") ||
      !check(bridge.submitOutgoing({.frameNumber = 3U, .state = {}}).error ==
        NetplayBridgeError::queueFull,
      "The bridge did not reject an overflowing frame") ||
      !check(bridge.pollOutgoing()->frameNumber == 1U &&
        bridge.pollOutgoing()->frameNumber == 2U && !bridge.pollOutgoing(),
      "The bridge did not preserve bounded FIFO order")) {
    return 1;
  }
  return 0;
}
