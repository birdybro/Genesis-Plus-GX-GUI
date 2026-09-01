#include "genplusgx/video/artwork_configuration.h"

#include <limits>

namespace genplusgx::video {
namespace {

std::size_t encodedPathBytes(const std::filesystem::path& path) noexcept
{
  try {
    const auto encoded = path.u8string();
    return encoded.size();
  } catch (...) {
    return std::numeric_limits<std::size_t>::max();
  }
}

} // namespace

bool validateArtworkConfiguration(
  const ArtworkConfiguration& configuration) noexcept
{
  const auto& insets = configuration.viewportInsets;
  return static_cast<unsigned>(configuration.mode) <=
      static_cast<unsigned>(ArtworkMode::overlay) &&
    configuration.opacityPercent >= 1U &&
    configuration.opacityPercent <= 100U &&
    encodedPathBytes(configuration.imagePath) <= maximumArtworkPathBytes &&
    (configuration.mode == ArtworkMode::disabled ||
      (!configuration.imagePath.empty() &&
       configuration.imagePath.is_absolute())) &&
    insets.leftPercent <= maximumArtworkInsetPercent &&
    insets.topPercent <= maximumArtworkInsetPercent &&
    insets.rightPercent <= maximumArtworkInsetPercent &&
    insets.bottomPercent <= maximumArtworkInsetPercent &&
    static_cast<unsigned>(insets.leftPercent) + insets.rightPercent <= 90U &&
    static_cast<unsigned>(insets.topPercent) + insets.bottomPercent <= 90U;
}

ArtworkViewport calculateArtworkViewport(
  std::int32_t availableWidth,
  std::int32_t availableHeight,
  const ArtworkConfiguration& configuration) noexcept
{
  if (availableWidth <= 0 || availableHeight <= 0 ||
      !validateArtworkConfiguration(configuration)) {
    return {};
  }
  if (!configuration.constrainVideoToViewport) {
    return {.width = availableWidth, .height = availableHeight};
  }
  const auto& insets = configuration.viewportInsets;
  const auto scaledInset = [](std::int32_t dimension,
                              std::uint8_t percent) noexcept {
    return static_cast<std::int32_t>(
      static_cast<std::int64_t>(dimension) * percent / 100);
  };
  const auto left = scaledInset(availableWidth, insets.leftPercent);
  const auto top = scaledInset(availableHeight, insets.topPercent);
  const auto right = scaledInset(availableWidth, insets.rightPercent);
  const auto bottom = scaledInset(availableHeight, insets.bottomPercent);
  const auto width = availableWidth - left - right;
  const auto height = availableHeight - top - bottom;
  return width > 0 && height > 0
    ? ArtworkViewport{.x = left, .y = top, .width = width, .height = height}
    : ArtworkViewport{};
}

VideoLayout calculateArtworkVideoLayout(
  std::uint32_t sourceWidth,
  std::uint32_t sourceHeight,
  std::int32_t availableWidth,
  std::int32_t availableHeight,
  AspectMode aspectMode,
  ScaleMode scaleMode,
  const ArtworkConfiguration& configuration) noexcept
{
  const auto viewport = calculateArtworkViewport(
    availableWidth, availableHeight, configuration);
  if (!viewport.valid()) {
    return {};
  }
  auto layout = calculateVideoLayout(sourceWidth, sourceHeight,
    viewport.width, viewport.height, aspectMode, scaleMode);
  if (layout.valid()) {
    layout.x += viewport.x;
    layout.y += viewport.y;
  }
  return layout;
}

} // namespace genplusgx::video
