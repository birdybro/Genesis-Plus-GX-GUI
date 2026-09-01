#pragma once

#include "genplusgx/persistence.h"
#include "genplusgx/timing/speed_configuration.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace genplusgx::settings {

[[nodiscard]] EmulationSpeedConfiguration defaultSpeedSettings() noexcept;

struct SpeedSettingsLoadResult final {
  PersistenceStatus status;
  EmulationSpeedConfiguration settings;
  bool migrated{false};
};

class SpeedSettingsStore final {
public:
  static constexpr std::uint32_t schemaVersion = 1U;
  static constexpr std::size_t maximumFileBytes = 32U * 1024U;

  explicit SpeedSettingsStore(std::filesystem::path path);

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] SpeedSettingsLoadResult load() const;
  [[nodiscard]] PersistenceStatus save(
    const EmulationSpeedConfiguration& settings) const;

private:
  std::filesystem::path path_;
};

} // namespace genplusgx::settings
