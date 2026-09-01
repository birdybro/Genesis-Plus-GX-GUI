#include "genplusgx/video/display_widget.h"
#include "genplusgx/video/libretro_shader_runtime.h"

#include <QColor>
#include <QDebug>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QOffscreenSurface>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QStackedLayout>
#include <QSurfaceFormat>
#include <QTimer>

#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

namespace genplusgx::video {

namespace {

QSurfaceFormat acceleratedSurfaceFormat(
  const PresentationConfiguration& configuration)
{
  auto format = QSurfaceFormat::defaultFormat();
  format.setRenderableType(QSurfaceFormat::OpenGL);
  format.setVersion(3, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setSwapBehavior(
    configuration.buffering == PresentationBufferingMode::tripleBuffer
      ? QSurfaceFormat::TripleBuffer
      : QSurfaceFormat::DoubleBuffer);
  format.setSwapInterval(requestedSwapInterval(configuration.sync));
  return format;
}

bool canCreateOpenGLRenderer(const PresentationConfiguration& configuration)
{
  QOpenGLContext context;
  context.setFormat(acceleratedSurfaceFormat(configuration));
  if (!context.create() || !context.isValid()) {
    return false;
  }

  QOffscreenSurface surface;
  surface.setFormat(context.format());
  surface.create();
  if (!surface.isValid() || !context.makeCurrent(&surface)) {
    return false;
  }
  context.doneCurrent();
  return true;
}

} // namespace

void configureOpenGLSurfaceFormat(
  const PresentationConfiguration& configuration)
{
  const auto effective = validatePresentationConfiguration(configuration)
    ? configuration : PresentationConfiguration{};
  QSurfaceFormat::setDefaultFormat(acceleratedSurfaceFormat(effective));
}

class OpenGLCanvas final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
  explicit OpenGLCanvas(DisplayWidget& owner)
    : QOpenGLWidget(&owner), owner_(owner)
  {
    setFormat(acceleratedSurfaceFormat(owner.presentationConfiguration_));
    setObjectName(QStringLiteral("openGLCanvas"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    QObject::connect(this, &QOpenGLWidget::frameSwapped, this, [this] {
      owner_.noteFrameSwapped(submittedGeneration_);
    });
  }

  ~OpenGLCanvas() override
  {
    if (context() != nullptr && context()->isValid()) {
      makeCurrent();
      if (texture_ != 0U) {
        glDeleteTextures(1, &texture_);
      }
      if (artworkTexture_ != 0U) {
        glDeleteTextures(1, &artworkTexture_);
      }
      shaderRuntime_.reset();
      if (shaderOutputTexture_ != 0U) {
        glDeleteTextures(1, &shaderOutputTexture_);
      }
      vertexBuffer_.destroy();
      vertexArray_.destroy();
      program_.removeAllShaders();
      doneCurrent();
    }
  }

protected:
  void initializeGL() override
  {
    initializeOpenGLFunctions();
    glDisable(GL_DEPTH_TEST);

    const bool modernShader = !context()->isOpenGLES() &&
      (context()->format().majorVersion() > 3 ||
       (context()->format().majorVersion() == 3 &&
        context()->format().minorVersion() >= 2));
    const char* vertexSource = modernShader
      ? "#version 150\n"
        "in vec2 position; in vec2 textureInput; out vec2 textureCoordinate;"
        "void main(){ textureCoordinate=textureInput; gl_Position=vec4(position,0.0,1.0); }"
      : "attribute vec2 position; attribute vec2 textureInput; varying vec2 textureCoordinate;"
        "void main(){ textureCoordinate=textureInput; gl_Position=vec4(position,0.0,1.0); }";
    const char* fragmentSource = modernShader
      ? "#version 150\n"
        "uniform sampler2D frameTexture; uniform float opacity;"
        "in vec2 textureCoordinate; out vec4 outputColor;"
        "void main(){ vec4 color=texture(frameTexture,textureCoordinate); outputColor=vec4(color.rgb,color.a*opacity); }"
      : "#ifdef GL_ES\nprecision mediump float;\n#endif\n"
        "uniform sampler2D frameTexture; uniform float opacity;"
        "varying vec2 textureCoordinate;"
        "void main(){ vec4 color=texture2D(frameTexture,textureCoordinate); gl_FragColor=vec4(color.rgb,color.a*opacity); }";

    if (!program_.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexSource) ||
        !program_.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentSource) ||
        !program_.link()) {
      owner_.scheduleSoftwareFallback(this);
      return;
    }

    static constexpr std::array<float, 16> vertices{
      -1.0F, -1.0F, 0.0F, 1.0F,
       1.0F, -1.0F, 1.0F, 1.0F,
      -1.0F,  1.0F, 0.0F, 0.0F,
       1.0F,  1.0F, 1.0F, 0.0F};
    if (!vertexBuffer_.create() || !vertexArray_.create()) {
      owner_.scheduleSoftwareFallback(this);
      return;
    }

    vertexArray_.bind();
    vertexBuffer_.bind();
    vertexBuffer_.allocate(
      vertices.data(), static_cast<int>(vertices.size() * sizeof(float)));
    program_.bind();
    const int position = program_.attributeLocation("position");
    const int textureInput = program_.attributeLocation("textureInput");
    if (position < 0 || textureInput < 0) {
      program_.release();
      vertexBuffer_.release();
      vertexArray_.release();
      owner_.scheduleSoftwareFallback(this);
      return;
    }
    program_.enableAttributeArray(position);
    program_.setAttributeBuffer(position, GL_FLOAT, 0, 2, 4 * sizeof(float));
    program_.enableAttributeArray(textureInput);
    program_.setAttributeBuffer(
      textureInput, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
    program_.setUniformValue("frameTexture", 0);
    program_.setUniformValue("opacity", 1.0F);
    program_.release();
    vertexBuffer_.release();
    vertexArray_.release();

    glGenTextures(1, &texture_);
    glGenTextures(1, &artworkTexture_);
    glGenTextures(1, &shaderOutputTexture_);
    initialized_ = texture_ != 0U && artworkTexture_ != 0U &&
      shaderOutputTexture_ != 0U;
    if (!initialized_) {
      owner_.scheduleSoftwareFallback(this);
    } else {
      const auto actualFormat = context()->format();
      const auto actualBuffering =
        actualFormat.swapBehavior() == QSurfaceFormat::TripleBuffer
          ? PresentationBufferingMode::tripleBuffer
          : PresentationBufferingMode::doubleBuffer;
      owner_.noteRendererInitialized(
        actualFormat.swapInterval(), actualBuffering);
      qInfo().noquote() << "OpenGL renderer initialized:"
                        << context()->format().majorVersion() << '.'
                        << context()->format().minorVersion()
                        << "swap interval" << actualFormat.swapInterval()
                        << "buffering"
                        << (actualBuffering ==
                              PresentationBufferingMode::tripleBuffer
                            ? "triple" : "double");
    }
  }

  void paintGL() override
  {
    glViewport(0, 0,
      static_cast<GLsizei>(std::lround(width() * devicePixelRatioF())),
      static_cast<GLsizei>(std::lround(height() * devicePixelRatioF())));
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!initialized_) {
      return;
    }

    uploadArtworkIfNeeded();
    if (owner_.artworkConfiguration_.mode == ArtworkMode::bezel &&
        !owner_.artworkImage_.isNull()) {
      drawTexture(artworkTexture_, 0, 0, width(), height(),
        static_cast<float>(owner_.artworkConfiguration_.opacityPercent) /
          100.0F,
        true, GL_LINEAR);
    }
    if (!owner_.hasFrame_) {
      QPainter painter(this);
      painter.setPen(QColor{184, 184, 184});
      auto font = painter.font();
      font.setPixelSize(16);
      painter.setFont(font);
      painter.drawText(
        rect(), Qt::AlignCenter, owner_.emptyLabel_->text());
      return;
    }

    const auto layout = owner_.currentLayout();
    if (!layout.valid()) {
      return;
    }
    const auto ratio = devicePixelRatioF();
    const auto viewportX = static_cast<GLint>(std::lround(layout.x * ratio));
    const auto viewportY = static_cast<GLint>(std::lround(
      (owner_.height() - layout.y - layout.height) * ratio));
    const auto viewportWidth = static_cast<GLsizei>(
      std::lround(layout.width * ratio));
    const auto viewportHeight = static_cast<GLsizei>(
      std::lround(layout.height * ratio));
    glViewport(viewportX, viewportY, viewportWidth, viewportHeight);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    const GLint filter = owner_.videoFilter_ == VideoFilter::bilinear
      ? GL_LINEAR
      : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    if (uploadedWidth_ != owner_.frame_.width ||
        uploadedHeight_ != owner_.frame_.height) {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
        static_cast<GLsizei>(owner_.frame_.width),
        static_cast<GLsizei>(owner_.frame_.height), 0, GL_RGB,
        GL_UNSIGNED_SHORT_5_6_5, owner_.pixels_.data());
      uploadedWidth_ = owner_.frame_.width;
      uploadedHeight_ = owner_.frame_.height;
      uploadedGeneration_ = owner_.generation_;
    } else if (uploadedGeneration_ != owner_.generation_) {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
        static_cast<GLsizei>(owner_.frame_.width),
        static_cast<GLsizei>(owner_.frame_.height), GL_RGB,
        GL_UNSIGNED_SHORT_5_6_5, owner_.pixels_.data());
      uploadedGeneration_ = owner_.generation_;
    }

