#include "genplusgx/game_file.h"
#include "genplusgx/game_archive.h"

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

bool corruptFirstStoredZipPayload(const std::filesystem::path& path)
{
  std::fstream stream(path, std::ios::binary | std::ios::in | std::ios::out);
  std::array<unsigned char, 30U> header{};
  stream.read(reinterpret_cast<char*>(header.data()),
    static_cast<std::streamsize>(header.size()));
  if (!stream || header[0] != 0x50U || header[1] != 0x4bU ||
      header[2] != 0x03U || header[3] != 0x04U) {
    return false;
  }
  const auto filenameBytes = static_cast<std::uint16_t>(header[26]) |
    (static_cast<std::uint16_t>(header[27]) << 8U);
  const auto extraBytes = static_cast<std::uint16_t>(header[28]) |
    (static_cast<std::uint16_t>(header[29]) << 8U);
  const auto payloadOffset = static_cast<std::streamoff>(header.size()) +
    filenameBytes + extraBytes;
  stream.seekg(payloadOffset);
  char byte{};
  stream.get(byte);
  if (!stream) {
    return false;
  }
  byte = static_cast<char>(static_cast<unsigned char>(byte) ^ 0x5aU);
  stream.seekp(payloadOffset);
  stream.put(byte);
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
    std::ranges::find(extensions, ".zip") != extensions.end() &&
      std::ranges::find(extensions, ".m3u") != extensions.end() &&
      std::ranges::find(extensions, ".m3u8") != extensions.end(),
    "ZIP archives and M3U disc playlists are advertised by the desktop resolver");
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
    std::filesystem::temp_directory_path() / "genplusgx-game.7z");
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

  const auto secondDiscPath = cueDirectory.path() / "disc-two.iso";
  auto secondDiscBytes = discBytes;
  secondDiscBytes.back() = 0x5aU;
  const auto playlistPath = cueDirectory.path() / "two-disc.m3u";
  constexpr std::string_view playlistText{
    "#EXTM3U\n"
    "track01.bin\n"
    "disc-two.iso\n"};
  genplusgx::DiscPlaylistInfo playlist;
  passed &= check(writeBytes(secondDiscPath, secondDiscBytes) &&
      writeText(playlistPath, playlistText) &&
      genplusgx::validateDiscPlaylistFile(playlistPath, playlist) &&
      playlist.discs.size() == 2U &&
      playlist.discs.front() == canonicalDataPath &&
      playlist.discs.back() == std::filesystem::weakly_canonical(secondDiscPath),
    "a UTF-8 M3U playlist resolves an ordered bounded local disc set");
  const auto playlistFiles = genplusgx::gameContentFiles(playlistPath);
  passed &= check(playlistFiles.status && playlistFiles.files.size() == 3U &&
      playlistFiles.files.front() == playlistPath,
    "playlist content identity includes the M3U and every resolved disc");

  constexpr std::array unsafePlaylists{
    std::string_view{"../outside.iso\n"},
    std::string_view{"/tmp/outside.iso\n"},
    std::string_view{"C:\\outside.iso\n"},
    std::string_view{"https://example.invalid/disc.iso\n"},
  };
  for (const auto text : unsafePlaylists) {
    genplusgx::DiscPlaylistInfo rejected;
    passed &= check(genplusgx::validateDiscPlaylistText(
          text, cueDirectory.path(), rejected).error ==
          genplusgx::GameFileError::unsafePlaylistReference,
      "M3U absolute, parent, drive, and URL references are rejected");
  }
  genplusgx::DiscPlaylistInfo duplicatePlaylist;
  passed &= check(genplusgx::validateDiscPlaylistText(
        "track01.bin\ntrack01.bin\n", cueDirectory.path(),
        duplicatePlaylist).error == genplusgx::GameFileError::invalidDiscPlaylist,
    "duplicate M3U disc entries are rejected deterministically");
  genplusgx::DiscPlaylistInfo missingPlaylist;
  passed &= check(genplusgx::validateDiscPlaylistText(
        "missing.iso\n", cueDirectory.path(), missingPlaylist).error ==
        genplusgx::GameFileError::missingPlaylistDisc,
    "a missing M3U disc reports a descriptive typed error");
  std::string invalidUtf8Playlist{"track"};
  invalidUtf8Playlist.push_back(static_cast<char>(0xffU));
  invalidUtf8Playlist += ".iso\n";
  passed &= check(genplusgx::validateDiscPlaylistText(
        invalidUtf8Playlist, cueDirectory.path(), missingPlaylist).error ==
        genplusgx::GameFileError::invalidDiscPlaylist,
    "invalid UTF-8 in an M3U playlist is rejected before path conversion");
  const std::string overlongPlaylistLine(
    genplusgx::maximumDiscPlaylistLineBytes + 1U, 'a');
  passed &= check(genplusgx::validateDiscPlaylistText(
        overlongPlaylistLine, cueDirectory.path(), missingPlaylist).error ==
        genplusgx::GameFileError::invalidDiscPlaylist,
    "an M3U line over the bounded parser limit is rejected");
  std::string excessiveDiscPlaylist;
  const std::array<std::uint8_t, 1U> tinyDisc{0U};
  for (std::size_t index = 0U;
       index <= genplusgx::maximumDiscPlaylistEntries; ++index) {
    const auto name = "bounded-disc-" + std::to_string(index) + ".iso";
    passed &= check(writeBytes(cueDirectory.path() / name, tinyDisc),
      "an M3U entry-limit fixture is generated locally");
    excessiveDiscPlaylist += name + '\n';
  }
  passed &= check(genplusgx::validateDiscPlaylistText(
        excessiveDiscPlaylist, cueDirectory.path(), missingPlaylist).error ==
        genplusgx::GameFileError::invalidDiscPlaylist,
    "an M3U playlist over the 32-disc limit is rejected");

  const auto zipPath = cueDirectory.path() / "collection.zip";
  const auto otherRom = genplusgx::test::makeZ80RamMarkerRom();
  passed &= check(genplusgx::test::writeZipFixture(zipPath, {
        {.name = "Genesis/Test Game.md", .data = genplusgx::test::makeGenesisRamMarkerRom()},
        {.name = "SMS/Other Game.sms", .data = otherRom},
        {.name = "notes/readme.txt", .data = {1U, 2U, 3U}},
      }),
    "a deterministic deflated ZIP fixture is generated locally");
  const auto archive = genplusgx::inspectZipArchive(zipPath);
  passed &= check(archive.status && archive.entries.size() == 2U &&
      archive.entries[0].name == "Genesis/Test Game.md" &&
      archive.entries[1].name == "SMS/Other Game.sms" &&
      genplusgx::validateGameFile(zipPath),
    "ZIP inspection returns only sorted supported cartridge entries");
  const auto cachePath = cueDirectory.path() / "cache";
  const auto extracted = genplusgx::extractZipGame(
    zipPath, "SMS/Other Game.sms", cachePath);
  passed &= check(extracted.status &&
      genplusgx::validateGameFile(extracted.path) &&
      std::filesystem::file_size(extracted.path) == otherRom.size(),
    "a selected deflated ZIP entry extracts to a core-loadable bounded cache file");
  const auto extractedAgain = genplusgx::extractZipGame(
    zipPath, "SMS/Other Game.sms", cachePath);
  passed &= check(extractedAgain.status && extractedAgain.path == extracted.path,
    "identical archive extraction reuses the collision-safe content cache");
  passed &= check(genplusgx::extractZipGame(
        zipPath, "missing.md", cachePath).status.error ==
        genplusgx::GameFileError::archiveEntryNotFound,
    "a stale or forged ZIP member selection is rejected");
  passed &= check(genplusgx::extractZipGame(
        zipPath, "SMS/Other Game.sms", "relative-cache").status.error ==
        genplusgx::GameFileError::unwritableCache,
    "archive extraction rejects a relative cache root");

  const auto storedZipPath = cueDirectory.path() / "stored.zip";
  const bool storedZipWritten = genplusgx::test::writeZipFixture(storedZipPath, {
        {.name = "Stored.md", .data = genplusgx::test::makeGenesisRamMarkerRom()},
      }, false);
  const auto storedArchive = genplusgx::inspectZipArchive(storedZipPath);
  passed &= check(storedZipWritten && storedArchive.status,
    "stored ZIP entries are supported alongside deflate entries");
  const auto corruptPayloadZipPath = cueDirectory.path() / "corrupt-payload.zip";
  const bool corruptPayloadWritten = genplusgx::test::writeZipFixture(
    corruptPayloadZipPath, {{.name = "Corrupt.md",
      .data = genplusgx::test::makeGenesisRamMarkerRom()}}, false);
  passed &= check(corruptPayloadWritten &&
      corruptFirstStoredZipPayload(corruptPayloadZipPath) &&
      genplusgx::inspectZipArchive(corruptPayloadZipPath).status &&
      genplusgx::extractZipGame(corruptPayloadZipPath, "Corrupt.md",
        cachePath).status.error == genplusgx::GameFileError::invalidArchive,
    "archive extraction rejects a payload that fails its central-directory CRC");
  const auto ratioZipPath = cueDirectory.path() / "excessive-ratio.zip";
  const bool ratioZipWritten = genplusgx::test::writeZipFixture(ratioZipPath,
    {{.name = "Highly Compressible.md",
      .data = std::vector<std::uint8_t>(4U * 1024U * 1024U, 0U)}});
  passed &= check(ratioZipWritten &&
      genplusgx::inspectZipArchive(ratioZipPath).status.error ==
        genplusgx::GameFileError::unsafeArchiveEntry,
    "a ZIP bomb-like compression ratio is rejected during inspection");
  const auto unsafeZipPath = cueDirectory.path() / "unsafe.zip";
  const bool unsafeZipWritten = genplusgx::test::writeZipFixture(unsafeZipPath, {
        {.name = "../escape.md", .data = genplusgx::test::makeGenesisRamMarkerRom()},
      });
  const auto unsafeArchive = genplusgx::inspectZipArchive(unsafeZipPath);
  passed &= check(unsafeZipWritten && unsafeArchive.status.error ==
        genplusgx::GameFileError::unsafeArchiveEntry,
    "archive traversal names are rejected even though extraction flattens names");
  const auto noGamesZipPath = cueDirectory.path() / "documents.zip";
  const bool noGamesZipWritten = genplusgx::test::writeZipFixture(noGamesZipPath, {
        {.name = "readme.txt", .data = {1U}},
      });
  const auto noGamesArchive = genplusgx::inspectZipArchive(noGamesZipPath);
  passed &= check(noGamesZipWritten && noGamesArchive.status.error ==
        genplusgx::GameFileError::archiveHasNoGames,
    "an archive without a compatible game reports a descriptive typed error");
  genplusgx::test::TemporaryFixture corruptZip{{0x50U, 0x4bU, 0x03U, 0x04U}, ".zip"};
  passed &= check(genplusgx::inspectZipArchive(corruptZip.path()).status.error ==
      genplusgx::GameFileError::invalidArchive,
    "a truncated ZIP file is rejected without reading untrusted offsets");

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
    genplusgx::DiscPlaylistInfo fuzzPlaylist;
    const auto playlistResult = genplusgx::validateDiscPlaylistText(
      fuzzInput, cueDirectory.path(), fuzzPlaylist);
    passed &= check(!playlistResult ||
        (!fuzzPlaylist.discs.empty() &&
          fuzzPlaylist.discs.size() <= genplusgx::maximumDiscPlaylistEntries),
      "bounded deterministic M3U fuzzing returned a malformed success result");
  }

  return passed ? 0 : 1;
}
