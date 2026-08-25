#include "genplusgx/persistence.h"
#include "synthetic_rom.h"

#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

} // namespace

int main()
{
  QTemporaryDir temporaryRoot;
  if (!check(temporaryRoot.isValid(), "Could not create an isolated persistence root")) {
    return 1;
  }

  const auto root = std::filesystem::path{temporaryRoot.path().toStdString()} / "app-data";
  genplusgx::PersistenceStore store{genplusgx::ApplicationPaths{root}};
  if (!check(store.initialize(), "Application-data hierarchy initialization failed") ||
      !check(std::filesystem::is_directory(store.paths().configDirectory()) &&
          std::filesystem::is_directory(store.paths().savesDirectory()) &&
          std::filesystem::is_directory(store.paths().statesDirectory()) &&
          std::filesystem::is_directory(store.paths().screenshotsDirectory()) &&
          std::filesystem::is_directory(store.paths().libraryDirectory()) &&
          std::filesystem::is_directory(store.paths().logsDirectory()),
        "Application-data hierarchy was incomplete")) {
    return 2;
  }

  if (!check(genplusgx::sanitizeFilename(" Sonic 2: The / Game?.bin ") ==
          "Sonic_2_The_Game_.bin",
        "Unsafe title sanitization was incorrect") ||
      !check(genplusgx::sanitizeFilename("../../") == "game",
        "Traversal-only title was not replaced") ||
      !check(genplusgx::sanitizeFilename("CON") == "_CON",
        "Reserved Windows device name was not escaped") ||
      !check(genplusgx::sanitizeFilename("long title", 4U) == "long",
        "Sanitized title length was not bounded") ||
      !check(genplusgx::sanitizeFilename("anything", 0U).empty(),
        "Zero-length sanitization did not remain bounded")) {
    return 3;
  }

  auto rom = genplusgx::test::makeGenesisRamMarkerRom();
  const genplusgx::test::TemporaryFixture firstFixture{rom, ".bin"};
  rom.back() ^= 0x01U;
  const genplusgx::test::TemporaryFixture secondFixture{rom, ".bin"};
  const auto firstIdentity = genplusgx::identifyGame(firstFixture.path(), "Persistence: Test");
  const auto repeatedIdentity = genplusgx::identifyGame(firstFixture.path(), "Persistence: Test");
  const auto secondIdentity = genplusgx::identifyGame(secondFixture.path(), "Persistence: Test");
  if (!check(firstIdentity.status && firstIdentity.identity.valid(),
        "Generated game identity was invalid") ||
      !check(firstIdentity.identity.sha256 == repeatedIdentity.identity.sha256,
        "SHA-256 game identity was not deterministic") ||
      !check(firstIdentity.identity.sha256.size() == 64U,
        "SHA-256 game identity had the wrong encoded length") ||
      !check(firstIdentity.identity.sha256 != secondIdentity.identity.sha256,
        "Different game content collided") ||
      !check(firstIdentity.identity.titleSlug == "Persistence_Test",
        "Identity title was not sanitized") ||
      !check(store.gameSaveDirectory(firstIdentity.identity) !=
          store.gameSaveDirectory(secondIdentity.identity),
        "Per-game save directories collided")) {
    return 4;
  }

  const std::array<std::uint8_t, 8> cartridge{0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
  const std::array<std::uint8_t, 6> internalBram{1, 1, 2, 3, 5, 8};
  const std::array<std::uint8_t, 5> cartridgeBram{13, 21, 34, 55, 89};
  if (!check(store.saveRam(firstIdentity.identity, genplusgx::SaveRamKind::cartridge, cartridge),
        "Cartridge SRAM atomic save failed") ||
      !check(store.saveRam(firstIdentity.identity, genplusgx::SaveRamKind::scdInternal, internalBram),
        "Sega CD internal BRAM atomic save failed") ||
      !check(store.saveRam(firstIdentity.identity, genplusgx::SaveRamKind::scdRamCartridge, cartridgeBram),
        "Sega CD RAM cartridge atomic save failed") ||
      !check(store.ramPath(firstIdentity.identity, genplusgx::SaveRamKind::cartridge).filename() ==
          "cartridge.srm" &&
          store.ramPath(firstIdentity.identity, genplusgx::SaveRamKind::scdInternal).filename() ==
          "scd-internal.brm" &&
          store.ramPath(firstIdentity.identity, genplusgx::SaveRamKind::scdRamCartridge).filename() ==
          "scd-cartridge.brm",
        "Save-RAM kind paths were incorrect")) {
    return 5;
  }

  const auto loadedCartridge = store.loadRam(
    firstIdentity.identity, genplusgx::SaveRamKind::cartridge);
  const auto loadedInternal = store.loadRam(
    firstIdentity.identity, genplusgx::SaveRamKind::scdInternal);
  const auto loadedCartridgeBram = store.loadRam(
    firstIdentity.identity, genplusgx::SaveRamKind::scdRamCartridge);
  const auto missing = store.loadRam(
    secondIdentity.identity, genplusgx::SaveRamKind::cartridge);
  if (!check(loadedCartridge.status && loadedCartridge.exists &&
          std::ranges::equal(loadedCartridge.data, cartridge),
        "Cartridge SRAM round trip failed") ||
      !check(loadedInternal.status && loadedInternal.exists &&
          std::ranges::equal(loadedInternal.data, internalBram),
        "Sega CD internal BRAM round trip failed") ||
      !check(loadedCartridgeBram.status && loadedCartridgeBram.exists &&
          std::ranges::equal(loadedCartridgeBram.data, cartridgeBram),
        "Sega CD RAM cartridge round trip failed") ||
      !check(missing.status && !missing.exists && missing.data.empty(),
        "Missing save RAM was not represented as an empty success")) {
    return 6;
  }

  const std::array<std::uint8_t, 4> replacement{9, 8, 7, 6};
  if (!check(store.saveRam(firstIdentity.identity, genplusgx::SaveRamKind::cartridge, replacement),
        "Atomic slot replacement failed")) {
    return 7;
  }
  std::vector<std::uint8_t> oversized(genplusgx::PersistenceStore::maximumRamBytes + 1U, 0xA5U);
  if (!check(store.saveRam(
          firstIdentity.identity, genplusgx::SaveRamKind::cartridge, oversized).error ==
          genplusgx::PersistenceError::dataTooLarge,
        "Oversized RAM payload was accepted")) {
    return 8;
  }
  const auto afterRejectedWrite = store.loadRam(
    firstIdentity.identity, genplusgx::SaveRamKind::cartridge);
  const auto boundedRejection = store.loadRam(
    firstIdentity.identity, genplusgx::SaveRamKind::cartridge, 3U);
  if (!check(afterRejectedWrite.status &&
          std::ranges::equal(afterRejectedWrite.data, replacement),
        "Rejected write damaged the previous atomic save") ||
      !check(boundedRejection.exists &&
          boundedRejection.status.error == genplusgx::PersistenceError::dataTooLarge,
        "Bounded corruption guard did not reject an unexpected file size")) {
    return 9;
  }

  const auto corruptPath = store.ramPath(
    secondIdentity.identity, genplusgx::SaveRamKind::scdInternal);
  std::filesystem::create_directories(corruptPath);
  const auto nonFile = store.loadRam(
    secondIdentity.identity, genplusgx::SaveRamKind::scdInternal);
  genplusgx::GameIdentity invalidIdentity;
  genplusgx::GameIdentity traversalIdentity{
    .sha256 = std::string(64U, 'a'),
    .titleSlug = "../escape",
  };
  if (!check(nonFile.exists &&
          nonFile.status.error == genplusgx::PersistenceError::fileReadFailed,
        "Non-file save path was not handled as corruption") ||
      !check(store.saveRam(invalidIdentity, genplusgx::SaveRamKind::cartridge, cartridge).error ==
          genplusgx::PersistenceError::invalidGameIdentity,
        "Invalid game identity reached persistence") ||
      !check(store.saveRam(traversalIdentity, genplusgx::SaveRamKind::cartridge, cartridge).error ==
          genplusgx::PersistenceError::invalidGameIdentity,
        "Traversal-bearing game identity reached persistence")) {
    return 10;
  }

  const auto blockedRoot = std::filesystem::path{temporaryRoot.path().toStdString()} / "blocked";
  QFile blocker{QString::fromStdString(blockedRoot.string())};
  if (!check(blocker.open(QIODevice::WriteOnly) && blocker.write("x", 1) == 1,
        "Could not create directory-failure fixture")) {
    return 11;
  }
  blocker.close();
  const genplusgx::ApplicationPaths blockedPaths{blockedRoot};
  if (!check(blockedPaths.initialize().error ==
          genplusgx::PersistenceError::directoryCreationFailed,
        "A file-backed application root was not rejected")) {
    return 12;
  }
  const genplusgx::ApplicationPaths relativePaths{"relative-app-data"};
  if (!check(relativePaths.initialize().error == genplusgx::PersistenceError::invalidRoot,
        "A current-directory-relative application root was accepted")) {
    return 13;
  }

  return 0;
}