    GLuint presentationTexture = texture_;
    if (owner_.shaderConfiguration_.mode != ShaderMode::disabled) {
      ensureShaderOutputTexture(
        static_cast<std::uint32_t>(viewportWidth),
        static_cast<std::uint32_t>(viewportHeight));
      ensureShaderConfiguration();
      if (shaderRuntime_.isInitialized()) {
        if (shaderRuntime_.render(texture_, GL_RGBA8,
              owner_.frame_.width, owner_.frame_.height,
              shaderOutputTexture_, GL_RGBA8,
              shaderOutputWidth_, shaderOutputHeight_, owner_.frame_.frameNumber,
              static_cast<float>(owner_.sourceFramesPerSecond_),
              static_cast<float>(layout.width) /
                static_cast<float>(layout.height))) {
          presentationTexture = shaderOutputTexture_;
        } else if (failedShaderGeneration_ !=
                   owner_.shaderConfigurationGeneration_) {
          failedShaderGeneration_ = owner_.shaderConfigurationGeneration_;
          owner_.reportShaderFailure(shaderRuntime_.lastError());
          shaderRuntime_.reset();
        }
      }
      glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
      glViewport(viewportX, viewportY, viewportWidth, viewportHeight);
      glDisable(GL_BLEND);
      glDisable(GL_CULL_FACE);
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_SCISSOR_TEST);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glDepthMask(GL_FALSE);
      glActiveTexture(GL_TEXTURE0);
      context()->extraFunctions()->glBindSampler(0U, 0U);
      glBindTexture(GL_TEXTURE_2D, presentationTexture);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else if (appliedShaderGeneration_ !=
               owner_.shaderConfigurationGeneration_) {
      shaderRuntime_.reset();
      appliedShaderGeneration_ = owner_.shaderConfigurationGeneration_;
      failedShaderGeneration_ = std::numeric_limits<std::uint64_t>::max();
    }

