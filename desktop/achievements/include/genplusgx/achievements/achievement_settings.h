#pragma once

#include "genplusgx/persistence.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace genplusgx::achievements {

inline constexpr std::size_t maximumUsernameBytes = 64U;

struct Settings final {
  bool enabled{false};
  bool hardcore{true};
  bool unofficial{false};
  bool encore{false};
  bool notifications{true};
  std::string username;

  [[nodiscard]] bool operator==(const Settings&) const = default;
};

[[nodiscard]] Settings defaultSettings() noexcept;
[[nodiscard]] bool validUsername(const std::string& username) noexcept;
[[nodiscard]] bool validateSettings(const Settings& settings) noexcept;

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

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] SettingsLoadResult load() const;
  [[nodiscard]] PersistenceStatus save(const Settings& settings) const;

private:
  std::filesystem::path path_;
};

} // namespace genplusgx::achievements
