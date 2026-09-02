#pragma once

#include "genplusgx/cloud/cloud_types.h"
#include "genplusgx/persistence.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace genplusgx::cloud {

struct Settings final {
  bool enabled{false};
  bool syncSaves{true};
  bool syncStates{true};
  bool syncOnStartup{false};
  bool syncOnGameClose{true};
  std::string endpoint;
  std::string username;
  std::string remoteDirectory{"Genesis-Plus-GX-GUI"};

  [[nodiscard]] bool operator==(const Settings&) const = default;
};

[[nodiscard]] Settings defaultSettings() noexcept;
[[nodiscard]] Status validateSettings(const Settings& settings) noexcept;

struct SettingsLoadResult final {
  PersistenceStatus status;
  Settings settings;
  bool migrated{false};
};

class SettingsStore final {
public:
  static constexpr std::uint32_t schemaVersion = 1U;
  static constexpr std::size_t maximumFileBytes = 32U * 1024U;

  explicit SettingsStore(std::filesystem::path path);

  [[nodiscard]] SettingsLoadResult load() const;
  [[nodiscard]] PersistenceStatus save(const Settings& settings) const;
  [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
  std::filesystem::path path_;
};

} // namespace genplusgx::cloud
