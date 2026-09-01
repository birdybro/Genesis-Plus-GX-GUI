#pragma once

#include "genplusgx/persistence.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace genplusgx::settings {

struct SessionSettings final {
  bool resumeOnLaunch{false};
  std::optional<std::filesystem::path> lastGamePath;

  [[nodiscard]] bool operator==(const SessionSettings&) const = default;
};

[[nodiscard]] SessionSettings defaultSessionSettings() noexcept;
[[nodiscard]] bool validateSessionSettings(const SessionSettings& settings) noexcept;

struct SessionSettingsLoadResult final {
  PersistenceStatus status;
  SessionSettings settings;
  bool migrated{false};
};

class SessionSettingsStore final {
public:
  static constexpr std::uint32_t schemaVersion = 1U;
  static constexpr std::size_t maximumFileBytes = 32U * 1024U;
  static constexpr std::size_t maximumPathBytes = 16U * 1024U;

  explicit SessionSettingsStore(std::filesystem::path path);

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] SessionSettingsLoadResult load() const;
  [[nodiscard]] PersistenceStatus save(const SessionSettings& settings) const;

private:
  std::filesystem::path path_;
};

} // namespace genplusgx::settings
