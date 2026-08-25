#pragma once

#include "genplusgx/video/frame_exchange.h"
#include "genplusgx/video/video_geometry.h"

#include <QWidget>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

class QLabel;
class QPaintEvent;
class QPainter;

namespace genplusgx::video {

class OpenGLCanvas;

class DisplayWidget final : public QWidget {
public:
  explicit DisplayWidget(QWidget* parent = nullptr);

  void setFrameExchange(std::shared_ptr<VideoFrameExchange> exchange);
  [[nodiscard]] std::shared_ptr<VideoFrameExchange> frameExchange() const;
  [[nodiscard]] bool presentLatestFrame();
  void clearFrame();
  void setAspectMode(AspectMode mode);
  void setScaleMode(ScaleMode mode);
  void setVideoFilter(VideoFilter filter);

  [[nodiscard]] bool hasFrame() const noexcept;
  [[nodiscard]] const CoreVideoFrameInfo& currentFrameInfo() const noexcept;
  [[nodiscard]] std::uint64_t currentGeneration() const noexcept;
  [[nodiscard]] std::span<const std::uint16_t> currentPixels() const noexcept;
  [[nodiscard]] AspectMode aspectMode() const noexcept;
  [[nodiscard]] ScaleMode scaleMode() const noexcept;
  [[nodiscard]] VideoFilter videoFilter() const noexcept;
  [[nodiscard]] VideoLayout currentLayout() const noexcept;
  [[nodiscard]] bool usesAcceleratedRenderer() const noexcept;

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  friend class OpenGLCanvas;
  void paintSoftwareFrame(QPainter& painter);
  void requestRepaint();
  void scheduleSoftwareFallback();

  std::shared_ptr<VideoFrameExchange> exchange_;
  std::vector<std::uint16_t> pixels_;
  CoreVideoFrameInfo frame_;
  std::uint64_t generation_{0};
  QLabel* emptyLabel_{nullptr};
  bool hasFrame_{false};
  AspectMode aspectMode_{AspectMode::native};
  ScaleMode scaleMode_{ScaleMode::fit};
  VideoFilter videoFilter_{VideoFilter::nearest};
  OpenGLCanvas* openGLCanvas_{nullptr};
};

} // namespace genplusgx::video
