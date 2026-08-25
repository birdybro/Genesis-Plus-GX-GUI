#include "genplusgx/game_file.h"

#include "synthetic_rom.h"

#include <algorithm>
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
  bool passed = true;
  const auto extensions = genplusgx::supportedGameExtensions();
  passed &= check(!extensions.empty(), "the supported extension list is populated");
  passed &= check(
    std::ranges::find(extensions, ".md") != extensions.end(),
    "Genesis ROM extension is advertised");
  passed &= check(
    std::ranges::find(extensions, ".sms") != extensions.end(),
    "Master System ROM extension is advertised");
  passed &= check(
    std::ranges::find(extensions, ".gg") != extensions.end(),
    "Game Gear ROM extension is advertised");
  passed &= check(
    std::ranges::find(extensions, ".sg") != extensions.end(),
    "SG-1000 ROM extension is advertised");
  passed &= check(
    std::ranges::find(extensions, ".cue") != extensions.end(),
    "Sega CD cue extension is advertised");
#if defined(GENPLUSGX_HAVE_CHD)
  passed &= check(
    std::ranges::find(extensions, ".chd") != extensions.end(),
    "CHD is advertised when the decoder is enabled");
#endif
  passed &= check(
    std::ranges::find(extensions, ".zip") == extensions.end(),
    "ZIP is not advertised by the raw desktop loader");
  const auto discExtensions = genplusgx::supportedDiscExtensions();
  passed &= check(
    std::ranges::find(discExtensions, ".cue") != discExtensions.end() &&
      std::ranges::find(discExtensions, ".iso") != discExtensions.end() &&
      std::ranges::find(discExtensions, ".md") == discExtensions.end(),
    "the disc replacement list is restricted to supported image containers");

  genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".MD"};
  const auto valid = genplusgx::validateGameFile(fixture.path());
  passed &= check(valid.ok(), "an existing uppercase-extension ROM validates");
  passed &= check(
    genplusgx::hasSupportedGameExtension(fixture.path()),
    "extension checks are case insensitive");
  passed &= check(
    genplusgx::validateDiscImageFile(fixture.path()).error ==
      genplusgx::GameFileError::unsupportedDiscExtension,
    "a cartridge-only extension is rejected by the disc validator");
  genplusgx::test::TemporaryFixture discFixture{
    genplusgx::test::makeSegaCdDiscImage(), ".ISO"};
  passed &= check(
    genplusgx::validateDiscImageFile(discFixture.path()),
    "an existing uppercase Sega CD image validates for disc replacement");

  const auto empty = genplusgx::validateGameFile({});
  passed &= check(
    empty.error == genplusgx::GameFileError::emptyPath,
    "an empty path is rejected descriptively");
  const auto missing = genplusgx::validateGameFile(
    std::filesystem::temp_directory_path() / "genplusgx-definitely-missing.md");
  passed &= check(
    missing.error == genplusgx::GameFileError::notFound,
    "a missing supported file is distinguished");
  const auto unsupported = genplusgx::validateGameFile(
    std::filesystem::temp_directory_path() / "genplusgx-game.zip");
  passed &= check(
    unsupported.error == genplusgx::GameFileError::unsupportedExtension,
    "an unsupported extension is rejected before opening");
  const auto directoryPath = std::filesystem::path{fixture.path().string() + "-dir.md"};
  std::filesystem::create_directory(directoryPath);
  const auto directory = genplusgx::validateGameFile(directoryPath);
  passed &= check(
    directory.error == genplusgx::GameFileError::notRegularFile,
    "a directory with a supported suffix is not treated as a game");
  std::filesystem::remove(directoryPath);
  const auto actualDirectory = genplusgx::validateGameFile(
    std::filesystem::temp_directory_path() / ".");
  passed &= check(
    actualDirectory.error == genplusgx::GameFileError::unsupportedExtension,
    "a path without a game extension is rejected");

  std::filesystem::path longPath{"/tmp/"};
  longPath += std::string(genplusgx::maximumCorePathBytes, 'a') + ".md";
  const auto tooLong = genplusgx::validateGameFile(longPath);
  passed &= check(
    tooLong.error == genplusgx::GameFileError::pathTooLong,
    "core-incompatible path lengths are rejected safely");

  return passed ? 0 : 1;
}
