#include "genplusgx/backup_store.h"
#include "genplusgx/emulation_worker.h"
#include "genplusgx/persistence.h"

#include "synthetic_rom.h"

#include <QTemporaryDir>

#include <chrono>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

using namespace std::chrono_literals;

namespace {

class FailingBackupPersistence final : public genplusgx::BackupMemoryPersistence {
public:
  std::atomic<bool> failSave{false};
  std::atomic<int> beginCount{0};
  std::atomic<int> endCount{0};

  genplusgx::BackupPersistenceStatus beginGame(
    const std::filesystem::path&) override
  {
    ++beginCount;
    return {};
  }

  genplusgx::BackupPersistenceLoadResult load(
    genplusgx::BackupMemoryKind,
    std::size_t) override
  {
    return {};
  }

  genplusgx::BackupPersistenceStatus save(
    genplusgx::BackupMemoryKind,
    std::span<const std::uint8_t>) override
  {
    if (failSave.load()) {
      return {
        .error = genplusgx::BackupPersistenceError::saveFailed,
        .message = "Injected atomic save failure.",
      };
    }
    return {};
  }

  void endGame() noexcept override
  {
    ++endCount;
  }
};

bool check(bool condition, std::string_view message)
{
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
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

} // namespace

int main()
{
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "temporary root creation failed")) {
    return 1;
  }
  const auto root = std::filesystem::path{temporary.path().toStdString()} / "app";
  const genplusgx::ApplicationPaths paths{root};
  genplusgx::PersistenceStore inspectionStore{paths};
  if (!check(inspectionStore.initialize(), "persistence root initialization failed")) {
    return 2;
  }
  const genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisSramWriterRom(), ".md"};
  const auto identity = genplusgx::identifyGame(fixture.path());
  if (!check(identity.status, "fixture identity failed")) {
    return 3;
  }

  auto liveStore = std::make_shared<genplusgx::PerGameBackupStore>(
    genplusgx::PersistenceStore{paths});
  genplusgx::EmulationWorker worker{64U, 64U, 48'000, {}, {}, liveStore};
  if (!check(worker.start(), "worker start failed") ||
      !check(worker.waitForEvent(2s).has_value(), "worker start event missing") ||
      !check(submitAndSucceed(worker,
        genplusgx::EmulationCommand::load(1U, fixture.path())),
        "persistent fixture load failed") ||
      !check(submitAndSucceed(worker, genplusgx::EmulationCommand::simple(
        genplusgx::EmulationCommandType::frameAdvance, 2U)),
        "persistent fixture frame failed") ||
      !check(submitAndSucceed(worker, genplusgx::EmulationCommand::simple(
        genplusgx::EmulationCommandType::unloadGame, 3U)),
        "persistent fixture unload failed")) {
    return 4;
  }

  auto saved = inspectionStore.loadRam(
    identity.identity, genplusgx::SaveRamKind::cartridge);
  if (!check(saved.status && saved.exists && saved.data.size() == 0x10000U &&
        saved.data.front() == 0x5AU,
      "frame-written SRAM was not atomically flushed on unload") ||
      !check(!liveStore->hasActiveGame(),
        "unload retained the previous game identity")) {
    return 5;
  }

  std::vector<std::uint8_t> persisted(0x10000U, 0xA5U);
  if (!check(inspectionStore.saveRam(
        identity.identity, genplusgx::SaveRamKind::cartridge, persisted),
        "known SRAM preload failed") ||
      !check(submitAndSucceed(worker,
        genplusgx::EmulationCommand::load(4U, fixture.path())),
        "reload with persisted SRAM failed") ||
      !check(submitAndSucceed(worker, genplusgx::EmulationCommand::simple(
        genplusgx::EmulationCommandType::unloadGame, 5U)),
        "unload after persisted SRAM load failed")) {
    return 6;
  }
  saved = inspectionStore.loadRam(identity.identity, genplusgx::SaveRamKind::cartridge);
  if (!check(saved.status && saved.data == persisted,
        "persisted SRAM was not loaded before the first frame")) {
    return 7;
  }

  const std::vector<std::uint8_t> corrupt{1U, 2U, 3U};
  if (!check(inspectionStore.saveRam(
        identity.identity, genplusgx::SaveRamKind::cartridge, corrupt),
        "corrupt SRAM fixture write failed") ||
      !check(worker.submit(genplusgx::EmulationCommand::load(6U, fixture.path())),
        "corrupt SRAM load could not be queued")) {
    return 8;
  }
  const auto rejected = waitForOperation(worker, 6U);
  if (!check(rejected && !rejected->succeeded() &&
        rejected->coreError == genplusgx::CoreError::invalidBackupMemory &&
        rejected->workerState == genplusgx::EmulationWorkerState::idle,
      "corrupt SRAM did not fail closed before emulation")) {
    return 9;
  }

  if (!check(inspectionStore.saveRam(
        identity.identity, genplusgx::SaveRamKind::cartridge, persisted),
        "shutdown preload failed") ||
      !check(submitAndSucceed(worker,
        genplusgx::EmulationCommand::load(7U, fixture.path())),
        "shutdown persistence load failed")) {
    return 10;
  }
  std::vector<std::uint8_t> diskMutation(0x10000U, 0xCCU);
  if (!check(inspectionStore.saveRam(
        identity.identity, genplusgx::SaveRamKind::cartridge, diskMutation),
        "shutdown disk mutation failed") ||
      !check(worker.stop(), "worker shutdown persistence failed")) {
    return 11;
  }
  saved = inspectionStore.loadRam(identity.identity, genplusgx::SaveRamKind::cartridge);
  if (!check(saved.status && saved.data == persisted,
        "clean shutdown did not flush the live SRAM image") ||
      !check(!liveStore->hasActiveGame(),
        "shutdown retained an active persistence identity")) {
    return 12;
  }

  auto failingStore = std::make_shared<FailingBackupPersistence>();
  genplusgx::EmulationWorker failingWorker{
    64U, 64U, 48'000, {}, {}, failingStore};
  if (!check(failingWorker.start(), "failure-path worker start failed") ||
      !check(failingWorker.waitForEvent(2s).has_value(),
        "failure-path worker start event missing") ||
      !check(submitAndSucceed(failingWorker,
        genplusgx::EmulationCommand::load(20U, fixture.path())),
        "failure-path game load failed")) {
    return 13;
  }
  failingStore->failSave.store(true);
  if (!check(failingWorker.submit(genplusgx::EmulationCommand::simple(
        genplusgx::EmulationCommandType::unloadGame, 21U)),
        "failing unload could not be queued")) {
    return 14;
  }
  const auto failedUnload = waitForOperation(failingWorker, 21U);
  if (!check(failedUnload && !failedUnload->succeeded() &&
        failedUnload->coreError == genplusgx::CoreError::persistenceFailed &&
        failedUnload->workerState == genplusgx::EmulationWorkerState::paused &&
        failingStore->endCount.load() == 0,
      "save failure did not preserve the loaded game and identity")) {
    return 15;
  }
  failingStore->failSave.store(false);
  if (!check(submitAndSucceed(failingWorker,
        genplusgx::EmulationCommand::simple(
          genplusgx::EmulationCommandType::unloadGame, 22U)),
        "recovery unload failed") ||
      !check(submitAndSucceed(failingWorker,
        genplusgx::EmulationCommand::load(23U, fixture.path())),
        "shutdown failure-path load failed")) {
    return 16;
  }
  failingStore->failSave.store(true);
  const auto failedStop = failingWorker.stop();
  if (!check(!failedStop &&
        failedStop.error == genplusgx::EmulationWorkerError::coreFailure &&
        failedStop.message.find("Injected") != std::string::npos &&
        failingStore->endCount.load() == 2,
      "shutdown did not surface save failure after releasing persistence identity")) {
    return 17;
  }
  return 0;
}
