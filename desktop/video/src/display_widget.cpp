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
#include <cmath>
#include <limits>
#include <utility>

namespace genplusgx::video {

namespace {

QSurfaceFormat acceleratedSurfaceFormat()
{
  auto format = QSurfaceFormat::defaultFormat();
  format.setRenderableType(QSurfaceFormat::OpenGL);
  format.setVersion(3, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
  return format;
}

bool canCreateOpenGLRenderer()
{
  QOpenGLContext context;
  context.setFormat(acceleratedSurfaceFormat());
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

class OpenGLCanvas final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
  explicit OpenGLCanvas(DisplayWidget& owner)
    : QOpenGLWidget(&owner), owner_(owner)
  {
    setFormat(acceleratedSurfaceFormat());
    setObjectName(QStringLiteral("openGLCanvas"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
  }

  ~OpenGLCanvas() override
  {
    if (context() != nullptr && context()->isValid()) {
      makeCurrent();
      if (texture_ != 0U) {
        glDeleteTextures(1, &texture_);
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
        "uniform sampler2D frameTexture; uniform float flipTexture;"
        "in vec2 textureCoordinate; out vec4 outputColor;"
        "void main(){ vec2 uv=vec2(textureCoordinate.x,mix(textureCoordinate.y,"
        "1.0-textureCoordinate.y,flipTexture)); outputColor=texture(frameTexture,uv); }"
      : "#ifdef GL_ES\nprecision mediump float;\n#endif\n"
        "uniform sampler2D frameTexture; uniform float flipTexture;"
        "varying vec2 textureCoordinate;"
        "void main(){ vec2 uv=vec2(textureCoordinate.x,mix(textureCoordinate.y,"
        "1.0-textureCoordinate.y,flipTexture)); gl_FragColor=texture2D(frameTexture,uv); }";

    if (!program_.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexSource) ||
        !program_.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentSource) ||
        !program_.link()) {
      owner_.scheduleSoftwareFallback();
      return;
    }

    static constexpr std::array<float, 16> vertices{
      -1.0F, -1.0F, 0.0F, 1.0F,
       1.0F, -1.0F, 1.0F, 1.0F,
      -1.0F,  1.0F, 0.0F, 0.0F,
       1.0F,  1.0F, 1.0F, 0.0F};
    if (!vertexBuffer_.create() || !vertexArray_.create()) {
      owner_.scheduleSoftwareFallback();
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
      owner_.scheduleSoftwareFallback();
      return;
    }
    program_.enableAttributeArray(position);
    program_.setAttributeBuffer(position, GL_FLOAT, 0, 2, 4 * sizeof(float));
    program_.enableAttributeArray(textureInput);
    program_.setAttributeBuffer(
      textureInput, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
    program_.setUniformValue("frameTexture", 0);
    program_.setUniformValue("flipTexture", 0.0F);
    program_.release();
    vertexBuffer_.release();
    vertexArray_.release();

    glGenTextures(1, &texture_);
    glGenTextures(1, &shaderOutputTexture_);
    initialized_ = texture_ != 0U && shaderOutputTexture_ != 0U;
    if (!initialized_) {
      owner_.scheduleSoftwareFallback();
    } else {
      qInfo().noquote() << "OpenGL renderer initialized:"
                        << context()->format().majorVersion() << '.'
                        << context()->format().minorVersion();
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
    float flipTexture = 0.0F;
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
          flipTexture = 1.0F;
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

    vertexArray_.bind();
    program_.bind();
    program_.setUniformValue("frameTexture", 0);
    program_.setUniformValue("flipTexture", flipTexture);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    program_.release();
    vertexArray_.release();
    glBindTexture(GL_TEXTURE_2D, 0);
  }

private:
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
  GLuint shaderOutputTexture_{0U};
  std::uint32_t uploadedWidth_{0U};
  std::uint32_t uploadedHeight_{0U};
  std::uint64_t uploadedGeneration_{0U};
  std::uint64_t appliedShaderGeneration_{std::numeric_limits<std::uint64_t>::max()};
  std::uint64_t failedShaderGeneration_{std::numeric_limits<std::uint64_t>::max()};
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

  auto* layout = new QStackedLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setStackingMode(QStackedLayout::StackAll);
  const auto platform = QGuiApplication::platformName();
  const bool forceSoftware = qEnvironmentVariableIsSet(
    "GENPLUSGX_FORCE_SOFTWARE_VIDEO");
  if (!forceSoftware && platform != QStringLiteral("offscreen") &&
      platform != QStringLiteral("minimal")) {
    if (canCreateOpenGLRenderer()) {
      openGLCanvas_ = new OpenGLCanvas(*this);
      layout->addWidget(openGLCanvas_);
    } else {
      qWarning().noquote()
        << "OpenGL context preflight failed; using Qt software rendering.";
    }
  }
  emptyLabel_ = new QLabel(tr("Open or drop a game to begin"), this);
  emptyLabel_->setObjectName(QStringLiteral("emptyCanvasLabel"));
  emptyLabel_->setAlignment(Qt::AlignCenter);
  emptyLabel_->setStyleSheet(QStringLiteral("color: #b8b8b8; font-size: 16px;"));
  emptyLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
  layout->addWidget(emptyLabel_);
  emptyLabel_->setVisible(openGLCanvas_ == nullptr);
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
  requestRepaint();
  return true;
}

void DisplayWidget::clearFrame()
{
  hasFrame_ = false;
  frame_ = {};
  generation_ = 0;
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

const ShaderConfiguration& DisplayWidget::shaderConfiguration() const noexcept
{
  return shaderConfiguration_;
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
  return calculateVideoLayout(
    frame_.width, frame_.height, width(), height(), aspectMode_, scaleMode_);
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

void DisplayWidget::scheduleSoftwareFallback()
{
  if (openGLCanvas_ == nullptr) {
    return;
  }
  QTimer::singleShot(0, this, [this] {
    if (openGLCanvas_ != nullptr) {
      constexpr auto detail =
        "OpenGL renderer initialization failed; using Qt software rendering.";
      qWarning() << detail;
      openGLCanvas_->hide();
      openGLCanvas_->deleteLater();
      openGLCanvas_ = nullptr;
      emptyLabel_->setVisible(!hasFrame_);
      update();
      if (rendererFailureSink_) {
        rendererFailureSink_(detail);
      }
    }
  });
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
}

} // namespace genplusgx::video
