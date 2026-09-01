#include "genplusgx/state_storage_service.h"

#include "synthetic_rom.h"

#include <QByteArray>
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

std::vector<std::uint8_t> fakePng()
{
  const auto bytes = QByteArray::fromBase64(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  return {
    reinterpret_cast<const std::uint8_t*>(bytes.constData()),
    reinterpret_cast<const std::uint8_t*>(bytes.constData()) +
      static_cast<std::size_t>(bytes.size())};
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
      "out-of-range slot was accepted") ||
      !check(service.submit(genplusgx::StateStorageCommand::file(
        genplusgx::StateStorageCommandType::importSlot,
        1U, 1U, 0U, {})).error ==
        genplusgx::StateStorageError::invalidCommand,
      "pathless import was accepted")) {
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
        11U, generation, 3U, 77U, payload, "Boss door", fakePng())),
      "slot save could not be queued")) {
    return 7;
  }
  const auto saved = waitForOperation(service, 11U);
  if (!check(saved && saved->succeeded() &&
        saved->type == genplusgx::StateStorageEventType::slotSaved &&
        saved->slotSummaries[3].availability ==
          genplusgx::StateSlotAvailability::available &&
        saved->slotSummaries[3].metadata.emulatedFrameNumber == 77U &&
        saved->slotSummaries[3].metadata.name == "Boss door" &&
        saved->slotSummaries[3].metadata.thumbnailPng == fakePng(),
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

  const auto exported = paths.statesDirectory() / "manual.gpgxstate";
  if (!check(service.submit(genplusgx::StateStorageCommand::file(
        genplusgx::StateStorageCommandType::exportSlot,
        30U, generation, 3U, exported)),
      "slot export could not be queued")) {
    return 25;
  }
  const auto exportEvent = waitForOperation(service, 30U);
  if (!check(exportEvent && exportEvent->succeeded() &&
        exportEvent->type == genplusgx::StateStorageEventType::slotExported &&
        exportEvent->path == exported,
      "slot export did not complete asynchronously") ||
      !check(service.submit(genplusgx::StateStorageCommand::file(
        genplusgx::StateStorageCommandType::importSlot,
        31U, generation, 4U, exported)),
      "slot import could not be queued")) {
    return 26;
  }
  const auto importEvent = waitForOperation(service, 31U);
  if (!check(importEvent && importEvent->succeeded() &&
        importEvent->type == genplusgx::StateStorageEventType::slotImported &&
        importEvent->slotSummaries[4].metadata.name == "Boss door" &&
        importEvent->slotSummaries[4].metadata.thumbnailPng == fakePng(),
      "slot import did not preserve presentation") ||
      !check(service.submit(genplusgx::StateStorageCommand::rename(
        32U, generation, 4U, "Final boss cleared")),
      "slot rename could not be queued")) {
    return 27;
  }
  const auto renameEvent = waitForOperation(service, 32U);
  if (!check(renameEvent && renameEvent->succeeded() &&
        renameEvent->type == genplusgx::StateStorageEventType::slotRenamed &&
        renameEvent->slotSummaries[4].metadata.name == "Final boss cleared" &&
        renameEvent->slotSummaries[4].metadata.thumbnailPng == fakePng(),
      "slot rename did not refresh metadata")) {
    return 28;
  }

  if (!check(service.submit(genplusgx::StateStorageCommand::saveResumeState(
        21U, generation, 78U, payload)),
      "resume checkpoint could not be queued")) {
    return 21;
  }
  const auto resumeSaved = waitForOperation(service, 21U);
  if (!check(resumeSaved && resumeSaved->succeeded() &&
        resumeSaved->type == genplusgx::StateStorageEventType::resumeSaved,
      "resume checkpoint was not saved asynchronously") ||
      !check(service.submit(genplusgx::StateStorageCommand::simple(
        genplusgx::StateStorageCommandType::loadResume,
        22U,
        generation)),
      "resume checkpoint load could not be queued")) {
    return 22;
  }
  const auto resumeLoaded = waitForOperation(service, 22U);
  if (!check(resumeLoaded && resumeLoaded->succeeded() &&
        resumeLoaded->type == genplusgx::StateStorageEventType::resumeLoaded &&
        resumeLoaded->rawPayload == payload &&
        resumeLoaded->metadata.emulatedFrameNumber == 78U,
      "resume checkpoint did not return validated payload") ||
      !check(service.submit(genplusgx::StateStorageCommand::simple(
        genplusgx::StateStorageCommandType::deleteResume,
        23U,
        generation)),
      "resume checkpoint deletion could not be queued")) {
    return 23;
  }
  const auto resumeDeleted = waitForOperation(service, 23U);
  if (!check(resumeDeleted && resumeDeleted->succeeded() &&
        resumeDeleted->type == genplusgx::StateStorageEventType::resumeDeleted,
      "resume checkpoint was not deleted asynchronously")) {
    return 24;
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
    genplusgx::SaveStateManager::maximumFileBytes);
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
