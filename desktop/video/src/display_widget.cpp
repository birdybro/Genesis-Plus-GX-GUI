#include "genplusgx/video/display_widget.h"

#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QVBoxLayout>

#include <algorithm>

namespace genplusgx::video {

DisplayWidget::DisplayWidget(QWidget* parent)
  : QWidget(parent),
    exchange_(std::make_shared<VideoFrameExchange>()),
    pixels_(VideoFrameExchange::maximumSurfacePixels, 0U)
{
  setObjectName(QStringLiteral("emulatorCanvas"));
  setAccessibleName(tr("Emulator display"));
  setAutoFillBackground(false);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setMinimumSize(320, 240);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  auto* layout = new QVBoxLayout(this);
  emptyLabel_ = new QLabel(tr("Open or drop a game to begin"), this);
  emptyLabel_->setObjectName(QStringLiteral("emptyCanvasLabel"));
  emptyLabel_->setAlignment(Qt::AlignCenter);
  emptyLabel_->setStyleSheet(QStringLiteral("color: #b8b8b8; font-size: 16px;"));
  emptyLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
  layout->addWidget(emptyLabel_);
}

void DisplayWidget::setFrameExchange(std::shared_ptr<VideoFrameExchange> exchange)
{
  exchange_ = std::move(exchange);
  clearFrame();
}

std::shared_ptr<VideoFrameExchange> DisplayWidget::frameExchange() const
{
  return exchange_;
}

bool DisplayWidget::presentLatestFrame()
{
  if (!exchange_) {
    return false;
  }
  CoreVideoFrameInfo nextFrame;
  std::uint64_t nextGeneration = 0;
  const auto copied = exchange_->copyLatest(pixels_, nextFrame, nextGeneration);
  if (!copied) {
    return false;
  }
  frame_ = nextFrame;
  generation_ = nextGeneration;
  hasFrame_ = true;
  emptyLabel_->hide();
  update();
  return true;
}

void DisplayWidget::clearFrame()
{
  hasFrame_ = false;
  frame_ = {};
  generation_ = 0;
  emptyLabel_->show();
  update();
}

bool DisplayWidget::hasFrame() const noexcept
{
  return hasFrame_;
}

const CoreVideoFrameInfo& DisplayWidget::currentFrameInfo() const noexcept
{
  return frame_;
}

std::uint64_t DisplayWidget::currentGeneration() const noexcept
{
  return generation_;
}

std::span<const std::uint16_t> DisplayWidget::currentPixels() const noexcept
{
  return std::span<const std::uint16_t>{pixels_}.first(
    hasFrame_ ? frame_.pixelCount() : 0U);
}

void DisplayWidget::paintEvent(QPaintEvent* event)
{
  static_cast<void>(event);
  QPainter painter(this);
  painter.fillRect(rect(), Qt::black);
  if (!hasFrame_) {
    return;
  }

  const auto imageWidth = static_cast<int>(frame_.width);
  const auto imageHeight = static_cast<int>(frame_.height);
  const QImage image{
    reinterpret_cast<const uchar*>(pixels_.data()),
    imageWidth,
    imageHeight,
    imageWidth * static_cast<int>(sizeof(std::uint16_t)),
    QImage::Format_RGB16};

  const double horizontalScale = static_cast<double>(width()) /
                                 static_cast<double>(imageWidth);
  const double verticalScale = static_cast<double>(height()) /
                               static_cast<double>(imageHeight);
  const double scale = std::min(horizontalScale, verticalScale);
  const int targetWidth = std::max(1, static_cast<int>(imageWidth * scale));
  const int targetHeight = std::max(1, static_cast<int>(imageHeight * scale));
  const QRect target{
    (width() - targetWidth) / 2,
    (height() - targetHeight) / 2,
    targetWidth,
    targetHeight};

  painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
  painter.drawImage(target, image);
}

} // namespace genplusgx::video
