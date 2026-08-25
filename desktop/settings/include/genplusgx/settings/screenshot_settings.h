#pragma once

#include "genplusgx/persistence.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace genplusgx::settings {

struct ScreenshotSettings final {
  std::filesystem::path directory;

  [[nodiscard]] bool operator==(const ScreenshotSettings&) const = default;
};

[[nodiscard]] bool validateScreenshotSettings(
  const ScreenshotSettings& settings) noexcept;

struct ScreenshotSettingsLoadResult final {
  PersistenceStatus status;
  ScreenshotSettings settings;
  bool migrated{false};
};

class ScreenshotSettingsStore final {
public:
  static constexpr std::uint32_t schemaVersion = 1U;
  static constexpr std::size_t maximumFileBytes = 16U * 1024U;

  ScreenshotSettingsStore(
    std::filesystem::path path, std::filesystem::path defaultDirectory);

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] const std::filesystem::path& defaultDirectory() const noexcept;
  [[nodiscard]] ScreenshotSettingsLoadResult load() const;
  [[nodiscard]] PersistenceStatus save(const ScreenshotSettings& settings) const;

private:
  std::filesystem::path path_;
  std::filesystem::path defaultDirectory_;
};

} // namespace genplusgx::settings
