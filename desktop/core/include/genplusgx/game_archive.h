#pragma once

#include "genplusgx/game_file.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace genplusgx {

inline constexpr std::size_t maximumZipArchiveBytes =
  512U * 1024U * 1024U;
inline constexpr std::size_t maximumZipEntries = 4'096U;
inline constexpr std::size_t maximumZipEntryNameBytes = 512U;
inline constexpr std::size_t maximumArchivedGameBytes =
  32U * 1024U * 1024U;
inline constexpr std::uint64_t maximumZipCompressionRatio = 1'000U;

struct ArchivedGameEntry final {
  std::string name;
  std::uint64_t compressedSize{0U};
  std::uint64_t uncompressedSize{0U};
  std::uint32_t crc32{0U};

  [[nodiscard]] bool operator==(const ArchivedGameEntry&) const = default;
};

struct ZipArchiveInspection final {
  GameFileStatus status;
  std::vector<ArchivedGameEntry> entries;
};

struct ZipExtractionResult final {
  GameFileStatus status;
  std::filesystem::path path;
};

[[nodiscard]] bool hasZipArchiveExtension(
  const std::filesystem::path& path) noexcept;
[[nodiscard]] ZipArchiveInspection inspectZipArchive(
  const std::filesystem::path& path);
[[nodiscard]] ZipExtractionResult extractZipGame(
  const std::filesystem::path& archivePath,
  std::string_view entryName,
  const std::filesystem::path& cacheDirectory);

} // namespace genplusgx
