#include "genplusgx/state_storage_service.h"

#include "synthetic_rom.h"

#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

using namespace std::chrono_literals;

namespace {

bool check(bool condition, std::string_view message)
{
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

std::vector<std::uint8_t> fakeRawState(std::uint8_t marker)
{
  constexpr std::string_view version = "GENPLUS-GX 1.7.6";
  std::vector<std::uint8_t> state(512U, marker);
  std::ranges::copy(version, state.begin());
  return state;
}

std::optional<genplusgx::StateStorageEvent> waitForOperation(
  genplusgx::StateStorageService& service,
  std::uint64_t operationId)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = service.waitForEvent(100ms);
    if (event && event->operationId == operationId) {
      return event;
    }
  }
  return std::nullopt;
}

} // namespace

int main()
{
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "temporary state root creation failed")) {
    return 1;
  }
  const genplusgx::ApplicationPaths paths{
    std::filesystem::path{temporary.path().toStdString()} / "app"};
  const genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  genplusgx::StateStorageService service{paths, 16U, 16U};
  if (!check(service.start(), "storage service start failed")) {
    return 2;
  }
  const auto started = service.waitForEvent(2s);
  if (!check(started &&
        started->type == genplusgx::StateStorageEventType::serviceStarted &&
        service.state() == genplusgx::StateStorageServiceState::running,
      "storage service did not publish a running event")) {
    return 3;
  }

  if (!check(service.submit(genplusgx::StateStorageCommand::simple(
        genplusgx::StateStorageCommandType::loadSlot, 0U, 1U)).error ==
        genplusgx::StateStorageError::invalidCommand,
      "zero operation ID was accepted") ||
      !check(service.submit(genplusgx::StateStorageCommand::simple(
        genplusgx::StateStorageCommandType::loadSlot, 1U, 0U)).error ==
        genplusgx::StateStorageError::invalidCommand,
      "zero game generation was accepted") ||
      !check(service.submit(genplusgx::StateStorageCommand::simple(
        genplusgx::StateStorageCommandType::loadSlot, 1U, 1U, 10U)).error ==
        genplusgx::StateStorageError::invalidCommand,
      "out-of-range slot was accepted")) {
    return 4;
  }

  constexpr std::uint64_t generation = 11U;
  if (!check(service.submit(genplusgx::StateStorageCommand::activate(
        10U, generation, fixture.path(), 0x80U)),
      "activation could not be queued")) {
    return 5;
  }
  const auto activated = waitForOperation(service, 10U);
  if (!check(activated && activated->succeeded() &&
        activated->type == genplusgx::StateStorageEventType::sessionActivated &&
        std::ranges::all_of(activated->slotSummaries, [](const auto& slot) {
          return slot.availability == genplusgx::StateSlotAvailability::empty;
        }),
      "fresh game session did not expose ten empty slots")) {
    return 6;
  }

  const auto payload = fakeRawState(0x5AU);
  if (!check(service.submit(genplusgx::StateStorageCommand::save(
        11U, generation, 3U, 77U, payload)),
      "slot save could not be queued")) {
    return 7;
  }
  const auto saved = waitForOperation(service, 11U);
  if (!check(saved && saved->succeeded() &&
        saved->type == genplusgx::StateStorageEventType::slotSaved &&
        saved->slotSummaries[3].availability ==
          genplusgx::StateSlotAvailability::available &&
        saved->slotSummaries[3].metadata.emulatedFrameNumber == 77U,
      "slot save did not publish refreshed metadata")) {
    return 8;
  }

  if (!check(service.submit(genplusgx::StateStorageCommand::simple(
        genplusgx::StateStorageCommandType::loadSlot,
        12U,
        generation,
        3U)),
      "slot load could not be queued")) {
    return 9;
  }
  const auto loaded = waitForOperation(service, 12U);
  if (!check(loaded && loaded->succeeded() &&
        loaded->type == genplusgx::StateStorageEventType::slotLoaded &&
        loaded->rawPayload == payload &&
        loaded->metadata.emulatedFrameNumber == 77U,
      "slot load did not return the validated raw payload")) {
    return 10;
  }

  if (!check(service.submit(genplusgx::StateStorageCommand::simple(
        genplusgx::StateStorageCommandType::loadSlot,
        13U,
        generation + 1U,
        3U)),
      "stale request could not be queued")) {
    return 11;
  }
  const auto stale = waitForOperation(service, 13U);
  if (!check(stale && !stale->succeeded() &&
        stale->error == genplusgx::StateStorageError::staleGame,
      "stale game generation reached persistence")) {
    return 12;
  }

  const auto identity = genplusgx::identifyGame(fixture.path());
  genplusgx::SaveStateManager manager{paths};
  auto encoded = genplusgx::readFileBounded(
    manager.statePath(identity.identity, 3U),
    genplusgx::SaveStateManager::maximumPayloadBytes + 128U);
  if (!check(identity.status && encoded.status && encoded.exists,
        "saved state could not be opened for corruption test")) {
    return 13;
  }
  encoded.data.back() ^= 0x01U;
  if (!check(genplusgx::writeFileAtomically(
        manager.statePath(identity.identity, 3U),
        encoded.data,
        encoded.data.size()),
      "state corruption fixture could not be committed") ||
      !check(service.submit(genplusgx::StateStorageCommand::simple(
        genplusgx::StateStorageCommandType::refreshSlots,
        14U,
        generation)),
        "slot refresh could not be queued")) {
    return 14;
  }
  const auto refreshed = waitForOperation(service, 14U);
  if (!check(refreshed && refreshed->succeeded() &&
        refreshed->slotSummaries[3].availability ==
          genplusgx::StateSlotAvailability::invalid &&
        !refreshed->slotSummaries[3].message.empty(),
      "corrupt slot was not identified without exposing its payload")) {
    return 15;
  }

  if (!check(service.submit(genplusgx::StateStorageCommand::simple(
        genplusgx::StateStorageCommandType::deleteSlot,
        15U,
        generation,
        3U)),
      "corrupt slot delete could not be queued")) {
    return 16;
  }
  const auto deleted = waitForOperation(service, 15U);
  if (!check(deleted && deleted->succeeded() &&
        deleted->type == genplusgx::StateStorageEventType::slotDeleted &&
        deleted->slotSummaries[3].availability ==
          genplusgx::StateSlotAvailability::empty,
      "slot delete did not publish an empty replacement")) {
    return 17;
  }

  if (!check(service.submit(genplusgx::StateStorageCommand::simple(
        genplusgx::StateStorageCommandType::deactivateGame,
        16U,
        generation)),
      "deactivation could not be queued")) {
    return 18;
  }
  const auto deactivated = waitForOperation(service, 16U);
  if (!check(deactivated && deactivated->succeeded() &&
        deactivated->type ==
          genplusgx::StateStorageEventType::sessionDeactivated,
      "session deactivation failed") ||
      !check(service.metrics().commandQueueDepth == 0U &&
          service.metrics().eventQueueDepth == 0U &&
          service.metrics().droppedEvents == 0U,
        "normal storage workflow exceeded or dropped bounded queue entries")) {
    return 19;
  }

  if (!check(service.stop(), "storage service stop failed") ||
      !check(service.state() == genplusgx::StateStorageServiceState::stopped,
        "storage service did not enter stopped state") ||
      !check(service.submit(genplusgx::StateStorageCommand::activate(
        20U, 12U, fixture.path(), 0x80U)).error ==
        genplusgx::StateStorageError::notRunning,
        "stopped storage service accepted a command") ||
      !check(service.start(), "storage service restart failed") ||
      !check(service.waitForEvent(2s).has_value(), "restart event missing") ||
      !check(service.stop(), "restarted storage service stop failed")) {
    return 20;
  }
  return 0;
}
