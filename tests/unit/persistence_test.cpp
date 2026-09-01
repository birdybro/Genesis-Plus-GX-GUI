#include "genplusgx/persistence.h"
#include "synthetic_rom.h"

#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

bool writeBytes(
  const std::filesystem::path& path,
  std::span<const std::uint8_t> bytes)
{
  QFile file{QString::fromStdString(path.string())};
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
    file.write(reinterpret_cast<const char*>(bytes.data()),
      static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size());
}

bool writeText(const std::filesystem::path& path, std::string_view text)
{
  return writeBytes(path, {
    reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
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
          std::filesystem::is_directory(store.paths().recordingsDirectory()) &&
          std::filesystem::is_directory(store.paths().libraryDirectory()) &&
          std::filesystem::is_directory(store.paths().logsDirectory()) &&
          std::filesystem::is_directory(store.paths().cacheDirectory()),
        "Application-data hierarchy was incomplete")) {
    return 2;
  }

  const auto launchDirectory =
    std::filesystem::path{temporaryRoot.path().toStdString()} / "relocated" / "bin";
  const auto launchPath = launchDirectory / "genesis-plus-gx-gui";
  const auto portablePaths =
    genplusgx::ApplicationPaths::fromPortableExecutable(launchPath);
  const auto secondLaunchPath =
    std::filesystem::path{temporaryRoot.path().toStdString()} /
    "another-copy" / "bin" / "genesis-plus-gx-gui";
  const auto macBundleLaunchPath =
    std::filesystem::path{temporaryRoot.path().toStdString()} /
    "Mac package" / "genesis-plus-gx-gui.app" / "Contents" / "MacOS" /
    "genesis-plus-gx-gui";
  if (!check(portablePaths.portable() &&
          portablePaths.mode() == genplusgx::ApplicationDataMode::portable &&
          portablePaths.root() == launchDirectory / "portable-data",
        "Portable mode did not resolve beside the executable") ||
      !check(genplusgx::portableApplicationDataRoot(secondLaunchPath) ==
          secondLaunchPath.parent_path() / "portable-data" &&
          genplusgx::portableApplicationDataRoot(secondLaunchPath) !=
            portablePaths.root(),
        "Relocating the executable did not relocate its isolated data root") ||
      !check(genplusgx::portableApplicationDataRoot(macBundleLaunchPath) ==
          macBundleLaunchPath.parent_path().parent_path().parent_path()
            .parent_path() / "portable-data",
        "A macOS bundle did not place portable data beside the app") ||
      !check(genplusgx::portableApplicationDataRoot("relative/app").empty() &&
          genplusgx::portableApplicationDataRoot(
            std::filesystem::path{"/"} / "app").empty(),
        "Unsafe relative or filesystem-root portable launch paths were accepted") ||
      !check(portablePaths.initialize() &&
          std::filesystem::is_directory(portablePaths.configDirectory()) &&
          std::filesystem::is_directory(portablePaths.savesDirectory()) &&
          std::filesystem::is_directory(portablePaths.cacheDirectory()),
        "The executable-relative portable hierarchy could not initialize") ||
      !check(genplusgx::applicationDataModeName(
          genplusgx::ApplicationDataMode::platform) == "Platform standard" &&
          genplusgx::applicationDataModeName(
            genplusgx::ApplicationDataMode::portable) == "Portable" &&
          genplusgx::ApplicationPaths::fromPlatform().mode() ==
            genplusgx::ApplicationDataMode::platform,
        "Application-data mode names were unstable")) {
    return 15;
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
  std::size_t observedHashBytes = 0U;
  std::size_t cancellationPolls = 0U;
  const auto cancelledHash = genplusgx::hashGameContent(
    firstFixture.path(),
    [&observedHashBytes](std::span<const std::uint8_t> bytes, std::uintmax_t) {
      observedHashBytes += bytes.size();
    },
    [&cancellationPolls] { return ++cancellationPolls >= 3U; });
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
        "Per-game save directories collided") ||
      !check(cancelledHash.status.error == genplusgx::PersistenceError::cancelled,
        "Game-content hashing ignored a cooperative cancellation request") ||
      !check(observedHashBytes > 0U && observedHashBytes <= 64U * 1024U,
        "Hash cancellation was not observed at a bounded chunk boundary")) {
    return 4;
  }

  QTemporaryDir cueRoot;
  constexpr std::string_view cueText{
    "FILE \"track.bin\" BINARY\n"
    "  TRACK 01 MODE1/2048\n"
    "    INDEX 01 00:00:00\n"
    "FILE \"audio.bin\" BINARY\n"
    "  TRACK 02 AUDIO\n"
    "    INDEX 01 00:00:00\n"};
  const auto dataTrack = genplusgx::test::makeSegaCdDiscImage();
  const std::array<std::uint8_t, 8U> audioTrackA{
    0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U};
  auto audioTrackB = audioTrackA;
  audioTrackB.back() ^= 0x01U;
  const auto cueBase = std::filesystem::path{cueRoot.path().toStdString()};
  const auto cueDirectoryA = cueBase / "a";
  const auto cueDirectoryB = cueBase / "b";
  const auto cueDirectoryRelocated = cueBase / "relocated";
  std::error_code cueError;
  std::filesystem::create_directories(cueDirectoryA, cueError);
  std::filesystem::create_directories(cueDirectoryB, cueError);
  std::filesystem::create_directories(cueDirectoryRelocated, cueError);
  const auto cueA = cueDirectoryA / "disc.cue";
  const auto cueB = cueDirectoryB / "disc.cue";
  const auto cueRelocated = cueDirectoryRelocated / "disc.cue";
  const bool cueFixturesWritten = cueRoot.isValid() && !cueError &&
    writeText(cueA, cueText) &&
    writeBytes(cueDirectoryA / "track.bin", dataTrack) &&
    writeBytes(cueDirectoryA / "audio.bin", audioTrackA) &&
    writeText(cueB, cueText) &&
    writeBytes(cueDirectoryB / "track.bin", dataTrack) &&
    writeBytes(cueDirectoryB / "audio.bin", audioTrackB) &&
    writeText(cueRelocated, cueText) &&
    writeBytes(cueDirectoryRelocated / "track.bin", dataTrack) &&
    writeBytes(cueDirectoryRelocated / "audio.bin", audioTrackA);
  const auto cueIdentityA = genplusgx::identifyGame(cueA, "Disc");
  const auto cueIdentityB = genplusgx::identifyGame(cueB, "Disc");
  const auto relocatedIdentity = genplusgx::identifyGame(cueRelocated, "Disc");
  if (!check(cueFixturesWritten && cueIdentityA.status && cueIdentityB.status &&
          relocatedIdentity.status,
        "Composite CUE identity fixtures could not be prepared") ||
      !check(cueIdentityA.identity.sha256 != cueIdentityB.identity.sha256,
        "Different CUE track content collided despite identical sheet text") ||
      !check(cueIdentityA.identity.sha256 == relocatedIdentity.identity.sha256,
        "Relocating identical CUE content changed its game identity") ||
      !check(store.gameSaveDirectory(cueIdentityA.identity) !=
          store.gameSaveDirectory(cueIdentityB.identity),
        "Different CUE games shared a persistence directory")) {
    return 5;
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
    return 6;
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
    return 7;
  }

  const std::array<std::uint8_t, 4> replacement{9, 8, 7, 6};
  if (!check(store.saveRam(firstIdentity.identity, genplusgx::SaveRamKind::cartridge, replacement),
        "Atomic slot replacement failed")) {
    return 8;
  }
  std::vector<std::uint8_t> oversized(genplusgx::PersistenceStore::maximumRamBytes + 1U, 0xA5U);
  if (!check(store.saveRam(
          firstIdentity.identity, genplusgx::SaveRamKind::cartridge, oversized).error ==
          genplusgx::PersistenceError::dataTooLarge,
        "Oversized RAM payload was accepted")) {
    return 9;
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
    return 10;
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
    return 11;
  }

  const auto blockedRoot = std::filesystem::path{temporaryRoot.path().toStdString()} / "blocked";
  QFile blocker{QString::fromStdString(blockedRoot.string())};
  if (!check(blocker.open(QIODevice::WriteOnly) && blocker.write("x", 1) == 1,
        "Could not create directory-failure fixture")) {
    return 12;
  }
  blocker.close();
  const genplusgx::ApplicationPaths blockedPaths{blockedRoot};
  if (!check(blockedPaths.initialize().error ==
          genplusgx::PersistenceError::directoryCreationFailed,
        "A file-backed application root was not rejected")) {
    return 13;
  }
  const genplusgx::ApplicationPaths relativePaths{"relative-app-data"};
  if (!check(relativePaths.initialize().error == genplusgx::PersistenceError::invalidRoot,
        "A current-directory-relative application root was accepted")) {
    return 14;
  }

  return 0;
}
