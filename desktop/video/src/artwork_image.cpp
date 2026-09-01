#include "genplusgx/video/artwork_image.h"

#include <QByteArray>
#include <QImageReader>
#include <QString>

#include <array>
#include <filesystem>
#include <limits>
#include <system_error>
#include <utility>

namespace genplusgx::video {
namespace {

QString fromPath(const std::filesystem::path& path)
{
#if defined(Q_OS_WIN)
  return QString::fromStdWString(path.wstring());
#else
  const auto bytes = path.u8string();
  return QString::fromUtf8(reinterpret_cast<const char*>(bytes.data()),
    static_cast<qsizetype>(bytes.size()));
#endif
}

ArtworkImageLoadResult failure(std::string error)
{
  ArtworkImageLoadResult result;
  result.error = std::move(error);
  return result;
}

bool supportedFormat(const QByteArray& format)
{
  const auto normalized = format.toLower();
  constexpr std::array<const char*, 4U> supported{
    "png", "jpeg", "jpg", "bmp"};
  for (const auto* candidate : supported) {
    if (normalized == candidate) {
      return true;
    }
  }
  return false;
}

bool dimensionsAllowed(const QSize& size)
{
  if (!size.isValid() || size.width() <= 0 || size.height() <= 0 ||
      size.width() > static_cast<int>(maximumArtworkDimension) ||
      size.height() > static_cast<int>(maximumArtworkDimension)) {
    return false;
  }
  const auto pixels = static_cast<std::size_t>(size.width()) *
    static_cast<std::size_t>(size.height());
  return pixels <= maximumArtworkPixels;
}

} // namespace

ArtworkImageLoadResult loadArtworkImage(
  const ArtworkConfiguration& configuration)
{
  if (!validateArtworkConfiguration(configuration)) {
    return failure("The artwork configuration is invalid.");
  }
  if (configuration.mode == ArtworkMode::disabled) {
    ArtworkImageLoadResult result;
    result.success = true;
    return result;
  }

  std::error_code error;
  if (!std::filesystem::is_regular_file(configuration.imagePath, error) || error) {
    return failure("The selected artwork file does not exist or is not a regular file.");
  }
  const auto fileBytes = std::filesystem::file_size(
    configuration.imagePath, error);
  if (error || fileBytes == 0U || fileBytes > maximumArtworkFileBytes) {
    return failure("Artwork files must be between 1 byte and 32 MiB.");
  }

  QImageReader reader{fromPath(configuration.imagePath)};
  reader.setAutoTransform(true);
  reader.setDecideFormatFromContent(true);
  if (!reader.canRead()) {
    return failure("Artwork must be a valid PNG, JPEG, or BMP image.");
  }
  const auto detectedFormat = reader.format().toLower();
  if (!supportedFormat(detectedFormat)) {
    return failure("Artwork must be a valid PNG, JPEG, or BMP image.");
  }
  if (!dimensionsAllowed(reader.size())) {
    return failure("Artwork dimensions must not exceed 4096 by 4096 pixels.");
  }
  auto image = reader.read();
  if (image.isNull() || !dimensionsAllowed(image.size())) {
    const auto detail = reader.errorString().toStdString();
    return failure(detail.empty()
      ? "The artwork image could not be decoded safely."
      : "The artwork image could not be decoded safely: " + detail);
  }
  const bool hasAlpha = image.hasAlphaChannel();
  image = image.convertToFormat(QImage::Format_RGBA8888);
  if (image.isNull()) {
    return failure("The artwork image could not be converted for rendering.");
  }
  bool hasTransparency = false;
  if (hasAlpha) {
    for (int y = 0; y < image.height() && !hasTransparency; ++y) {
      const auto* row = image.constScanLine(y);
      for (int x = 0; x < image.width(); ++x) {
        if (row[x * 4 + 3] != 0xffU) {
          hasTransparency = true;
          break;
        }
      }
    }
  }
  if (configuration.mode == ArtworkMode::overlay &&
      (detectedFormat != QByteArrayLiteral("png") || !hasAlpha ||
       !hasTransparency)) {
    return failure(
      "Foreground overlays require PNG alpha transparency (transparent pixels) so the game remains visible.");
  }
  ArtworkImageLoadResult result;
  result.success = true;
  result.image = std::move(image);
  result.fileBytes = fileBytes;
  result.hasAlpha = hasAlpha;
  result.format = detectedFormat.toStdString();
  return result;
}

} // namespace genplusgx::video
