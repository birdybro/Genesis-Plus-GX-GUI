#include "genplusgx/backup_store.h"

#include "synthetic_rom.h"

#include <QTemporaryDir>

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <ranges>
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
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "temporary application root creation failed")) {
    return 1;
  }

  const genplusgx::ApplicationPaths paths{
    std::filesystem::path{temporary.path().toStdString()} / "app"};
  genplusgx::PerGameBackupStore store{genplusgx::PersistenceStore{paths}};
  const genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  if (!check(store.beginGame(fixture.path()), "game identity creation failed") ||
      !check(store.hasActiveGame(), "successful begin did not retain identity")) {
    return 2;
  }

  constexpr std::array<std::uint8_t, 4> cartridge{1U, 2U, 3U, 4U};
  constexpr std::array<std::uint8_t, 5> internalBram{5U, 6U, 7U, 8U, 9U};
  constexpr std::array<std::uint8_t, 6> cartridgeBram{
    10U, 11U, 12U, 13U, 14U, 15U};
  if (!check(store.save(genplusgx::BackupMemoryKind::cartridgeSram, cartridge),
        "cartridge SRAM save failed") ||
      !check(store.save(genplusgx::BackupMemoryKind::scdInternalBram, internalBram),
        "Sega CD internal BRAM save failed") ||
      !check(store.save(
        genplusgx::BackupMemoryKind::scdRamCartridge, cartridgeBram),
        "Sega CD RAM cartridge save failed")) {
    return 3;
  }

  const auto loadedCartridge = store.load(
    genplusgx::BackupMemoryKind::cartridgeSram, cartridge.size());
  const auto loadedInternal = store.load(
    genplusgx::BackupMemoryKind::scdInternalBram, internalBram.size());
  const auto loadedCartridgeBram = store.load(
    genplusgx::BackupMemoryKind::scdRamCartridge, cartridgeBram.size());
  if (!check(loadedCartridge.status && loadedCartridge.exists &&
        std::ranges::equal(loadedCartridge.data, cartridge),
      "cartridge SRAM mapping round trip failed") ||
      !check(loadedInternal.status && loadedInternal.exists &&
        std::ranges::equal(loadedInternal.data, internalBram),
      "Sega CD internal BRAM mapping round trip failed") ||
      !check(loadedCartridgeBram.status && loadedCartridgeBram.exists &&
        std::ranges::equal(loadedCartridgeBram.data, cartridgeBram),
      "Sega CD RAM cartridge mapping round trip failed")) {
    return 4;
  }

  const auto wrongSize = store.load(
    genplusgx::BackupMemoryKind::scdInternalBram, internalBram.size() - 1U);
  if (!check(!wrongSize.status && wrongSize.exists &&
        wrongSize.status.error == genplusgx::BackupPersistenceError::loadFailed,
      "unexpected backup-memory size was not rejected")) {
    return 5;
  }

  store.endGame();
  const auto inactiveLoad = store.load(
    genplusgx::BackupMemoryKind::cartridgeSram, cartridge.size());
  if (!check(!store.hasActiveGame() && !inactiveLoad.status &&
        inactiveLoad.status.error ==
          genplusgx::BackupPersistenceError::noActiveGame &&
        store.save(genplusgx::BackupMemoryKind::cartridgeSram, cartridge).error ==
          genplusgx::BackupPersistenceError::noActiveGame,
      "ended game identity still permitted persistence access")) {
    return 6;
  }
  return 0;
}
