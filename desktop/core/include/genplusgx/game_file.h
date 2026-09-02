#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace genplusgx {

enum class GameFileError {
  none,
  emptyPath,
  pathTooLong,
  unsupportedExtension,
  unsupportedDiscExtension,
  notFound,
  notRegularFile,
  unreadable,
  fileTooLarge,
  invalidCueSheet,
  unsafeCueReference,
  missingCueTrackFile,
  invalidDiscPlaylist,
  unsafePlaylistReference,
  missingPlaylistDisc,
  invalidArchive,
  archiveHasNoGames,
  unsafeArchiveEntry,
  encryptedArchiveEntry,
  unsupportedArchiveCompression,
  archiveEntryTooLarge,
  archiveEntryNotFound,
  unwritableCache,
};

struct GameFileStatus final {
  GameFileError error{GameFileError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept { return error == GameFileError::none; }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

inline constexpr std::size_t maximumCorePathBytes = 255U;
inline constexpr std::size_t maximumCueSheetBytes = 1U * 1024U * 1024U;
inline constexpr std::size_t maximumCueLineBytes = 127U;
inline constexpr std::size_t maximumDiscPlaylistBytes = 256U * 1024U;
inline constexpr std::size_t maximumDiscPlaylistLineBytes = 1'024U;
inline constexpr std::size_t maximumDiscPlaylistEntries = 32U;

struct CueSheetInfo final {
  std::vector<std::filesystem::path> referencedFiles;
  std::size_t trackCount{0U};
};

struct GameContentFilesResult final {
  GameFileStatus status;
  std::vector<std::filesystem::path> files;
};

struct DiscPlaylistInfo final {
  std::vector<std::filesystem::path> discs;
};

struct PhysicalMediaLaunch final {
  std::string driveId;
  std::string displayName;
  std::filesystem::path storageDirectory;
  std::string sha256;
  std::uint64_t byteSize{0U};

  [[nodiscard]] bool valid() const noexcept
  {
    return !driveId.empty() && !displayName.empty() &&
      !storageDirectory.empty() && sha256.size() == 64U && byteSize != 0U;
  }
  [[nodiscard]] bool operator==(const PhysicalMediaLaunch&) const = default;
};

struct GameLaunchTarget final {
  std::filesystem::path sourcePath;
  std::filesystem::path runtimePath;
  std::filesystem::path patchPath;
  std::string archiveEntry;
  std::vector<std::filesystem::path> playlistDiscs;
  std::optional<PhysicalMediaLaunch> physicalMedia;

  [[nodiscard]] bool valid() const noexcept
  {
    return !sourcePath.empty() && !runtimePath.empty() &&
      (!physicalMedia || physicalMedia->valid());
  }
  [[nodiscard]] bool isArchive() const noexcept
  {
    return !archiveEntry.empty();
  }
  [[nodiscard]] bool isPlaylist() const noexcept
  {
    return !playlistDiscs.empty();
  }
  [[nodiscard]] bool isPatched() const noexcept
  {
    return !patchPath.empty();
  }
  [[nodiscard]] bool isPhysicalMedia() const noexcept
  {
    return physicalMedia.has_value();
  }
  [[nodiscard]] bool operator==(const GameLaunchTarget&) const = default;
};

[[nodiscard]] std::span<const std::string_view> supportedGameExtensions() noexcept;
[[nodiscard]] bool hasSupportedGameExtension(const std::filesystem::path& path);
[[nodiscard]] std::span<const std::string_view> supportedDiscExtensions() noexcept;
[[nodiscard]] bool hasSupportedDiscExtension(const std::filesystem::path& path);
[[nodiscard]] bool hasDiscPlaylistExtension(
  const std::filesystem::path& path) noexcept;
[[nodiscard]] GameFileStatus validateCueSheetText(
  std::string_view text,
  CueSheetInfo& information);
[[nodiscard]] GameFileStatus validateCueSheetFile(
  const std::filesystem::path& path);
[[nodiscard]] GameFileStatus validateDiscPlaylistText(
  std::string_view text,
  const std::filesystem::path& playlistDirectory,
  DiscPlaylistInfo& information);
[[nodiscard]] GameFileStatus validateDiscPlaylistFile(
  const std::filesystem::path& path,
  DiscPlaylistInfo& information);
[[nodiscard]] GameFileStatus validateGameFile(const std::filesystem::path& path);
[[nodiscard]] GameFileStatus validateDiscImageFile(
  const std::filesystem::path& path);
[[nodiscard]] GameContentFilesResult gameContentFiles(
  const std::filesystem::path& path);

} // namespace genplusgx
