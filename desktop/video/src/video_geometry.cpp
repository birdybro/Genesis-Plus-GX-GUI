#include "genplusgx/video/video_geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace genplusgx::video {
namespace {

std::int32_t boundedDimension(double value, std::int32_t maximum) noexcept
{
  if (!std::isfinite(value) || value <= 0.0 || maximum < 1) {
    return 0;
  }
  return std::clamp(
    static_cast<std::int32_t>(std::min(
      std::floor(value),
      static_cast<double>(std::numeric_limits<std::int32_t>::max()))),
    1,
    maximum);
}

} // namespace

VideoLayout calculateVideoLayout(
  std::uint32_t sourceWidth,
  std::uint32_t sourceHeight,
  std::int32_t availableWidth,
  std::int32_t availableHeight,
  AspectMode aspectMode,
  ScaleMode scaleMode) noexcept
{
  if (sourceWidth == 0U || sourceHeight == 0U ||
      availableWidth <= 0 || availableHeight <= 0) {
    return {};
  }
  if (aspectMode == AspectMode::stretch) {
    return {
      .width = availableWidth,
      .height = availableHeight,
    };
  }

  const double sourceHeightValue = static_cast<double>(sourceHeight);
  const double targetAspect = aspectMode == AspectMode::fourThree
    ? 4.0 / 3.0
    : static_cast<double>(sourceWidth) / sourceHeightValue;
  const double baseWidth = sourceHeightValue * targetAspect;
  const double fitScale = std::min(
    static_cast<double>(availableWidth) / baseWidth,
    static_cast<double>(availableHeight) / sourceHeightValue);

  std::uint32_t integerScale = 0U;
  double scale = fitScale;
  if (scaleMode == ScaleMode::integer) {
    const double maximumInteger = std::floor(fitScale);
    if (maximumInteger >= 1.0 &&
        maximumInteger <= static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
      integerScale = static_cast<std::uint32_t>(maximumInteger);
      scale = maximumInteger;
    }
  }

  const auto targetHeight = boundedDimension(sourceHeightValue * scale, availableHeight);
  auto targetWidth = aspectMode == AspectMode::native && integerScale > 0U
    ? boundedDimension(
        static_cast<double>(sourceWidth) * static_cast<double>(integerScale),
        availableWidth)
    : boundedDimension(static_cast<double>(targetHeight) * targetAspect, availableWidth);
  if (targetWidth == 0 || targetHeight == 0) {
    return {};
  }

  // Flooring height first can leave one avoidable horizontal pixel at some ratios.
  // Expanding by one is safe only when it still fits and does not exceed the ideal size.
  const double idealWidth = baseWidth * scale;
  if (targetWidth < availableWidth &&
      static_cast<double>(targetWidth + 1) <= idealWidth) {
    ++targetWidth;
  }

  return {
    .x = (availableWidth - targetWidth) / 2,
    .y = (availableHeight - targetHeight) / 2,
    .width = targetWidth,
    .height = targetHeight,
    .integerScale = integerScale,
  };
}

} // namespace genplusgx::video
