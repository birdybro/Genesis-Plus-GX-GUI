#include "genplusgx/video/display_widget.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QLabel>
#include <QTest>
#include <QTemporaryDir>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>

namespace {

class DisplayWidgetTest final : public QObject {
  Q_OBJECT

private slots:
  void presentsNewestFrameAndResizesStably();
};

void DisplayWidgetTest::presentsNewestFrameAndResizesStably()
{
  QTemporaryDir artworkDirectory;
  QVERIFY(artworkDirectory.isValid());
  auto exchange = std::make_shared<genplusgx::VideoFrameExchange>(8U);
  genplusgx::video::DisplayWidget widget;
  widget.setFrameExchange(exchange);
  widget.resize(400, 300);
  widget.show();
  QApplication::processEvents();

  QCOMPARE(widget.focusPolicy(), Qt::StrongFocus);
  QVERIFY(!widget.usesAcceleratedRenderer());
  QVERIFY(!widget.hasFrame());
  QVERIFY(!widget.presentLatestFrame());
  auto* prompt = widget.findChild<QLabel*>(QStringLiteral("emptyCanvasLabel"));
  QVERIFY(prompt != nullptr);
  QVERIFY(prompt->isVisible());

  auto write = exchange->beginWrite();
  QVERIFY(write.has_value());
  const std::uint16_t pixels[]{
    0xF800U, 0xF800U, 0x07E0U, 0x07E0U,
    0xF800U, 0xF800U, 0x07E0U, 0x07E0U};
  std::ranges::copy(pixels, write->pixels().begin());
  const genplusgx::CoreVideoFrameInfo info{
    .format = genplusgx::CorePixelFormat::rgb565,
    .width = 4U,
    .height = 2U,
    .sourceSurfaceWidth = 4U,
    .sourceSurfaceHeight = 2U,
    .sourcePitchPixels = 4U,
    .frameNumber = 77U,
  };
  QVERIFY(write->publish(info));
  QVERIFY(widget.presentLatestFrame());
  QApplication::processEvents();

  QVERIFY(widget.hasFrame());
  QCOMPARE(widget.currentGeneration(), 1U);
  QCOMPARE(widget.currentFrameInfo().frameNumber, 77U);
  QCOMPARE(widget.currentPixels().size(), 8U);
  QVERIFY(!prompt->isVisible());

  QImage rendered(widget.size(), QImage::Format_ARGB32);
  rendered.fill(Qt::magenta);
  widget.render(&rendered);
  QCOMPARE(rendered.pixelColor(200, 10), QColor(Qt::black));
  QCOMPARE(rendered.pixelColor(100, 150), QColor(Qt::red));
  QCOMPARE(rendered.pixelColor(300, 150), QColor(Qt::green));

  widget.setAspectMode(genplusgx::video::AspectMode::fourThree);
  QCOMPARE(widget.aspectMode(), genplusgx::video::AspectMode::fourThree);
  QCOMPARE(widget.currentLayout().width, 400);
  QCOMPARE(widget.currentLayout().height, 300);
  widget.setAspectMode(genplusgx::video::AspectMode::stretch);
  QCOMPARE(widget.currentLayout().width, 400);
  QCOMPARE(widget.currentLayout().height, 300);
  widget.setAspectMode(genplusgx::video::AspectMode::native);

  widget.resize(410, 310);
  widget.setScaleMode(genplusgx::video::ScaleMode::fit);
  QCOMPARE(widget.currentLayout().width, 410);
  QCOMPARE(widget.currentLayout().height, 205);
  widget.setScaleMode(genplusgx::video::ScaleMode::integer);
  QCOMPARE(widget.currentLayout().width, 408);
  QCOMPARE(widget.currentLayout().height, 204);
  QCOMPARE(widget.currentLayout().integerScale, 102U);
  widget.setVideoFilter(genplusgx::video::VideoFilter::bilinear);
  QCOMPARE(widget.videoFilter(), genplusgx::video::VideoFilter::bilinear);
  widget.setVideoFilter(genplusgx::video::VideoFilter::nearest);
  QCOMPARE(widget.videoFilter(), genplusgx::video::VideoFilter::nearest);

  const auto artworkRoot = std::filesystem::path{
    artworkDirectory.path().toStdString()};
  const auto artworkPath = artworkRoot / "bezel.png";
  QImage bezel(400, 300, QImage::Format_ARGB32);
  bezel.fill(QColor{0, 0, 255, 255});
  QVERIFY(bezel.save(QString::fromStdString(artworkPath.string()), "PNG"));
  QVERIFY(widget.setArtworkConfiguration({
    .mode = genplusgx::video::ArtworkMode::bezel,
    .imagePath = artworkPath,
    .opacityPercent = 100U,
    .constrainVideoToViewport = true,
    .viewportInsets = {
      .leftPercent = 25U,
      .topPercent = 10U,
      .rightPercent = 25U,
      .bottomPercent = 10U,
    },
  }));
  QVERIFY(widget.artworkAvailable());
  QCOMPARE(widget.artworkFormat(), std::string{"png"});
  QCOMPARE(widget.artworkWidth(), 400U);
  QCOMPARE(widget.artworkHeight(), 300U);
  QVERIFY(widget.artworkFileBytes() > 0U);
  QApplication::processEvents();
  rendered = QImage(widget.size(), QImage::Format_ARGB32);
  rendered.fill(Qt::magenta);
  widget.render(&rendered);
  const auto bezelLayout = widget.currentLayout();
  QVERIFY(bezelLayout.valid());
  QVERIFY(bezelLayout.x >= 100);
  QCOMPARE(rendered.pixelColor(10, 10), QColor(Qt::blue));
  QVERIFY(rendered.pixelColor(
    bezelLayout.x + bezelLayout.width / 4,
    bezelLayout.y + bezelLayout.height / 2).red() > 200);
  QVERIFY(rendered.pixelColor(
    bezelLayout.x + (bezelLayout.width * 3) / 4,
    bezelLayout.y + bezelLayout.height / 2).green() > 200);

  const auto overlayPath = artworkRoot / "overlay.png";
  QImage overlay(400, 300, QImage::Format_ARGB32);
  overlay.fill(Qt::transparent);
  for (int y = 0; y < 40; ++y) {
    for (int x = 0; x < overlay.width(); ++x) {
      overlay.setPixelColor(x, y, QColor{255, 255, 0, 255});
    }
  }
  QVERIFY(overlay.save(QString::fromStdString(overlayPath.string()), "PNG"));
  QVERIFY(widget.setArtworkConfiguration({
    .mode = genplusgx::video::ArtworkMode::overlay,
    .imagePath = overlayPath,
    .opacityPercent = 100U,
    .constrainVideoToViewport = false,
    .viewportInsets = {},
  }));
  QApplication::processEvents();
  rendered.fill(Qt::magenta);
  widget.render(&rendered);
  QCOMPARE(rendered.pixelColor(200, 10), QColor(Qt::yellow));
  const auto overlayLayout = widget.currentLayout();
  QVERIFY(rendered.pixelColor(
    overlayLayout.x + overlayLayout.width / 4,
    overlayLayout.y + (overlayLayout.height * 3) / 4).red() > 200);

  std::string artworkFailure;
  widget.setArtworkFailureSink([&artworkFailure](std::string detail) {
    artworkFailure = std::move(detail);
  });
  QVERIFY(!widget.setArtworkConfiguration({
    .mode = genplusgx::video::ArtworkMode::bezel,
    .imagePath = artworkRoot / "missing.png",
    .opacityPercent = 100U,
    .constrainVideoToViewport = false,
    .viewportInsets = {},
  }));
  QApplication::processEvents();
  QVERIFY(!artworkFailure.empty());
  QVERIFY(!widget.artworkAvailable());
  QVERIFY(widget.setArtworkConfiguration({}));
  QCOMPARE(widget.sourceFramesPerSecond(), 60.0);
  QCOMPARE(widget.presentationConfiguration(),
    genplusgx::video::PresentationConfiguration{});
  widget.setPresentationConfiguration({
    .sync = genplusgx::video::PresentationSyncMode::adaptive,
    .buffering = genplusgx::video::PresentationBufferingMode::tripleBuffer,
  });
  QCOMPARE(widget.presentationConfiguration().sync,
    genplusgx::video::PresentationSyncMode::adaptive);
  QCOMPARE(widget.presentationConfiguration().buffering,
    genplusgx::video::PresentationBufferingMode::tripleBuffer);
  auto invalidPresentation = widget.presentationConfiguration();
  invalidPresentation.sync =
    static_cast<genplusgx::video::PresentationSyncMode>(99);
  widget.setPresentationConfiguration(invalidPresentation);
  QCOMPARE(widget.presentationConfiguration().sync,
    genplusgx::video::PresentationSyncMode::adaptive);
  widget.setSourceFramesPerSecond(50.0);
  QCOMPARE(widget.sourceFramesPerSecond(), 50.0);
  widget.setSourceFramesPerSecond(0.0);
  QCOMPARE(widget.sourceFramesPerSecond(), 50.0);
  widget.setScaleMode(genplusgx::video::ScaleMode::fit);

  widget.resize(320, 640);
  QApplication::processEvents();
  rendered = QImage(widget.size(), QImage::Format_ARGB32);
  rendered.fill(Qt::magenta);
  widget.render(&rendered);
  QCOMPARE(rendered.pixelColor(160, 10), QColor(Qt::black));
  QCOMPARE(rendered.pixelColor(80, 320), QColor(Qt::red));
  QCOMPARE(rendered.pixelColor(240, 320), QColor(Qt::green));
  QCOMPARE(widget.currentGeneration(), 1U);
  const auto presentation = widget.presentationMetrics();
  QCOMPARE(presentation.requested, widget.presentationConfiguration());
  QVERIFY(!presentation.accelerated);
  QVERIFY(!presentation.rendererInitialized);
  QCOMPARE(presentation.exchange.publishedFrames, 1U);
  QCOMPARE(presentation.exchange.copiedFrames, 1U);
  QCOMPARE(presentation.telemetry.receivedFrames, 1U);
  QCOMPARE(presentation.telemetry.maximumPendingFrames, std::size_t{1U});
  QCOMPARE(presentation.telemetry.pendingFrames, std::size_t{0U});
  QVERIFY(presentation.telemetry.renderedFrames >= 1U);
  QVERIFY(presentation.telemetry.swappedFrames >= 1U);

  widget.clearFrame();
  QApplication::processEvents();
  QVERIFY(!widget.hasFrame());
  QVERIFY(prompt->isVisible());
  QCOMPARE(widget.presentationMetrics().telemetry.pendingFrames,
    std::size_t{0U});
}

} // namespace

QTEST_MAIN(DisplayWidgetTest)

#include "display_widget_test.moc"
