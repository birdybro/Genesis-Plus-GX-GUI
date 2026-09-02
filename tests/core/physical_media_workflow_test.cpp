#include "genplusgx/core_adapter.h"
#include "genplusgx/platform/physical_media.h"

#include "physical_media_fixture.h"
#include "synthetic_rom.h"

#include <QTemporaryDir>

#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

bool check(bool condition, std::string_view message)
{
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

} // namespace

int main()
{
  QTemporaryDir cacheDirectory;
  if (!check(cacheDirectory.isValid(),
        "The physical-media integration cache was unavailable")) {
    return 1;
  }
  auto backend = genplusgx::test::SyntheticPhysicalMediaBackend{};
  const auto cache =
    std::filesystem::path{cacheDirectory.path().toStdString()} / "physical";
  auto imported = genplusgx::platform::snapshotPhysicalDisc(
    backend, backend.disc.drive.id, cache);
  if (!check(imported.status && imported.snapshot.valid(),
        "The legal synthetic physical disc could not be imported")) {
    return 2;
  }

  const genplusgx::test::TemporaryFixture bios{
    genplusgx::test::makeSegaCdBios(), ".bin"};
  genplusgx::CoreAdapter adapter;
  genplusgx::CoreFirmwareSettings firmware;
  firmware.segaCdUsa = bios.path();
  genplusgx::CoreDiscInfo disc;
  genplusgx::CoreTimingInfo timing;
  if (!check(adapter.initialize(), "The core adapter did not initialize") ||
      !check(adapter.applyFirmwareSettings(firmware),
        "The synthetic Sega CD firmware was rejected") ||
      !check(adapter.loadGame(imported.snapshot.cuePath),
        "The imported raw BIN/CUE snapshot did not load") ||
      !check(adapter.discInfo(disc) && disc.segaCd && disc.discPresent &&
          !disc.trayOpen && disc.trackCount == 2U &&
          disc.path == imported.snapshot.cuePath,
        "The core did not retain the imported mixed-mode disc layout") ||
      !check(adapter.timingInfo(timing) && timing.segaCd,
        "The imported physical disc did not select Sega CD timing") ||
      !check(adapter.runFrame(),
        "The imported physical disc could not execute a core frame")) {
    static_cast<void>(adapter.shutdown());
    static_cast<void>(genplusgx::platform::releasePhysicalMediaSnapshot(
      cache, imported.snapshot));
    return 3;
  }

  if (!check(std::filesystem::exists(imported.snapshot.cuePath),
        "The transient physical snapshot disappeared while the core owned it") ||
      !check(adapter.unloadGame() && adapter.shutdown(),
        "The physical Sega CD session did not unload cleanly") ||
      !check(genplusgx::platform::releasePhysicalMediaSnapshot(
          cache, imported.snapshot) &&
          !std::filesystem::exists(imported.snapshot.storageDirectory),
        "The physical snapshot did not release after core shutdown")) {
    return 4;
  }
  return 0;
}
