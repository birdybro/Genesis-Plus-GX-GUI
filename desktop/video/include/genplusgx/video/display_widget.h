#pragma once

#include "genplusgx/video/frame_exchange.h"
#include "genplusgx/video/shader_configuration.h"
#include "genplusgx/video/video_geometry.h"

#include <QWidget>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
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
  void setShaderConfiguration(ShaderConfiguration configuration);
  void setSourceFramesPerSecond(double framesPerSecond);
  void setRendererFailureSink(std::function<void(std::string)> sink);
  void setShaderFailureSink(std::function<void(std::string)> sink);

  [[nodiscard]] bool hasFrame() const noexcept;
  [[nodiscard]] const CoreVideoFrameInfo& currentFrameInfo() const noexcept;
  [[nodiscard]] std::uint64_t currentGeneration() const noexcept;
  [[nodiscard]] std::span<const std::uint16_t> currentPixels() const noexcept;
  [[nodiscard]] AspectMode aspectMode() const noexcept;
  [[nodiscard]] ScaleMode scaleMode() const noexcept;
  [[nodiscard]] VideoFilter videoFilter() const noexcept;
  [[nodiscard]] const ShaderConfiguration& shaderConfiguration() const noexcept;
  [[nodiscard]] double sourceFramesPerSecond() const noexcept;
  [[nodiscard]] VideoLayout currentLayout() const noexcept;
  [[nodiscard]] bool usesAcceleratedRenderer() const noexcept;

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  friend class OpenGLCanvas;
  void paintSoftwareFrame(QPainter& painter);
  void requestRepaint();
  void scheduleSoftwareFallback();
  void reportShaderFailure(std::string detail);

  std::shared_ptr<VideoFrameExchange> exchange_;
  std::vector<std::uint16_t> pixels_;
  CoreVideoFrameInfo frame_;
  std::uint64_t generation_{0};
  QLabel* emptyLabel_{nullptr};
  bool hasFrame_{false};
  AspectMode aspectMode_{AspectMode::native};
  ScaleMode scaleMode_{ScaleMode::fit};
  VideoFilter videoFilter_{VideoFilter::nearest};
  ShaderConfiguration shaderConfiguration_;
  double sourceFramesPerSecond_{60.0};
  std::uint64_t shaderConfigurationGeneration_{0U};
  OpenGLCanvas* openGLCanvas_{nullptr};
  std::function<void(std::string)> rendererFailureSink_;
  std::function<void(std::string)> shaderFailureSink_;
};

} // namespace genplusgx::video
