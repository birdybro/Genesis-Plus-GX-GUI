#pragma once

#include "genplusgx/persistence.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace genplusgx::updates {

struct Settings final {
  bool automaticChecks{false};
  std::string lastCheckUtc;
  std::string highestSeenVersion;
  [[nodiscard]] bool operator==(const Settings&) const = default;
};

[[nodiscard]] Settings defaultSettings() noexcept;
[[nodiscard]] bool validateSettings(const Settings& settings) noexcept;
[[nodiscard]] bool automaticCheckDue(
  const Settings& settings, const std::string& nowUtc) noexcept;

struct SettingsLoadResult final {
  PersistenceStatus status;
  Settings settings;
  bool migrated{false};
};

class SettingsStore final {
public:
  static constexpr std::uint32_t schemaVersion = 1U;
  static constexpr std::size_t maximumFileBytes = 16U * 1024U;

  explicit SettingsStore(std::filesystem::path path);
  [[nodiscard]] SettingsLoadResult load() const;
  [[nodiscard]] PersistenceStatus save(const Settings& settings) const;
  [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
  std::filesystem::path path_;
};

} // namespace genplusgx::updates
