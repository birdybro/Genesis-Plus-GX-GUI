#pragma once

#include "genplusgx/video/video_geometry.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace genplusgx::video {

enum class ArtworkMode : std::uint8_t {
  disabled,
  bezel,
  overlay,
};

struct ArtworkInsets final {
  std::uint8_t leftPercent{0U};
  std::uint8_t topPercent{0U};
  std::uint8_t rightPercent{0U};
  std::uint8_t bottomPercent{0U};

  [[nodiscard]] bool operator==(const ArtworkInsets&) const = default;
};

struct ArtworkConfiguration final {
  ArtworkMode mode{ArtworkMode::disabled};
  std::filesystem::path imagePath;
  std::uint8_t opacityPercent{100U};
  bool constrainVideoToViewport{false};
  ArtworkInsets viewportInsets;

  [[nodiscard]] bool operator==(const ArtworkConfiguration&) const = default;
};

inline constexpr std::size_t maximumArtworkPathBytes = 4U * 1024U;
inline constexpr std::uint8_t maximumArtworkInsetPercent = 45U;

struct ArtworkViewport final {
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};

  [[nodiscard]] bool valid() const noexcept
  {
    return width > 0 && height > 0;
  }

  [[nodiscard]] bool operator==(const ArtworkViewport&) const = default;
};

[[nodiscard]] bool validateArtworkConfiguration(
  const ArtworkConfiguration& configuration) noexcept;
[[nodiscard]] ArtworkViewport calculateArtworkViewport(
  std::int32_t availableWidth,
  std::int32_t availableHeight,
  const ArtworkConfiguration& configuration) noexcept;
[[nodiscard]] VideoLayout calculateArtworkVideoLayout(
  std::uint32_t sourceWidth,
  std::uint32_t sourceHeight,
  std::int32_t availableWidth,
  std::int32_t availableHeight,
  AspectMode aspectMode,
  ScaleMode scaleMode,
  const ArtworkConfiguration& configuration) noexcept;

} // namespace genplusgx::video
