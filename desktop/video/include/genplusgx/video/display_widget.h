#pragma once

#include "genplusgx/video/artwork_configuration.h"
#include "genplusgx/video/artwork_image.h"
#include "genplusgx/video/frame_exchange.h"
#include "genplusgx/video/presentation.h"
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
class QStackedLayout;

namespace genplusgx::video {

class OpenGLCanvas;

// Must be called before QApplication is constructed so Qt's backing-store
// compositor and QOpenGLWidget create mutually shareable contexts.
void configureOpenGLSurfaceFormat(
  const PresentationConfiguration& configuration = {});

struct DisplayPresentationMetrics final {
  PresentationConfiguration requested;
  PresentationBufferingMode effectiveBuffering{
    PresentationBufferingMode::doubleBuffer};
  VideoExchangeMetrics exchange;
  PresentationTelemetrySnapshot telemetry;
  int effectiveSwapInterval{0};
  bool rendererInitialized{false};
  bool accelerated{false};
  bool swapIntervalHonored{false};
  bool bufferingHonored{false};
};

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
  void setPresentationConfiguration(PresentationConfiguration configuration);
  void setShaderConfiguration(ShaderConfiguration configuration);
  [[nodiscard]] bool setArtworkConfiguration(
    ArtworkConfiguration configuration);
  void setSourceFramesPerSecond(double framesPerSecond);
  void setRendererFailureSink(std::function<void(std::string)> sink);
  void setShaderFailureSink(std::function<void(std::string)> sink);
  void setArtworkFailureSink(std::function<void(std::string)> sink);

  [[nodiscard]] bool hasFrame() const noexcept;
  [[nodiscard]] const CoreVideoFrameInfo& currentFrameInfo() const noexcept;
  [[nodiscard]] std::uint64_t currentGeneration() const noexcept;
  [[nodiscard]] std::span<const std::uint16_t> currentPixels() const noexcept;
  [[nodiscard]] AspectMode aspectMode() const noexcept;
  [[nodiscard]] ScaleMode scaleMode() const noexcept;
  [[nodiscard]] VideoFilter videoFilter() const noexcept;
  [[nodiscard]] const PresentationConfiguration&
    presentationConfiguration() const noexcept;
  [[nodiscard]] DisplayPresentationMetrics presentationMetrics() const;
  [[nodiscard]] const ShaderConfiguration& shaderConfiguration() const noexcept;
  [[nodiscard]] const ArtworkConfiguration&
    artworkConfiguration() const noexcept;
  [[nodiscard]] bool artworkAvailable() const noexcept;
  [[nodiscard]] const std::string& artworkError() const noexcept;
  [[nodiscard]] std::uintmax_t artworkFileBytes() const noexcept;
  [[nodiscard]] const std::string& artworkFormat() const noexcept;
  [[nodiscard]] std::uint32_t artworkWidth() const noexcept;
  [[nodiscard]] std::uint32_t artworkHeight() const noexcept;
  [[nodiscard]] double sourceFramesPerSecond() const noexcept;
  [[nodiscard]] VideoLayout currentLayout() const noexcept;
  [[nodiscard]] bool usesAcceleratedRenderer() const noexcept;

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  friend class OpenGLCanvas;
  void paintSoftwareFrame(QPainter& painter);
  void rebuildAcceleratedRenderer();
  void requestRepaint();
  void scheduleSoftwareFallback(OpenGLCanvas* failedCanvas);
  void reportShaderFailure(std::string detail);
  void reportArtworkFailure(std::string detail);
  void noteFrameRendered(std::uint64_t generation);
  void noteFrameSwapped(std::uint64_t generation);
  void noteRendererInitialized(
    int swapInterval,
    PresentationBufferingMode buffering);

  std::shared_ptr<VideoFrameExchange> exchange_;
  std::vector<std::uint16_t> pixels_;
  CoreVideoFrameInfo frame_;
  std::uint64_t generation_{0};
  QLabel* emptyLabel_{nullptr};
  QStackedLayout* stackedLayout_{nullptr};
  bool hasFrame_{false};
  AspectMode aspectMode_{AspectMode::native};
  ScaleMode scaleMode_{ScaleMode::fit};
  VideoFilter videoFilter_{VideoFilter::nearest};
  PresentationConfiguration presentationConfiguration_;
  PresentationTelemetry presentationTelemetry_;
  ShaderConfiguration shaderConfiguration_;
  ArtworkConfiguration artworkConfiguration_;
  QImage artworkImage_;
  std::string artworkError_;
  std::string artworkFormat_;
  std::uintmax_t artworkFileBytes_{0U};
  double sourceFramesPerSecond_{60.0};
  std::uint64_t shaderConfigurationGeneration_{0U};
  std::uint64_t artworkConfigurationGeneration_{0U};
  OpenGLCanvas* openGLCanvas_{nullptr};
  int effectiveSwapInterval_{0};
  PresentationBufferingMode effectiveBuffering_{
    PresentationBufferingMode::doubleBuffer};
  bool rendererInitialized_{false};
  std::function<void(std::string)> rendererFailureSink_;
  std::function<void(std::string)> shaderFailureSink_;
  std::function<void(std::string)> artworkFailureSink_;
};

} // namespace genplusgx::video
