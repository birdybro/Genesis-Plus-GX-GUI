#pragma once

#include "genplusgx/persistence.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace genplusgx::settings {

enum class ThemeMode : std::uint8_t {
  system,
  light,
  dark,
};

struct AppearanceSettings final {
  ThemeMode theme{ThemeMode::system};
  std::string language{"system"};
  bool developerToolsEnabled{false};

  [[nodiscard]] bool operator==(const AppearanceSettings&) const = default;
};

[[nodiscard]] AppearanceSettings defaultAppearanceSettings() noexcept;
[[nodiscard]] bool validateAppearanceSettings(
  const AppearanceSettings& settings) noexcept;

struct AppearanceSettingsLoadResult final {
  PersistenceStatus status;
  AppearanceSettings settings;
  bool migrated{false};
};

class AppearanceSettingsStore final {
public:
  static constexpr std::uint32_t schemaVersion = 3U;
  static constexpr std::size_t maximumFileBytes = 16U * 1024U;

  explicit AppearanceSettingsStore(std::filesystem::path path);

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] AppearanceSettingsLoadResult load() const;
  [[nodiscard]] PersistenceStatus save(const AppearanceSettings& settings) const;

private:
  std::filesystem::path path_;
};

} // namespace genplusgx::settings
