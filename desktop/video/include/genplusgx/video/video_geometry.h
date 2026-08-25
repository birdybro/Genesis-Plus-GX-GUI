#pragma once

#include <cstdint>

namespace genplusgx::video {

enum class AspectMode {
  native,
  fourThree,
  stretch,
};

enum class ScaleMode {
  fit,
  integer,
};

enum class VideoFilter {
  nearest,
  bilinear,
};

struct VideoLayout final {
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};
  std::uint32_t integerScale{0};

  [[nodiscard]] bool valid() const noexcept
  {
    return width > 0 && height > 0;
  }
};

[[nodiscard]] VideoLayout calculateVideoLayout(
  std::uint32_t sourceWidth,
  std::uint32_t sourceHeight,
  std::int32_t availableWidth,
  std::int32_t availableHeight,
  AspectMode aspectMode,
  ScaleMode scaleMode) noexcept;

} // namespace genplusgx::video
