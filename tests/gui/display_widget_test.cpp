#include "genplusgx/video/display_widget.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QLabel>
#include <QTest>

#include <algorithm>
#include <memory>

namespace {

class DisplayWidgetTest final : public QObject {
  Q_OBJECT

private slots:
  void presentsNewestFrameAndResizesStably();
};

void DisplayWidgetTest::presentsNewestFrameAndResizesStably()
{
  auto exchange = std::make_shared<genplusgx::VideoFrameExchange>(8U);
  genplusgx::video::DisplayWidget widget;
  widget.setFrameExchange(exchange);
  widget.resize(400, 300);
  widget.show();
  QApplication::processEvents();

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

  widget.resize(320, 640);
  QApplication::processEvents();
  rendered = QImage(widget.size(), QImage::Format_ARGB32);
  rendered.fill(Qt::magenta);
  widget.render(&rendered);
  QCOMPARE(rendered.pixelColor(160, 10), QColor(Qt::black));
  QCOMPARE(rendered.pixelColor(80, 320), QColor(Qt::red));
  QCOMPARE(rendered.pixelColor(240, 320), QColor(Qt::green));
  QCOMPARE(widget.currentGeneration(), 1U);

  widget.clearFrame();
  QApplication::processEvents();
  QVERIFY(!widget.hasFrame());
  QVERIFY(prompt->isVisible());
}

} // namespace

QTEST_MAIN(DisplayWidgetTest)

#include "display_widget_test.moc"
