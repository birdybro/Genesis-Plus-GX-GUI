#pragma once

#include "genplusgx/core_system_settings.h"
#include "genplusgx/persistence.h"
#include "genplusgx/platform/bios_manager.h"
#include "genplusgx/settings/audio_settings.h"
#include "genplusgx/settings/video_settings.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace genplusgx::settings {

struct PerGameSettings final {
  std::optional<VideoSettings> video;
  std::optional<AudioSettings> audio;
  std::optional<CoreSystemSettings> system;
  std::optional<std::string> inputProfile;
  std::optional<platform::BiosConfiguration> bios;

  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] bool operator==(const PerGameSettings&) const = default;
};

struct GlobalGameSettings final {
  VideoSettings video{defaultVideoSettings()};
  AudioSettings audio{defaultAudioSettings()};
  CoreSystemSettings system;
  std::string inputProfile;
  platform::BiosConfiguration bios;
};

struct EffectiveGameSettings final {
  VideoSettings video{defaultVideoSettings()};
  AudioSettings audio{defaultAudioSettings()};
  CoreSystemSettings system;
  std::string inputProfile;
  platform::BiosConfiguration bios;

  [[nodiscard]] bool operator==(const EffectiveGameSettings&) const = default;
};

[[nodiscard]] bool validatePerGameSettings(const PerGameSettings& settings) noexcept;
[[nodiscard]] EffectiveGameSettings resolvePerGameSettings(
  const GlobalGameSettings& global, const PerGameSettings& overrides) noexcept;

struct PerGameSettingsLoadResult final {
  PersistenceStatus status;
  bool exists{false};
  PerGameSettings settings;
};

class PerGameSettingsStore final {
public:
  static constexpr std::uint32_t schemaVersion = 1U;
  static constexpr std::size_t maximumFileBytes = 256U * 1024U;
  static constexpr std::size_t maximumInputProfileBytes = 120U;
  static constexpr std::size_t maximumBiosPathBytes = 4U * 1024U;

  explicit PerGameSettingsStore(std::filesystem::path root);

  [[nodiscard]] const std::filesystem::path& root() const noexcept;
  [[nodiscard]] std::filesystem::path pathFor(const GameIdentity& identity) const;
  [[nodiscard]] PerGameSettingsLoadResult load(const GameIdentity& identity) const;
  [[nodiscard]] PersistenceStatus save(
    const GameIdentity& identity, const PerGameSettings& settings) const;

private:
  std::filesystem::path root_;
};

} // namespace genplusgx::settings
