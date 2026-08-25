#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

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
};

struct GameFileStatus final {
  GameFileError error{GameFileError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept { return error == GameFileError::none; }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

inline constexpr std::size_t maximumCorePathBytes = 255U;

[[nodiscard]] std::span<const std::string_view> supportedGameExtensions() noexcept;
[[nodiscard]] bool hasSupportedGameExtension(const std::filesystem::path& path);
[[nodiscard]] std::span<const std::string_view> supportedDiscExtensions() noexcept;
[[nodiscard]] bool hasSupportedDiscExtension(const std::filesystem::path& path);
[[nodiscard]] GameFileStatus validateGameFile(const std::filesystem::path& path);
[[nodiscard]] GameFileStatus validateDiscImageFile(
  const std::filesystem::path& path);

} // namespace genplusgx