    drawTexture(presentationTexture, layout.x, layout.y,
      layout.width, layout.height, 1.0F, false,
      owner_.shaderConfiguration_.mode != ShaderMode::disabled
        ? GL_LINEAR : filter);
    if (owner_.artworkConfiguration_.mode == ArtworkMode::overlay &&
        !owner_.artworkImage_.isNull()) {
      drawTexture(artworkTexture_, 0, 0, width(), height(),
        static_cast<float>(owner_.artworkConfiguration_.opacityPercent) /
          100.0F,
        true, GL_LINEAR);
    }
    submittedGeneration_ = owner_.generation_;
    owner_.noteFrameRendered(submittedGeneration_);
  }

private:
  void drawTexture(GLuint texture, int x, int y, int targetWidth,
    int targetHeight, float opacity, bool blend, GLint filter)
  {
    const auto ratio = devicePixelRatioF();
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glViewport(
      static_cast<GLint>(std::lround(x * ratio)),
      static_cast<GLint>(std::lround(
        (height() - y - targetHeight) * ratio)),
      static_cast<GLsizei>(std::lround(targetWidth * ratio)),
      static_cast<GLsizei>(std::lround(targetHeight * ratio)));
    if (blend) {
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
      glDisable(GL_BLEND);
    }
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);
    glActiveTexture(GL_TEXTURE0);
    context()->extraFunctions()->glBindSampler(0U, 0U);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    vertexArray_.bind();
    program_.bind();
    program_.setUniformValue("frameTexture", 0);
    program_.setUniformValue("opacity", opacity);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    program_.release();
    vertexArray_.release();
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
  }

  void uploadArtworkIfNeeded()
  {
    if (uploadedArtworkGeneration_ ==
        owner_.artworkConfigurationGeneration_) {
      return;
    }
    uploadedArtworkGeneration_ = owner_.artworkConfigurationGeneration_;
    if (owner_.artworkImage_.isNull()) {
      return;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, artworkTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
      owner_.artworkImage_.width(), owner_.artworkImage_.height(), 0,
      GL_RGBA, GL_UNSIGNED_BYTE, owner_.artworkImage_.constBits());
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  void ensureShaderConfiguration()
  {
    if (appliedShaderGeneration_ == owner_.shaderConfigurationGeneration_) {
      return;
    }
    shaderRuntime_.reset();
    appliedShaderGeneration_ = owner_.shaderConfigurationGeneration_;
    failedShaderGeneration_ = std::numeric_limits<std::uint64_t>::max();
    if (!shaderRuntime_.initialize(owner_.shaderConfiguration_)) {
      failedShaderGeneration_ = owner_.shaderConfigurationGeneration_;
      owner_.reportShaderFailure(shaderRuntime_.lastError());
    }
  }

  void ensureShaderOutputTexture(std::uint32_t width, std::uint32_t height)
  {
    if (shaderOutputWidth_ == width && shaderOutputHeight_ == height) {
      return;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, shaderOutputTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
      static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
      GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    shaderOutputWidth_ = width;
    shaderOutputHeight_ = height;
  }

  DisplayWidget& owner_;
  QOpenGLShaderProgram program_;
  QOpenGLBuffer vertexBuffer_{QOpenGLBuffer::VertexBuffer};
  QOpenGLVertexArrayObject vertexArray_;
  GLuint texture_{0U};
  GLuint artworkTexture_{0U};
  GLuint shaderOutputTexture_{0U};
  std::uint32_t uploadedWidth_{0U};
  std::uint32_t uploadedHeight_{0U};
  std::uint64_t uploadedGeneration_{0U};
  std::uint64_t uploadedArtworkGeneration_{
    std::numeric_limits<std::uint64_t>::max()};
  std::uint64_t appliedShaderGeneration_{std::numeric_limits<std::uint64_t>::max()};
  std::uint64_t failedShaderGeneration_{std::numeric_limits<std::uint64_t>::max()};
  std::uint64_t submittedGeneration_{0U};
  std::uint32_t shaderOutputWidth_{0U};
  std::uint32_t shaderOutputHeight_{0U};
  LibretroShaderRuntime shaderRuntime_;
  bool initialized_{false};
};

DisplayWidget::DisplayWidget(QWidget* parent)
  : QWidget(parent),
    exchange_(std::make_shared<VideoFrameExchange>()),
    pixels_(VideoFrameExchange::maximumSurfacePixels, 0U)
{
  setObjectName(QStringLiteral("emulatorCanvas"));
  setAccessibleName(tr("Emulator display"));
  setFocusPolicy(Qt::StrongFocus);
  setAutoFillBackground(false);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setMinimumSize(320, 240);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  stackedLayout_ = new QStackedLayout(this);
  stackedLayout_->setContentsMargins(0, 0, 0, 0);
  stackedLayout_->setStackingMode(QStackedLayout::StackAll);
  emptyLabel_ = new QLabel(tr("Open or drop a game to begin"), this);
  emptyLabel_->setObjectName(QStringLiteral("emptyCanvasLabel"));
  emptyLabel_->setAlignment(Qt::AlignCenter);
  emptyLabel_->setStyleSheet(QStringLiteral("color: #b8b8b8; font-size: 16px;"));
  emptyLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
  stackedLayout_->addWidget(emptyLabel_);
  rebuildAcceleratedRenderer();
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
  presentationTelemetry_.frameReceived(generation_);
  hasFrame_ = true;
  emptyLabel_->hide();
  requestRepaint();
  return true;
}

void DisplayWidget::clearFrame()
{
  hasFrame_ = false;
  frame_ = {};
  generation_ = 0;
  presentationTelemetry_.cancelPending();
  emptyLabel_->setVisible(openGLCanvas_ == nullptr);
  requestRepaint();
}

void DisplayWidget::setAspectMode(AspectMode mode)
{
  if (aspectMode_ != mode) {
    aspectMode_ = mode;
    requestRepaint();
  }
}

void DisplayWidget::setScaleMode(ScaleMode mode)
{
  if (scaleMode_ != mode) {
    scaleMode_ = mode;
    requestRepaint();
  }
}

void DisplayWidget::setVideoFilter(VideoFilter filter)
{
  if (videoFilter_ != filter) {
    videoFilter_ = filter;
    requestRepaint();
  }
}

void DisplayWidget::setPresentationConfiguration(
  PresentationConfiguration configuration)
{
  if (!validatePresentationConfiguration(configuration) ||
      presentationConfiguration_ == configuration) {
    return;
  }
  presentationConfiguration_ = configuration;
  rebuildAcceleratedRenderer();
}

void DisplayWidget::setShaderConfiguration(ShaderConfiguration configuration)
{
  if (!validateShaderConfiguration(configuration) ||
      shaderConfiguration_ == configuration) {
    return;
  }
  shaderConfiguration_ = std::move(configuration);
  ++shaderConfigurationGeneration_;
  if (openGLCanvas_ == nullptr &&
      shaderConfiguration_.mode != ShaderMode::disabled) {
    reportShaderFailure(
      "Libretro shaders require an OpenGL 3.3 renderer. Normal unshaded "
      "output remains active on this display.");
  }
  requestRepaint();
}

bool DisplayWidget::setArtworkConfiguration(
  ArtworkConfiguration configuration)
{
  if (configuration == artworkConfiguration_ &&
      (configuration.mode == ArtworkMode::disabled ||
       !artworkImage_.isNull())) {
    return true;
  }
  auto result = loadArtworkImage(configuration);
  artworkConfiguration_ = std::move(configuration);
  artworkImage_ = std::move(result.image);
  artworkError_ = std::move(result.error);
  artworkFormat_ = std::move(result.format);
  artworkFileBytes_ = result.fileBytes;
  ++artworkConfigurationGeneration_;
  if (!result.success) {
    reportArtworkFailure(artworkError_);
  }
  requestRepaint();
  return result.success;
}

void DisplayWidget::setSourceFramesPerSecond(double framesPerSecond)
{
  if (std::isfinite(framesPerSecond) && framesPerSecond >= 1.0 &&
      framesPerSecond <= 1000.0) {
    sourceFramesPerSecond_ = framesPerSecond;
  }
}

void DisplayWidget::setRendererFailureSink(
  std::function<void(std::string)> sink)
{
  rendererFailureSink_ = std::move(sink);
}

void DisplayWidget::setShaderFailureSink(
  std::function<void(std::string)> sink)
{
  shaderFailureSink_ = std::move(sink);
}

void DisplayWidget::setArtworkFailureSink(
  std::function<void(std::string)> sink)
{
  artworkFailureSink_ = std::move(sink);
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

AspectMode DisplayWidget::aspectMode() const noexcept
{
  return aspectMode_;
}

ScaleMode DisplayWidget::scaleMode() const noexcept
{
  return scaleMode_;
}

VideoFilter DisplayWidget::videoFilter() const noexcept
{
  return videoFilter_;
}

const PresentationConfiguration&
DisplayWidget::presentationConfiguration() const noexcept
{
  return presentationConfiguration_;
}

DisplayPresentationMetrics DisplayWidget::presentationMetrics() const
{
  const auto exchangeMetrics = exchange_ != nullptr
    ? exchange_->metrics() : VideoExchangeMetrics{};
  return {
    .requested = presentationConfiguration_,
    .effectiveBuffering = effectiveBuffering_,
    .exchange = exchangeMetrics,
    .telemetry = presentationTelemetry_.snapshot(),
    .effectiveSwapInterval = effectiveSwapInterval_,
    .rendererInitialized = rendererInitialized_,
    .accelerated = openGLCanvas_ != nullptr,
    .swapIntervalHonored = rendererInitialized_ &&
      effectiveSwapInterval_ == requestedSwapInterval(
        presentationConfiguration_.sync),
    .bufferingHonored = rendererInitialized_ &&
      effectiveBuffering_ == presentationConfiguration_.buffering,
  };
}

const ShaderConfiguration& DisplayWidget::shaderConfiguration() const noexcept
{
  return shaderConfiguration_;
}

const ArtworkConfiguration&
DisplayWidget::artworkConfiguration() const noexcept
{
  return artworkConfiguration_;
}

bool DisplayWidget::artworkAvailable() const noexcept
{
  return artworkConfiguration_.mode != ArtworkMode::disabled &&
    !artworkImage_.isNull();
}

const std::string& DisplayWidget::artworkError() const noexcept
{
  return artworkError_;
}

std::uintmax_t DisplayWidget::artworkFileBytes() const noexcept
{
  return artworkFileBytes_;
}

const std::string& DisplayWidget::artworkFormat() const noexcept
{
  return artworkFormat_;
}

std::uint32_t DisplayWidget::artworkWidth() const noexcept
{
  return artworkImage_.isNull()
    ? 0U : static_cast<std::uint32_t>(artworkImage_.width());
}

std::uint32_t DisplayWidget::artworkHeight() const noexcept
{
  return artworkImage_.isNull()
    ? 0U : static_cast<std::uint32_t>(artworkImage_.height());
}

double DisplayWidget::sourceFramesPerSecond() const noexcept
{
  return sourceFramesPerSecond_;
}

VideoLayout DisplayWidget::currentLayout() const noexcept
{
  if (!hasFrame_) {
    return {};
  }
  auto effectiveArtwork = artworkConfiguration_;
  if (artworkImage_.isNull()) {
    effectiveArtwork.mode = ArtworkMode::disabled;
    effectiveArtwork.constrainVideoToViewport = false;
  }
  return calculateArtworkVideoLayout(frame_.width, frame_.height,
    width(), height(), aspectMode_, scaleMode_, effectiveArtwork);
}

bool DisplayWidget::usesAcceleratedRenderer() const noexcept
{
  return openGLCanvas_ != nullptr;
}

void DisplayWidget::requestRepaint()
{
  if (openGLCanvas_ != nullptr) {
    openGLCanvas_->update();
  } else {
    update();
  }
}

void DisplayWidget::rebuildAcceleratedRenderer()
{
  if (openGLCanvas_ != nullptr) {
    auto* previous = openGLCanvas_;
    openGLCanvas_ = nullptr;
    stackedLayout_->removeWidget(previous);
    delete previous;
  }
  rendererInitialized_ = false;
  effectiveSwapInterval_ = 0;
  effectiveBuffering_ = PresentationBufferingMode::doubleBuffer;

  const auto platform = QGuiApplication::platformName();
  const bool forceSoftware = qEnvironmentVariableIsSet(
    "GENPLUSGX_FORCE_SOFTWARE_VIDEO");
  if (!forceSoftware && platform != QStringLiteral("offscreen") &&
      platform != QStringLiteral("minimal")) {
    if (canCreateOpenGLRenderer(presentationConfiguration_)) {
      openGLCanvas_ = new OpenGLCanvas(*this);
      stackedLayout_->insertWidget(0, openGLCanvas_);
      openGLCanvas_->show();
    } else {
      qWarning().noquote()
        << "OpenGL context preflight failed; using Qt software rendering.";
    }
  }
  if (emptyLabel_ != nullptr) {
    emptyLabel_->setVisible(openGLCanvas_ == nullptr && !hasFrame_);
  }
  requestRepaint();
}

void DisplayWidget::scheduleSoftwareFallback(OpenGLCanvas* failedCanvas)
{
  if (openGLCanvas_ == nullptr || openGLCanvas_ != failedCanvas) {
    return;
  }
  QTimer::singleShot(0, this, [this, failedCanvas] {
    if (openGLCanvas_ == failedCanvas) {
      constexpr auto detail =
        "OpenGL renderer initialization failed; using Qt software rendering.";
      qWarning() << detail;
      openGLCanvas_->hide();
      openGLCanvas_->deleteLater();
      openGLCanvas_ = nullptr;
      rendererInitialized_ = false;
      emptyLabel_->setVisible(!hasFrame_);
      update();
      if (rendererFailureSink_) {
        rendererFailureSink_(detail);
      }
    }
  });
}

void DisplayWidget::noteFrameRendered(std::uint64_t generation)
{
  if (hasFrame_ && generation != 0U) {
    presentationTelemetry_.frameRendered(generation);
  }
}

void DisplayWidget::noteFrameSwapped(std::uint64_t generation)
{
  if (!hasFrame_ || generation == 0U) {
    return;
  }
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  const auto microseconds = std::chrono::duration_cast<
    std::chrono::microseconds>(now).count();
  if (microseconds > 0) {
    presentationTelemetry_.frameSwapped(
      generation, static_cast<std::uint64_t>(microseconds));
  }
}

void DisplayWidget::noteRendererInitialized(
  int swapInterval,
  PresentationBufferingMode buffering)
{
  effectiveSwapInterval_ = swapInterval;
  effectiveBuffering_ = buffering;
  rendererInitialized_ = true;
}

void DisplayWidget::reportShaderFailure(std::string detail)
{
  if (detail.empty()) {
    detail = "The Libretro shader failed for an unknown reason.";
  }
  qWarning().noquote() << "Libretro shader disabled for this frame:"
                       << QString::fromStdString(detail);
  if (shaderFailureSink_) {
    QTimer::singleShot(0, this,
      [this, detail = std::move(detail)] {
        if (shaderFailureSink_) {
          shaderFailureSink_(detail);
        }
      });
  }
}

void DisplayWidget::reportArtworkFailure(std::string detail)
{
  if (detail.empty()) {
    detail = "The local artwork could not be loaded for an unknown reason.";
  }
  qWarning().noquote() << "Local video artwork disabled:"
                       << QString::fromStdString(detail);
  if (artworkFailureSink_) {
    QTimer::singleShot(0, this,
      [this, detail = std::move(detail)] {
        if (artworkFailureSink_) {
          artworkFailureSink_(detail);
        }
      });
  }
}

void DisplayWidget::paintEvent(QPaintEvent* event)
{
  static_cast<void>(event);
  QPainter painter(this);
  if (openGLCanvas_ != nullptr) {
    painter.fillRect(rect(), Qt::black);
    return;
  }
  paintSoftwareFrame(painter);
}

void DisplayWidget::paintSoftwareFrame(QPainter& painter)
{
  painter.fillRect(rect(), Qt::black);
  if (artworkConfiguration_.mode == ArtworkMode::bezel &&
      !artworkImage_.isNull()) {
    painter.save();
    painter.setOpacity(
      static_cast<qreal>(artworkConfiguration_.opacityPercent) / 100.0);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(rect(), artworkImage_);
    painter.restore();
  }
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

  const auto layout = currentLayout();
  if (!layout.valid()) {
    return;
  }
  const QRect target{layout.x, layout.y, layout.width, layout.height};

  painter.setRenderHint(
    QPainter::SmoothPixmapTransform, videoFilter_ == VideoFilter::bilinear);
  painter.drawImage(target, image);
  if (artworkConfiguration_.mode == ArtworkMode::overlay &&
      !artworkImage_.isNull()) {
    painter.save();
    painter.setOpacity(
      static_cast<qreal>(artworkConfiguration_.opacityPercent) / 100.0);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(rect(), artworkImage_);
    painter.restore();
  }
  noteFrameRendered(generation_);
  noteFrameSwapped(generation_);
}

} // namespace genplusgx::video
