#pragma once

#include "genplusgx/library/online_metadata.h"
#include "genplusgx/persistence.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace genplusgx::library {

enum class OnlineMetadataProvider : std::uint8_t {
  retronian,
  licensedManifest,
};

struct OnlineMetadataSettings final {
  bool enabled{false};
  bool automaticLookup{false};
  bool downloadArtwork{false};
  OnlineMetadataProvider provider{OnlineMetadataProvider::retronian};
  std::string endpoint{"https://gamedb.retronian.com"};
  std::string preferredLanguage{"en"};
  std::string preferredRegion;
  std::uint32_t cacheMegabytes{128U};

  [[nodiscard]] bool operator==(const OnlineMetadataSettings&) const = default;
};

[[nodiscard]] OnlineMetadataSettings defaultOnlineMetadataSettings() noexcept;
[[nodiscard]] OnlineMetadataStatus validateOnlineMetadataSettings(
  const OnlineMetadataSettings& settings) noexcept;
[[nodiscard]] std::string_view onlineMetadataProviderName(
  OnlineMetadataProvider provider) noexcept;

struct OnlineMetadataSettingsLoadResult final {
  PersistenceStatus status;
  OnlineMetadataSettings settings;
  bool migrated{false};
};

class OnlineMetadataSettingsStore final {
public:
  static constexpr std::uint32_t schemaVersion = 1U;
  static constexpr std::size_t maximumFileBytes = 32U * 1024U;

  explicit OnlineMetadataSettingsStore(std::filesystem::path path);

  [[nodiscard]] OnlineMetadataSettingsLoadResult load() const;
  [[nodiscard]] PersistenceStatus save(
    const OnlineMetadataSettings& settings) const;
  [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
  std::filesystem::path path_;
};

} // namespace genplusgx::library
