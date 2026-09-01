#pragma once

#include "genplusgx/persistence.h"
#include "genplusgx/run_ahead_configuration.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace genplusgx::settings {

[[nodiscard]] RunAheadConfiguration defaultRunAheadSettings() noexcept;

struct RunAheadSettingsLoadResult final {
  PersistenceStatus status;
  RunAheadConfiguration settings;
};

class RunAheadSettingsStore final {
public:
  static constexpr std::uint32_t schemaVersion = 1U;
  static constexpr std::size_t maximumFileBytes = 32U * 1024U;

  explicit RunAheadSettingsStore(std::filesystem::path path);

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] RunAheadSettingsLoadResult load() const;
  [[nodiscard]] PersistenceStatus save(
    const RunAheadConfiguration& settings) const;

private:
  std::filesystem::path path_;
};

} // namespace genplusgx::settings
