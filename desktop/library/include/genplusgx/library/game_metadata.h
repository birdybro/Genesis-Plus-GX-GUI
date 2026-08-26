#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace genplusgx::library {

enum class GameSystem : std::uint8_t {
  unknown,
  sg1000,
  masterSystem,
  gameGear,
  genesis,
  segaCd,
};

[[nodiscard]] std::string_view gameSystemName(GameSystem system) noexcept;

enum class GameMetadataError : std::uint8_t {
  none,
  invalidPath,
  unsupportedFile,
  fileTooLarge,
  openFailed,
  readFailed,
  cancelled,
};

struct GameMetadataStatus final {
  GameMetadataError error{GameMetadataError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == GameMetadataError::none;
  }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

struct GameMetadata final {
  std::filesystem::path path;
  std::filesystem::path relatedDataPath;
  std::uintmax_t fileSize{0};
  GameSystem system{GameSystem::unknown};
  std::string format;
  std::string domesticTitle;
  std::string internationalTitle;
  std::string copyright;
  std::string productCode;
  std::string region;
  std::string romType;
  std::string peripheralSupport;
  std::string mapper;
  std::string sha256;
  std::string notes;
  std::optional<std::uint16_t> headerChecksum;
  std::optional<std::uint16_t> computedChecksum;
  std::optional<std::uint64_t> declaredRomSize;
  std::uint32_t trackCount{0};
  bool headerRecognized{false};

  [[nodiscard]] std::string displayTitle() const;
};

struct GameMetadataResult final {
  GameMetadataStatus status;
  GameMetadata metadata;
};

inline constexpr std::size_t maximumMetadataHeaderBytes = 64U * 1024U;
inline constexpr std::uintmax_t maximumMetadataFileBytes =
  16ULL * 1024ULL * 1024ULL * 1024ULL;

[[nodiscard]] GameMetadataResult parseGameMetadataBytes(
  std::span<const std::uint8_t> data,
  std::string_view extension,
  std::uintmax_t fileSize = 0U);
[[nodiscard]] GameMetadataResult readGameMetadata(
  const std::filesystem::path& path,
  const std::function<bool()>& cancellationRequested = {});

} // namespace genplusgx::library
