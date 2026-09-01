#include "genplusgx/video/artwork_configuration.h"
#include "genplusgx/video/artwork_image.h"

#include <QColor>
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::filesystem::path fixturePath(
  const QTemporaryDir& directory, const char* name)
{
  return std::filesystem::path{directory.path().toStdString()} / name;
}

} // namespace

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  using namespace genplusgx::video;

  ArtworkConfiguration bezel{
    .mode = ArtworkMode::bezel,
    .imagePath = std::filesystem::path{"/absolute/bezel.png"},
    .opacityPercent = 65U,
    .constrainVideoToViewport = true,
    .viewportInsets = {
      .leftPercent = 20U,
      .topPercent = 10U,
      .rightPercent = 20U,
      .bottomPercent = 10U,
    },
  };
  const auto constrainedLayout = calculateArtworkVideoLayout(
    320U, 240U, 1000, 500, AspectMode::fourThree, ScaleMode::fit, bezel);
  if (!check(validateArtworkConfiguration(bezel),
        "A valid artwork configuration was rejected") ||
      !check(calculateArtworkViewport(1000, 500, bezel) ==
        ArtworkViewport{.x = 200, .y = 50, .width = 600, .height = 400},
        "Artwork viewport insets were calculated incorrectly") ||
      !check(constrainedLayout.x == 233 && constrainedLayout.y == 50 &&
        constrainedLayout.width == 533 && constrainedLayout.height == 400 &&
        constrainedLayout.integerScale == 0U,
        "Artwork-constrained video geometry was calculated incorrectly")) {
    return 1;
  }
  auto invalid = bezel;
  invalid.opacityPercent = 0U;
  if (!check(!validateArtworkConfiguration(invalid),
        "Zero artwork opacity was accepted")) {
    return 2;
  }
  invalid = bezel;
  invalid.imagePath = "relative.png";
  if (!check(!validateArtworkConfiguration(invalid),
        "A relative enabled artwork path was accepted")) {
    return 3;
  }
  invalid = bezel;
  invalid.viewportInsets.leftPercent = 45U;
  invalid.viewportInsets.rightPercent = 46U;
  if (!check(!validateArtworkConfiguration(invalid),
        "Invalid opposing artwork insets were accepted")) {
    return 3;
  }
  const auto hugeViewport = calculateArtworkViewport(
    std::numeric_limits<std::int32_t>::max(),
    std::numeric_limits<std::int32_t>::max(), bezel);
  if (!check(hugeViewport.valid() && hugeViewport.x > 0 &&
        hugeViewport.width > 0,
      "Artwork viewport arithmetic overflowed at a valid extreme")) {
    return 4;
  }

  QTemporaryDir directory;
  if (!check(directory.isValid(),
        "Temporary artwork directory is unavailable")) {
    return 5;
  }
  const auto overlayPath = fixturePath(directory, "overlay.png");
  QImage overlay(16, 12, QImage::Format_ARGB32);
  overlay.fill(QColor{0, 0, 0, 0});
  overlay.setPixelColor(0, 0, QColor{255, 0, 0, 200});
  if (!check(overlay.save(QString::fromStdString(overlayPath.string()), "PNG"),
        "The valid overlay fixture could not be written")) {
    return 6;
  }
  ArtworkConfiguration overlayConfiguration{
    .mode = ArtworkMode::overlay,
    .imagePath = overlayPath,
    .opacityPercent = 100U,
    .constrainVideoToViewport = false,
    .viewportInsets = {},
  };
  const auto loadedOverlay = loadArtworkImage(overlayConfiguration);
  const bool overlayLoaded = loadedOverlay.success &&
    !loadedOverlay.image.isNull() &&
        loadedOverlay.image.format() == QImage::Format_RGBA8888 &&
        loadedOverlay.image.width() == 16 && loadedOverlay.image.height() == 12 &&
        loadedOverlay.hasAlpha && loadedOverlay.fileBytes > 0U &&
        loadedOverlay.format == "png";
  if (!overlayLoaded) {
    std::cerr << "Overlay decode: success=" << loadedOverlay.success
              << " null=" << loadedOverlay.image.isNull()
              << " format=" << static_cast<int>(loadedOverlay.image.format())
              << " size=" << loadedOverlay.image.width() << 'x'
              << loadedOverlay.image.height()
              << " alpha=" << loadedOverlay.hasAlpha
              << " bytes=" << loadedOverlay.fileBytes
              << " reader-format=" << loadedOverlay.format
              << " error=" << loadedOverlay.error << '\n';
  }
  if (!check(overlayLoaded,
        "A valid alpha overlay did not decode into the bounded cache format")) {
    return 7;
  }

  const auto jpegPath = fixturePath(directory, "opaque.jpg");
  QImage opaque(8, 8, QImage::Format_RGB32);
  opaque.fill(QColor{10, 20, 30});
  if (!check(opaque.save(QString::fromStdString(jpegPath.string()), "JPEG"),
        "The opaque artwork fixture could not be written")) {
    return 8;
  }
  auto jpegConfiguration = overlayConfiguration;
  jpegConfiguration.imagePath = jpegPath;
  const auto rejectedForeground = loadArtworkImage(jpegConfiguration);
  jpegConfiguration.mode = ArtworkMode::bezel;
  const auto acceptedBackground = loadArtworkImage(jpegConfiguration);
  if (!check(!rejectedForeground.success &&
        rejectedForeground.error.find("alpha") != std::string::npos &&
        acceptedBackground.success && !acceptedBackground.hasAlpha,
      "Opaque artwork was not restricted to background bezel mode")) {
    return 9;
  }
  const auto bitmapPath = fixturePath(directory, "alpha.bmp");
  if (!check(overlay.save(QString::fromStdString(bitmapPath.string()), "BMP"),
        "The bitmap artwork fixture could not be written")) {
    return 10;
  }
  auto bitmapConfiguration = overlayConfiguration;
  bitmapConfiguration.imagePath = bitmapPath;
  if (!check(!loadArtworkImage(bitmapConfiguration).success,
        "A non-PNG foreground overlay was accepted")) {
    return 10;
  }
  const auto opaquePngPath = fixturePath(directory, "opaque-alpha.png");
  QImage opaqueAlpha(8, 8, QImage::Format_ARGB32);
  opaqueAlpha.fill(QColor{10, 20, 30, 255});
  if (!check(opaqueAlpha.save(
        QString::fromStdString(opaquePngPath.string()), "PNG"),
        "The opaque alpha-PNG fixture could not be written")) {
    return 10;
  }
  auto opaquePngConfiguration = overlayConfiguration;
  opaquePngConfiguration.imagePath = opaquePngPath;
  const auto rejectedOpaquePng = loadArtworkImage(opaquePngConfiguration);
  if (!check(!rejectedOpaquePng.success &&
        rejectedOpaquePng.error.find("transparent") != std::string::npos,
        "A fully opaque foreground PNG was accepted")) {
    return 10;
  }

  const auto oversizedPath = fixturePath(directory, "oversized.png");
  QImage oversized(static_cast<int>(maximumArtworkDimension + 1U), 1,
    QImage::Format_ARGB32);
  oversized.fill(Qt::transparent);
  if (!check(oversized.save(
        QString::fromStdString(oversizedPath.string()), "PNG"),
        "The oversized fixture could not be written")) {
    return 10;
  }
  auto oversizedConfiguration = overlayConfiguration;
  oversizedConfiguration.imagePath = oversizedPath;
  if (!check(!loadArtworkImage(oversizedConfiguration).success,
        "An oversized artwork image was decoded")) {
    return 11;
  }

  const auto malformedPath = fixturePath(directory, "malformed.png");
  QFile malformed(QString::fromStdString(malformedPath.string()));
  if (!check(malformed.open(QIODevice::WriteOnly) &&
        malformed.write("not an image") == 12,
      "The malformed fixture could not be written")) {
    return 12;
  }
  malformed.close();
  auto malformedConfiguration = overlayConfiguration;
  malformedConfiguration.imagePath = malformedPath;
  if (!check(!loadArtworkImage(malformedConfiguration).success,
        "A malformed artwork file was accepted")) {
    return 13;
  }

  const auto disabled = loadArtworkImage(ArtworkConfiguration{});
  return check(disabled.success && disabled.image.isNull(),
      "Disabled artwork unexpectedly attempted file I/O")
    ? 0 : 14;
}
