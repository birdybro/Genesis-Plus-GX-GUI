#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace genplusgx {

enum class GamePatchFormat { ips, bps, ups };

enum class GamePatchError {
  none,
  emptyPath,
  unsupportedFormat,
  notFound,
  unreadable,
  fileTooLarge,
  invalidPatch,
  sourceMismatch,
  checksumMismatch,
  outputTooLarge,
  ambiguousSidecar,
  unwritableCache,
  pathTooLong,
};

struct GamePatchStatus final {
  GamePatchError error{GamePatchError::none};
  std::string message;
  [[nodiscard]] bool ok() const noexcept
  {
    return error == GamePatchError::none;
  }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

struct GamePatchDataResult final {
  GamePatchStatus status;
  std::vector<std::uint8_t> data;
  GamePatchFormat format{GamePatchFormat::ips};
};

struct GamePatchFileResult final {
  GamePatchStatus status;
  std::filesystem::path path;
  GamePatchFormat format{GamePatchFormat::ips};
};

struct GamePatchDiscoveryResult final {
  GamePatchStatus status;
  std::optional<std::filesystem::path> path;
};

inline constexpr std::size_t maximumGamePatchBytes = 64U * 1024U * 1024U;
inline constexpr std::size_t maximumPatchedGameBytes = 32U * 1024U * 1024U;

[[nodiscard]] std::span<const std::string_view>
supportedGamePatchExtensions() noexcept;
[[nodiscard]] bool hasSupportedGamePatchExtension(
  const std::filesystem::path& path) noexcept;
[[nodiscard]] std::string_view gamePatchFormatName(
  GamePatchFormat format) noexcept;
[[nodiscard]] GamePatchDiscoveryResult discoverGamePatchSidecar(
  const std::filesystem::path& gamePath);
[[nodiscard]] GamePatchDataResult applyGamePatch(
  std::span<const std::uint8_t> source,
  std::span<const std::uint8_t> patch,
  const std::filesystem::path& patchPath);
[[nodiscard]] GamePatchFileResult applyGamePatchFile(
  const std::filesystem::path& sourcePath,
  const std::filesystem::path& patchPath,
  const std::filesystem::path& cacheDirectory);

} // namespace genplusgx
