#pragma once

#include "genplusgx/video/artwork_configuration.h"

#include <QImage>

#include <cstddef>
#include <cstdint>
#include <string>

namespace genplusgx::video {

inline constexpr std::uintmax_t maximumArtworkFileBytes = 32U * 1024U * 1024U;
inline constexpr std::uint32_t maximumArtworkDimension = 4'096U;
inline constexpr std::size_t maximumArtworkPixels =
  static_cast<std::size_t>(maximumArtworkDimension) * maximumArtworkDimension;

struct ArtworkImageLoadResult final {
  bool success{false};
  QImage image;
  std::uintmax_t fileBytes{0U};
  bool hasAlpha{false};
  std::string format;
  std::string error;
};

[[nodiscard]] ArtworkImageLoadResult loadArtworkImage(
  const ArtworkConfiguration& configuration);

} // namespace genplusgx::video
