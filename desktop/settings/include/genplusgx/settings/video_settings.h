#pragma once

#include "genplusgx/core_video_settings.h"
#include "genplusgx/persistence.h"
#include "genplusgx/video/video_geometry.h"
#include "genplusgx/video/shader_configuration.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace genplusgx::settings {

struct VideoSettings final {
  video::AspectMode aspect{video::AspectMode::native};
  video::ScaleMode scaling{video::ScaleMode::fit};
  video::VideoFilter presentationFilter{video::VideoFilter::nearest};
  video::ShaderConfiguration shader;
  CoreVideoSettings core;

  [[nodiscard]] bool operator==(const VideoSettings&) const = default;
};

[[nodiscard]] VideoSettings defaultVideoSettings() noexcept;
[[nodiscard]] bool validateVideoSettings(
  const VideoSettings& settings) noexcept;

struct VideoSettingsLoadResult final {
  PersistenceStatus status;
  VideoSettings settings;
  bool migrated{false};
};

class VideoSettingsStore final {
public:
  static constexpr std::uint32_t schemaVersion = 2U;
  static constexpr std::size_t maximumFileBytes = 64U * 1024U;

  explicit VideoSettingsStore(std::filesystem::path path);

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] VideoSettingsLoadResult load() const;
  [[nodiscard]] PersistenceStatus save(const VideoSettings& settings) const;

private:
  std::filesystem::path path_;
};

} // namespace genplusgx::settings
