#include "genplusgx/game_file.h"

#include "synthetic_rom.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool check(bool condition, std::string_view message)
{
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

class TemporaryDirectory final {
public:
  TemporaryDirectory()
    : anchor_({0U}, ".tmp"),
      path_(anchor_.path().string() + "-cue-directory")
  {
    std::error_code error;
    if (!std::filesystem::create_directory(path_, error) || error) {
      throw std::runtime_error{"Unable to create CUE test directory"};
    }
  }

  ~TemporaryDirectory()
  {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept
  {
    return path_;
  }

private:
  genplusgx::test::TemporaryFixture anchor_;
  std::filesystem::path path_;
};

bool writeText(const std::filesystem::path& path, std::string_view text)
{
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  return static_cast<bool>(stream);
}

bool writeBytes(const std::filesystem::path& path,
  std::span<const std::uint8_t> bytes)
{
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(bytes.data()),
    static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(stream);
}

bool rejectsCue(std::string_view text, genplusgx::GameFileError error)
{
  genplusgx::CueSheetInfo information;
  return genplusgx::validateCueSheetText(text, information).error == error;
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

  constexpr std::string_view validCue{
    "REM generated legal test fixture\n"
    "FILE \"track01.bin\" BINARY\n"
    "  TRACK 01 MODE1/2048\n"
    "    INDEX 01 00:00:00\n"
    "FILE \"track 02.wav\" WAVE\n"
    "  TRACK 02 AUDIO\n"
    "    PREGAP 00:02:00\n"
    "    INDEX 01 00:00:00\n"};
  genplusgx::CueSheetInfo cueInformation;
  passed &= check(
    genplusgx::validateCueSheetText(validCue, cueInformation) &&
      cueInformation.trackCount == 2U &&
      cueInformation.referencedFiles.size() == 2U &&
      cueInformation.referencedFiles[1] == "track 02.wav",
    "a bounded multi-file CUE sheet parses into deterministic references");

  constexpr std::array invalidCueSheets{
    std::pair{std::string_view{"FILE \"unterminated.bin BINARY\n"},
      genplusgx::GameFileError::invalidCueSheet},
    std::pair{std::string_view{
      "FILEX \"disc.bin\" BINARY\nFILE \"disc.bin\" BINARY\n"
      "TRACK 01 MODE1/2048\nINDEX 01 00:00:00\n"},
      genplusgx::GameFileError::invalidCueSheet},
    std::pair{std::string_view{"TRACK 01 MODE1/2048\nINDEX 01 00:00:00\n"},
      genplusgx::GameFileError::invalidCueSheet},
    std::pair{std::string_view{
      "FILE \"disc.bin\" BINARY\nTRACK 02 MODE1/2048\nINDEX 01 00:00:00\n"},
      genplusgx::GameFileError::invalidCueSheet},
    std::pair{std::string_view{
      "FILE \"disc.bin\" BINARY\nTRACK 01 MODE1/2048\nINDEX 01 00:60:00\n"},
      genplusgx::GameFileError::invalidCueSheet},
    std::pair{std::string_view{
      "FILE \"disc.bin\" BINARY\nTRACK 01 MODE1/2048\nTRACK 02 AUDIO\nINDEX 01 00:00:00\n"},
      genplusgx::GameFileError::invalidCueSheet},
    std::pair{std::string_view{
      "FILE \"disc.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n"},
      genplusgx::GameFileError::invalidCueSheet},
    std::pair{std::string_view{
      "FILE \"disc.bin\" BINARY\nTRACK 01 MODE1/2048\nINDEX 01 00:00:00\n"
      "TRACK 02 MODE2/2352\nINDEX 01 00:02:00\n"},
      genplusgx::GameFileError::invalidCueSheet},
    std::pair{std::string_view{
      "FILE \"../disc.bin\" BINARY\nTRACK 01 MODE1/2048\nINDEX 01 00:00:00\n"},
      genplusgx::GameFileError::unsafeCueReference},
    std::pair{std::string_view{
      "FILE \"..\\disc.bin\" BINARY\nTRACK 01 MODE1/2048\nINDEX 01 00:00:00\n"},
      genplusgx::GameFileError::unsafeCueReference},
    std::pair{std::string_view{
      "FILE \"/tmp/disc.bin\" BINARY\nTRACK 01 MODE1/2048\nINDEX 01 00:00:00\n"},
      genplusgx::GameFileError::unsafeCueReference},
    std::pair{std::string_view{
      "FILE \"C:\\disc.bin\" BINARY\nTRACK 01 MODE1/2048\nINDEX 01 00:00:00\n"},
      genplusgx::GameFileError::unsafeCueReference},
  };
  for (const auto& [text, expectedError] : invalidCueSheets) {
    passed &= check(rejectsCue(text, expectedError),
      "a malformed or unsafe CUE construct was rejected with its typed error");
  }

  std::string controlCharacterCue{
    "FILE \"disc.bin\" BINARY\nTRACK 01 MODE1/2048\nINDEX 01 00:00:00\n"};
  controlCharacterCue.insert(4U, 1U, '\0');
  passed &= check(rejectsCue(controlCharacterCue,
      genplusgx::GameFileError::invalidCueSheet),
    "embedded binary control characters are rejected");
  const std::string oversizedLine(genplusgx::maximumCueLineBytes + 1U, 'A');
  passed &= check(rejectsCue(oversizedLine,
      genplusgx::GameFileError::invalidCueSheet),
    "a logical line longer than the inherited parser buffer is rejected");

  TemporaryDirectory cueDirectory;
  const auto dataPath = cueDirectory.path() / "track01.bin";
  const auto cuePath = cueDirectory.path() / "disc.cue";
  const auto discBytes = genplusgx::test::makeSegaCdDiscImage();
  constexpr std::string_view fileCue{
    "FILE \"track01.bin\" BINARY\n"
    "  TRACK 01 MODE1/2048\n"
    "    INDEX 01 00:00:00\n"};
  passed &= check(writeBytes(dataPath, discBytes) && writeText(cuePath, fileCue) &&
      genplusgx::validateGameFile(cuePath) &&
      genplusgx::validateDiscImageFile(cuePath),
    "a local, readable CUE/BIN pair passes game and disc preflight");
  const auto cueFiles = genplusgx::gameContentFiles(cuePath);
  std::error_code canonicalError;
  const auto canonicalDataPath = std::filesystem::weakly_canonical(
    dataPath, canonicalError);
  passed &= check(cueFiles.status && !canonicalError &&
      cueFiles.files.size() == 2U && cueFiles.files.front() == cuePath &&
      cueFiles.files.back() == canonicalDataPath,
    "validated CUE content enumeration includes the sheet and resolved tracks");
  const auto cartridgeFiles = genplusgx::gameContentFiles(fixture.path());
  passed &= check(cartridgeFiles.status && cartridgeFiles.files.size() == 1U &&
      cartridgeFiles.files.front() == fixture.path(),
    "single-file games retain their original content boundary");

  const auto missingCuePath = cueDirectory.path() / "missing.cue";
  constexpr std::string_view missingCue{
    "FILE \"missing.bin\" BINARY\nTRACK 01 MODE1/2048\nINDEX 01 00:00:00\n"};
  passed &= check(writeText(missingCuePath, missingCue) &&
      genplusgx::validateGameFile(missingCuePath).error ==
        genplusgx::GameFileError::missingCueTrackFile,
    "a CUE sheet cannot refer to a missing track file");
  passed &= check(
    genplusgx::gameContentFiles(missingCuePath).status.error ==
      genplusgx::GameFileError::missingCueTrackFile,
    "content enumeration preserves typed CUE validation failures");

  const auto emptyDataPath = cueDirectory.path() / "empty.bin";
  const auto emptyCuePath = cueDirectory.path() / "empty.cue";
  constexpr std::string_view emptyCue{
    "FILE \"empty.bin\" BINARY\nTRACK 01 MODE1/2048\nINDEX 01 00:00:00\n"};
  passed &= check(writeText(emptyDataPath, {}) && writeText(emptyCuePath, emptyCue) &&
      genplusgx::validateGameFile(emptyCuePath).error ==
        genplusgx::GameFileError::missingCueTrackFile,
    "an empty CUE track file is rejected before the core opens it");

  const auto directoryTrack = cueDirectory.path() / "directory.bin";
  const auto directoryCuePath = cueDirectory.path() / "directory.cue";
  constexpr std::string_view directoryCue{
    "FILE \"directory.bin\" BINARY\nTRACK 01 MODE1/2048\nINDEX 01 00:00:00\n"};
  std::error_code directoryError;
  std::filesystem::create_directory(directoryTrack, directoryError);
  passed &= check(!directoryError && writeText(directoryCuePath, directoryCue) &&
      genplusgx::validateGameFile(directoryCuePath).error ==
        genplusgx::GameFileError::missingCueTrackFile,
    "a directory cannot masquerade as a CUE track file");

  genplusgx::test::TemporaryFixture outsideTrack{discBytes, ".bin"};
  const auto linkPath = cueDirectory.path() / "linked.bin";
  const auto linkCuePath = cueDirectory.path() / "linked.cue";
  constexpr std::string_view linkCue{
    "FILE \"linked.bin\" BINARY\nTRACK 01 MODE1/2048\nINDEX 01 00:00:00\n"};
  std::error_code linkError;
  std::filesystem::create_symlink(outsideTrack.path(), linkPath, linkError);
  if (!linkError) {
    passed &= check(writeText(linkCuePath, linkCue) &&
        genplusgx::validateGameFile(linkCuePath).error ==
          genplusgx::GameFileError::unsafeCueReference,
      "a symlink cannot escape the CUE directory after lexical validation");
  }

  const auto largeCuePath = cueDirectory.path() / "large.cue";
  {
    std::ofstream largeCue(largeCuePath, std::ios::binary | std::ios::trunc);
    largeCue.seekp(static_cast<std::streamoff>(genplusgx::maximumCueSheetBytes));
    largeCue.put('X');
  }
  passed &= check(genplusgx::validateGameFile(largeCuePath).error ==
      genplusgx::GameFileError::fileTooLarge,
    "a CUE sheet larger than the bounded reader limit is rejected");

  std::uint32_t fuzzState = 0xC0FFEEU;
  for (std::size_t iteration = 0U; iteration < 2'000U; ++iteration) {
    fuzzState ^= fuzzState << 13U;
    fuzzState ^= fuzzState >> 17U;
    fuzzState ^= fuzzState << 5U;
    const auto length = static_cast<std::size_t>(fuzzState % 513U);
    std::string fuzzInput(length, '\0');
    for (char& character : fuzzInput) {
      fuzzState ^= fuzzState << 13U;
      fuzzState ^= fuzzState >> 17U;
      fuzzState ^= fuzzState << 5U;
      character = static_cast<char>(fuzzState & 0x7FU);
    }
    genplusgx::CueSheetInfo fuzzInformation;
    const auto fuzzResult = genplusgx::validateCueSheetText(
      fuzzInput, fuzzInformation);
    passed &= check(!fuzzResult ||
        (fuzzInformation.trackCount >= 1U &&
          fuzzInformation.trackCount <= 99U &&
          !fuzzInformation.referencedFiles.empty() &&
          fuzzInformation.referencedFiles.size() <= 99U),
      "bounded deterministic CUE fuzzing returned a malformed success result");
  }

  return passed ? 0 : 1;
}
