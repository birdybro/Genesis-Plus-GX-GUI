#include "genplusgx/core_adapter.h"
#include "genplusgx/game_archive.h"
#include "genplusgx/game_file.h"
#include "genplusgx/persistence.h"

#include "synthetic_rom.h"

#include <QTemporaryDir>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string_view>

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
  stream.write(reinterpret_cast<const char*>(bytes.data()),
    static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(stream);
}

} // namespace

int main()
{
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "temporary workflow root was unavailable")) {
    return 1;
  }
  const auto root = std::filesystem::path{temporary.path().toStdString()};
  const auto archivePath = root / "games.zip";
  const auto genesis = genplusgx::test::makeGenesisRamMarkerRom();
  const auto sms = genplusgx::test::makeZ80RamMarkerRom(0x67U);
  if (!check(genplusgx::test::writeZipFixture(archivePath, {
        {.name = "Genesis/Fixture.md", .data = genesis},
        {.name = "Master System/Fixture.sms", .data = sms},
      }), "deflated archive fixture creation failed")) {
    return 2;
  }
  const auto inspection = genplusgx::inspectZipArchive(archivePath);
  if (!check(inspection.status && inspection.entries.size() == 2U,
        "archive browser did not enumerate both synthetic games")) {
    return 3;
  }
  const auto genesisGame = genplusgx::extractZipGame(
    archivePath, "Genesis/Fixture.md", root / "cache");
  const auto smsGame = genplusgx::extractZipGame(
    archivePath, "Master System/Fixture.sms", root / "cache");
  if (!check(genesisGame.status && smsGame.status,
        "selected archive games did not extract") ||
      !check(genplusgx::identifyGame(genesisGame.path).identity.sha256 !=
          genplusgx::identifyGame(smsGame.path).identity.sha256,
        "different archive members did not receive different content identities")) {
    return 4;
  }

  genplusgx::CoreAdapter adapter;
  if (!check(adapter.initialize(), "core initialization failed") ||
      !check(adapter.loadGame(genesisGame.path) && adapter.runFrame() &&
          adapter.loadedPath() == genesisGame.path && adapter.unloadGame(),
        "selected Genesis archive member did not execute through the core") ||
      !check(adapter.loadGame(smsGame.path) && adapter.runFrame() &&
          adapter.loadedPath() == smsGame.path && adapter.unloadGame(),
        "selected Master System archive member did not execute through the core")) {
    return 5;
  }

  const auto biosPath = root / "test-bios.bin";
  const auto firstDisc = root / "disc-one.iso";
  const auto secondDisc = root / "disc-two.iso";
  const auto playlistPath = root / "multi-disc.m3u";
  auto firstBytes = genplusgx::test::makeSegaCdDiscImage();
  auto secondBytes = firstBytes;
  secondBytes.back() = 0xa5U;
  constexpr std::string_view playlistText{
    "#EXTM3U\n"
    "disc-one.iso\n"
    "disc-two.iso\n"};
  if (!check(writeBytes(biosPath, genplusgx::test::makeSegaCdBios()) &&
        writeBytes(firstDisc, firstBytes) && writeBytes(secondDisc, secondBytes) &&
        writeBytes(playlistPath, std::span<const std::uint8_t>{
          reinterpret_cast<const std::uint8_t*>(playlistText.data()),
          playlistText.size()}),
      "M3U workflow fixture creation failed")) {
    return 6;
  }
  genplusgx::DiscPlaylistInfo playlist;
  if (!check(genplusgx::validateDiscPlaylistFile(playlistPath, playlist) &&
        playlist.discs.size() == 2U,
      "M3U playlist did not resolve its ordered disc set")) {
    return 7;
  }
  genplusgx::CoreFirmwareSettings firmware;
  firmware.segaCdUsa = biosPath;
  genplusgx::CoreDiscInfo disc;
  if (!check(adapter.applyFirmwareSettings(firmware),
        "synthetic Sega CD firmware setting failed") ||
      !check(adapter.loadGame(playlist.discs[0]) && adapter.discInfo(disc) &&
          disc.segaCd && disc.path == playlist.discs[0],
        "first M3U disc did not start a Sega CD session") ||
      !check(adapter.changeDisc(playlist.discs[1]) && adapter.discInfo(disc) &&
          disc.discPresent && !disc.trayOpen && disc.path == playlist.discs[1],
        "M3U disc change did not reach the active core session") ||
      !check(adapter.unloadGame() && adapter.shutdown(),
        "archive and playlist core session did not shut down cleanly")) {
    return 8;
  }
  return 0;
}
