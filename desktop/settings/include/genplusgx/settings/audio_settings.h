#pragma once

#include "genplusgx/core_audio_settings.h"
#include "genplusgx/persistence.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace genplusgx::settings {

struct AudioSettings final {
  int masterVolumePercent{100};
  bool muted{false};
  int latencyMilliseconds{80};
  std::string outputDeviceName;
  CoreAudioSettings core;

  [[nodiscard]] bool operator==(const AudioSettings&) const = default;
};

[[nodiscard]] AudioSettings defaultAudioSettings() noexcept;
[[nodiscard]] bool validateAudioSettings(const AudioSettings& settings) noexcept;

struct AudioSettingsLoadResult final {
  PersistenceStatus status;
  AudioSettings settings;
  bool migrated{false};
};

class AudioSettingsStore final {
public:
  static constexpr std::uint32_t schemaVersion = 1U;
  static constexpr std::size_t maximumFileBytes = 64U * 1024U;

  explicit AudioSettingsStore(std::filesystem::path path);

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] AudioSettingsLoadResult load() const;
  [[nodiscard]] PersistenceStatus save(const AudioSettings& settings) const;

private:
  std::filesystem::path path_;
};

} // namespace genplusgx::settings
