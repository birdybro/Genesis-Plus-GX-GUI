#pragma once

#include "genplusgx/persistence.h"
#include "genplusgx/rewind_configuration.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace genplusgx::settings {

[[nodiscard]] RewindConfiguration defaultRewindSettings() noexcept;

struct RewindSettingsLoadResult final {
  PersistenceStatus status;
  RewindConfiguration settings;
  bool migrated{false};
};

class RewindSettingsStore final {
public:
  static constexpr std::uint32_t schemaVersion = 1U;
  static constexpr std::size_t maximumFileBytes = 32U * 1024U;

  explicit RewindSettingsStore(std::filesystem::path path);

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] RewindSettingsLoadResult load() const;
  [[nodiscard]] PersistenceStatus save(
    const RewindConfiguration& settings) const;

private:
  std::filesystem::path path_;
};

} // namespace genplusgx::settings
