#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace genplusgx::library {

inline constexpr std::size_t maximumOnlineMetadataBytes = 512U * 1024U;
inline constexpr std::size_t maximumOnlineIndexBytes = 8U * 1024U * 1024U;
inline constexpr std::size_t maximumOnlineArtworkBytes = 8U * 1024U * 1024U;

enum class OnlineMetadataError : std::uint8_t {
  none,
  disabled,
  unsupportedSystem,
  invalidSettings,
  invalidRequest,
  invalidResponse,
  notFound,
  dataTooLarge,
  transportFailed,
  cacheFailed,
  queueFull,
  notRunning,
  cancelled,
  threadFailure,
};

struct OnlineMetadataStatus final {
  OnlineMetadataError error{OnlineMetadataError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == OnlineMetadataError::none;
  }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

struct OnlineAttribution final {
  std::string creator;
  std::string licenseSpdx;
  std::string licenseUrl;
  std::string sourceUrl;

  [[nodiscard]] bool operator==(const OnlineAttribution&) const = default;
};

struct OnlineArtworkOffer final {
  std::string url;
  std::string sha256;
  std::string mimeType;
  OnlineAttribution attribution;

  [[nodiscard]] bool operator==(const OnlineArtworkOffer&) const = default;
};

struct OnlineMetadataRecord final {
  std::string lookupSha256;
  std::string providerName;
  std::string providerHomepage;
  std::string preferredTitle;
  std::string alternateTitle;
  std::string description;
  std::string releaseDate;
  std::string developer;
  std::string publisher;
  std::vector<std::string> genres;
  OnlineAttribution attribution;
  std::optional<OnlineArtworkOffer> artwork;

  [[nodiscard]] bool operator==(const OnlineMetadataRecord&) const = default;
};

struct OnlineMetadataDecodeResult final {
  OnlineMetadataStatus status;
  OnlineMetadataRecord record;
};

[[nodiscard]] bool isApprovedContentLicense(
  const std::string& spdx) noexcept;
[[nodiscard]] OnlineMetadataStatus validateOnlineMetadataRecord(
  const OnlineMetadataRecord& record) noexcept;
[[nodiscard]] std::vector<std::uint8_t> encodeOnlineMetadataRecord(
  const OnlineMetadataRecord& record);
[[nodiscard]] OnlineMetadataDecodeResult decodeOnlineMetadataRecord(
  std::span<const std::uint8_t> data);

} // namespace genplusgx::library
