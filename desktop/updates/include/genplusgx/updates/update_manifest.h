#pragma once

#include "genplusgx/updates/update_types.h"

#include <cstdint>
#include <span>
#include <string>

namespace genplusgx::updates {

inline constexpr std::size_t maximumManifestBytes = 256U * 1024U;
inline constexpr std::uint64_t maximumPackageBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;

struct ManifestResult final {
  Status status;
  Manifest manifest;
};

[[nodiscard]] ManifestResult verifyAndParseManifest(
  std::span<const std::uint8_t> manifestBytes,
  std::span<const std::uint8_t> signatureFileBytes,
  const Trust& trust);
[[nodiscard]] Status validateTrustedUrl(
  const std::string& url,
  const Trust& trust,
  bool allowQuery = false);
[[nodiscard]] std::optional<Asset> selectAsset(
  const Manifest& manifest,
  std::string_view platform,
  std::string_view architecture);

} // namespace genplusgx::updates
