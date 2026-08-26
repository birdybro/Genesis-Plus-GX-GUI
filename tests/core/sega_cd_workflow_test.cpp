#include "genplusgx/backup_store.h"
#include "genplusgx/core_adapter.h"
#include "genplusgx/emulation_worker.h"
#include "genplusgx/persistence.h"

#include "synthetic_rom.h"

#include <QTemporaryDir>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
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

bool writeBytes(
  const std::filesystem::path& path,
  std::span<const std::uint8_t> bytes)
{
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(
    reinterpret_cast<const char*>(bytes.data()),
    static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(stream);
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

std::optional<genplusgx::EmulationEvent> submit(
  genplusgx::EmulationWorker& worker,
  genplusgx::EmulationCommand command)
{
  const auto operationId = command.operationId;
  if (!worker.submit(std::move(command))) {
    return std::nullopt;
  }
  return waitForOperation(worker, operationId);
}

} // namespace

int main()
{
  const genplusgx::test::TemporaryFixture bios{
    genplusgx::test::makeSegaCdBios(), ".bin"};
  const genplusgx::test::TemporaryFixture usaDisc{
    genplusgx::test::makeSegaCdDiscImage(), ".iso"};
  const genplusgx::test::TemporaryFixture europeDisc{
    genplusgx::test::makeSegaCdDiscImage(
      genplusgx::test::SyntheticSegaCdRegion::europe), ".iso"};

  genplusgx::CoreAdapter adapter;
  if (!check(adapter.initialize(), "core initialization failed")) {
    return 1;
  }
  genplusgx::CoreFirmwareSettings overlongFirmware;
  overlongFirmware.segaCdUsa = std::filesystem::path{
    std::string(genplusgx::maximumCoreFirmwarePathBytes + 1U, 'x')};
  if (!check(adapter.applyFirmwareSettings(overlongFirmware).error ==
        genplusgx::CoreError::invalidSettings,
      "an overlong firmware path crossed the core host boundary")) {
    return 2;
  }

  const auto noFirmware = adapter.loadGame(usaDisc.path());
  if (!check(!noFirmware &&
        noFirmware.error == genplusgx::CoreError::missingFirmware &&
        noFirmware.message.find("USA") != std::string::npos &&
        adapter.state() == genplusgx::CoreLifecycleState::ready,
      "USA disc did not produce a region-specific missing-BIOS error")) {
    return 3;
  }

  genplusgx::CoreFirmwareSettings wrongRegion;
  wrongRegion.segaCdEurope = bios.path();
  if (!check(adapter.applyFirmwareSettings(wrongRegion),
        "Europe firmware settings were rejected") ||
      !check(adapter.loadGame(usaDisc.path()).error ==
          genplusgx::CoreError::missingFirmware,
        "a firmware configured for the wrong region was accepted")) {
    return 4;
  }

  genplusgx::CoreDiscInfo disc;
  if (!check(adapter.loadGame(europeDisc.path()) && adapter.discInfo(disc) &&
        disc.segaCd && disc.region == genplusgx::CoreDiscRegion::europe &&
        disc.discPresent && adapter.unloadGame(),
      "Europe disc did not select the independent Europe firmware path")) {
    return 5;
  }

  genplusgx::CoreFirmwareSettings firmware;
  firmware.segaCdUsa = bios.path();
  if (!check(adapter.applyFirmwareSettings(firmware),
        "USA firmware settings were rejected") ||
      !check(adapter.loadGame(usaDisc.path()),
        "synthetic Sega CD image did not load with synthetic firmware")) {
    return 6;
  }

  genplusgx::CoreTimingInfo timing;
  genplusgx::BackupMemoryInfo internalBram;
  genplusgx::BackupMemoryInfo cartridgeBram;
  if (!check(adapter.discInfo(disc) && disc.segaCd && disc.discPresent &&
        !disc.trayOpen && disc.trackCount >= 1U &&
        disc.region == genplusgx::CoreDiscRegion::usa &&
        disc.path == usaDisc.path(),
      "loaded Sega CD session metadata was incorrect") ||
      !check(adapter.timingInfo(timing) && timing.segaCd,
        "Sega CD timing was not identified") ||
      !check(adapter.backupMemoryInfo(
          genplusgx::BackupMemoryKind::scdInternalBram, internalBram) &&
        internalBram.available && internalBram.size == 0x2000U,
        "internal Sega CD BRAM was unavailable") ||
      !check(adapter.backupMemoryInfo(
          genplusgx::BackupMemoryKind::scdRamCartridge, cartridgeBram) &&
        cartridgeBram.available && cartridgeBram.size == 0x80000U,
        "Sega CD RAM cartridge was unavailable") ||
      !check(adapter.runFrame(),
        "synthetic BIOS could not execute one deterministic frame")) {
    return 7;
  }

  if (!check(adapter.setDiscEjected(true) && adapter.discInfo(disc) &&
        disc.trayOpen && disc.discPresent,
      "disc eject did not open the tray while retaining the mounted image") ||
      !check(adapter.setDiscEjected(false) && adapter.discInfo(disc) &&
        !disc.trayOpen && disc.discPresent,
        "disc tray could not close around the mounted image")) {
    return 8;
  }

  const genplusgx::test::TemporaryFixture malformedDisc{{}, ".iso"};
  const auto rejectedSwap = adapter.changeDisc(malformedDisc.path());
  if (!check(!rejectedSwap &&
        rejectedSwap.error == genplusgx::CoreError::invalidDiscImage &&
        adapter.discInfo(disc) && disc.trayOpen && !disc.discPresent &&
        disc.path.empty(),
      "a malformed replacement disc did not fail open and recoverably")) {
    return 9;
  }

  QTemporaryDir cueDirectory;
  if (!check(cueDirectory.isValid(), "CUE fixture directory creation failed")) {
    return 10;
  }
  const auto cueRoot = std::filesystem::path{cueDirectory.path().toStdString()};
  const auto binPath = cueRoot / "fixture.bin";
  const auto cuePath = cueRoot / "fixture.cue";
  const auto cueDisc = genplusgx::test::makeSegaCdDiscImage();
  const std::string cueText =
    "FILE \"fixture.bin\" BINARY\n"
    "  TRACK 01 MODE1/2048\n"
    "    INDEX 01 00:00:00\n";
  if (!check(writeBytes(binPath, cueDisc), "CUE/BIN data file write failed") ||
      !check(writeBytes(cuePath, std::span<const std::uint8_t>{
          reinterpret_cast<const std::uint8_t*>(cueText.data()), cueText.size()}),
        "CUE sheet write failed") ||
      !check(adapter.changeDisc(cuePath) && adapter.discInfo(disc) &&
        disc.discPresent && !disc.trayOpen && disc.trackCount >= 1U &&
        disc.path == cuePath,
        "valid CUE/BIN replacement disc did not mount")) {
    return 11;
  }

  const auto unsafeCuePath = cueRoot / "unsafe.cue";
  const std::string unsafeCueText =
    "FILE \"../fixture.bin\" BINARY\n"
    "  TRACK 01 MODE1/2048\n"
    "    INDEX 01 00:00:00\n";
  if (!check(writeBytes(unsafeCuePath, std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(unsafeCueText.data()),
        unsafeCueText.size()}), "unsafe CUE fixture write failed")) {
    return 12;
  }
  const auto rejectedCueSwap = adapter.changeDisc(unsafeCuePath);
  if (!check(!rejectedCueSwap &&
        rejectedCueSwap.error == genplusgx::CoreError::invalidDiscImage &&
        adapter.discInfo(disc) && disc.discPresent && !disc.trayOpen &&
        disc.path == cuePath,
      "unsafe CUE replacement reached or disturbed the mounted core disc")) {
    return 13;
  }
  const auto rejectedCueLoad = adapter.loadGame(unsafeCuePath);
  if (!check(!rejectedCueLoad &&
        rejectedCueLoad.error == genplusgx::CoreError::invalidDiscImage &&
        adapter.state() == genplusgx::CoreLifecycleState::loaded &&
        adapter.discInfo(disc) && disc.path == cuePath,
      "unsafe CUE game loading bypassed preflight or unloaded the active game") ||
      !check(adapter.unloadGame() && adapter.shutdown(),
        "direct Sega CD session did not shut down cleanly")) {
    return 14;
  }

  QTemporaryDir persistenceDirectory;
  if (!check(persistenceDirectory.isValid(),
        "persistence fixture directory creation failed")) {
    return 12;
  }
  const genplusgx::ApplicationPaths paths{
    std::filesystem::path{persistenceDirectory.path().toStdString()} / "app"};
  genplusgx::PersistenceStore inspectionStore{paths};
  if (!check(inspectionStore.initialize(), "persistence root initialization failed")) {
    return 13;
  }
  const auto identity = genplusgx::identifyGame(usaDisc.path());
  if (!check(identity.status, "Sega CD fixture identification failed")) {
    return 14;
  }

  auto liveStore = std::make_shared<genplusgx::PerGameBackupStore>(
    genplusgx::PersistenceStore{paths});
  genplusgx::EmulationWorker worker{64U, 64U, 48'000, {}, {}, liveStore};
  if (!check(worker.start(), "Sega CD worker start failed") ||
      !check(worker.waitForEvent(2s).has_value(), "worker start event missing")) {
    return 15;
  }

  auto event = submit(worker,
    genplusgx::EmulationCommand::updateFirmwareSettings(20U, firmware));
  if (!check(event && event->succeeded(), "worker firmware update failed")) {
    return 16;
  }
  event = submit(worker, genplusgx::EmulationCommand::load(21U, usaDisc.path()));
  if (!check(event && event->succeeded() && event->disc.segaCd &&
        event->disc.discPresent && event->workerState ==
          genplusgx::EmulationWorkerState::paused,
      "worker did not expose a loaded Sega CD session")) {
    return 17;
  }
  event = submit(worker, genplusgx::EmulationCommand::discEjected(22U, true));
  if (!check(event && event->succeeded() && event->disc.trayOpen,
        "worker disc eject failed")) {
    return 18;
  }
  event = submit(worker, genplusgx::EmulationCommand::changeDisc(23U, cuePath));
  if (!check(event && event->succeeded() && event->disc.discPresent &&
        !event->disc.trayOpen && event->disc.path == cuePath,
      "worker disc change failed")) {
    return 19;
  }
  event = submit(worker, genplusgx::EmulationCommand::simple(
    genplusgx::EmulationCommandType::frameAdvance, 24U));
  if (!check(event && event->succeeded(), "worker Sega CD frame failed")) {
    return 20;
  }
  event = submit(worker, genplusgx::EmulationCommand::simple(
    genplusgx::EmulationCommandType::unloadGame, 25U));
  if (!check(event && event->succeeded() && !event->disc.segaCd,
        "worker Sega CD unload failed") ||
      !check(worker.stop(), "worker Sega CD shutdown failed")) {
    return 21;
  }

  const auto internal = inspectionStore.loadRam(
    identity.identity, genplusgx::SaveRamKind::scdInternal);
  const auto cartridge = inspectionStore.loadRam(
    identity.identity, genplusgx::SaveRamKind::scdRamCartridge);
  if (!check(internal.status && internal.exists && internal.data.size() == 0x2000U,
        "internal Sega CD BRAM was not persisted") ||
      !check(cartridge.status && cartridge.exists &&
        cartridge.data.size() == 0x80000U,
        "Sega CD RAM cartridge was not persisted") ||
      !check(!liveStore->hasActiveGame(),
        "Sega CD unload retained a persistence identity")) {
    return 22;
  }
  return 0;
}
