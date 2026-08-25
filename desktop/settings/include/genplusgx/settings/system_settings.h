#pragma once

#include "genplusgx/core_system_settings.h"
#include "genplusgx/persistence.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace genplusgx::settings {

struct SystemSettingsLoadResult final {
  PersistenceStatus status;
  CoreSystemSettings settings;
  bool migrated{false};
};

class SystemSettingsStore final {
public:
  static constexpr std::uint32_t schemaVersion = 1U;
  static constexpr std::size_t maximumFileBytes = 32U * 1024U;

  explicit SystemSettingsStore(std::filesystem::path path);

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] SystemSettingsLoadResult load() const;
  [[nodiscard]] PersistenceStatus save(const CoreSystemSettings& settings) const;

private:
  std::filesystem::path path_;
};

} // namespace genplusgx::settings
