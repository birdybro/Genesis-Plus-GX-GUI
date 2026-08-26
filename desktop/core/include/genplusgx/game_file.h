#pragma once

#include <filesystem>
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

struct CueSheetInfo final {
  std::vector<std::filesystem::path> referencedFiles;
  std::size_t trackCount{0U};
};

[[nodiscard]] std::span<const std::string_view> supportedGameExtensions() noexcept;
[[nodiscard]] bool hasSupportedGameExtension(const std::filesystem::path& path);
[[nodiscard]] std::span<const std::string_view> supportedDiscExtensions() noexcept;
[[nodiscard]] bool hasSupportedDiscExtension(const std::filesystem::path& path);
[[nodiscard]] GameFileStatus validateCueSheetText(
  std::string_view text,
  CueSheetInfo& information);
[[nodiscard]] GameFileStatus validateCueSheetFile(
  const std::filesystem::path& path);
[[nodiscard]] GameFileStatus validateGameFile(const std::filesystem::path& path);
[[nodiscard]] GameFileStatus validateDiscImageFile(
  const std::filesystem::path& path);

} // namespace genplusgx
