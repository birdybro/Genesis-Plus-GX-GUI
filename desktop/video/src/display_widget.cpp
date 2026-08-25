#include "genplusgx/video/display_widget.h"

#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QStackedLayout>
#include <QTimer>

#include <array>
#include <cmath>

namespace genplusgx::video {

class OpenGLCanvas final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
  explicit OpenGLCanvas(DisplayWidget& owner)
    : QOpenGLWidget(&owner), owner_(owner)
  {
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
        "uniform sampler2D frameTexture; in vec2 textureCoordinate; out vec4 outputColor;"
        "void main(){ outputColor=texture(frameTexture,textureCoordinate); }"
      : "#ifdef GL_ES\nprecision mediump float;\n#endif\n"
        "uniform sampler2D frameTexture; varying vec2 textureCoordinate;"
        "void main(){ gl_FragColor=texture2D(frameTexture,textureCoordinate); }";

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
    program_.release();
    vertexBuffer_.release();
    vertexArray_.release();

    glGenTextures(1, &texture_);
    initialized_ = texture_ != 0U;
    if (!initialized_) {
      owner_.scheduleSoftwareFallback();
    }
  }

  void paintGL() override
  {
    glViewport(0, 0,
      static_cast<GLsizei>(std::lround(width() * devicePixelRatioF())),
      static_cast<GLsizei>(std::lround(height() * devicePixelRatioF())));
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!initialized_ || !owner_.hasFrame_) {
      return;
    }

    const auto layout = owner_.currentLayout();
    if (!layout.valid()) {
      return;
    }
    const auto ratio = devicePixelRatioF();
    glViewport(
      static_cast<GLint>(std::lround(layout.x * ratio)),
      static_cast<GLint>(std::lround(
        (owner_.height() - layout.y - layout.height) * ratio)),
      static_cast<GLsizei>(std::lround(layout.width * ratio)),
      static_cast<GLsizei>(std::lround(layout.height * ratio)));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    const GLint filter = owner_.videoFilter_ == VideoFilter::bilinear
      ? GL_LINEAR
      : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    if (uploadedWidth_ != owner_.frame_.width ||
        uploadedHeight_ != owner_.frame_.height) {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
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

    vertexArray_.bind();
    program_.bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    program_.release();
    vertexArray_.release();
    glBindTexture(GL_TEXTURE_2D, 0);
  }

private:
  DisplayWidget& owner_;
  QOpenGLShaderProgram program_;
  QOpenGLBuffer vertexBuffer_{QOpenGLBuffer::VertexBuffer};
  QOpenGLVertexArrayObject vertexArray_;
  GLuint texture_{0U};
  std::uint32_t uploadedWidth_{0U};
  std::uint32_t uploadedHeight_{0U};
  std::uint64_t uploadedGeneration_{0U};
  bool initialized_{false};
};

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

  auto* layout = new QStackedLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setStackingMode(QStackedLayout::StackAll);
  const auto platform = QGuiApplication::platformName();
  const bool forceSoftware = qEnvironmentVariableIsSet(
    "GENPLUSGX_FORCE_SOFTWARE_VIDEO");
  if (!forceSoftware && platform != QStringLiteral("offscreen") &&
      platform != QStringLiteral("minimal")) {
    openGLCanvas_ = new OpenGLCanvas(*this);
    layout->addWidget(openGLCanvas_);
  }
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
  requestRepaint();
  return true;
}

void DisplayWidget::clearFrame()
{
  hasFrame_ = false;
  frame_ = {};
  generation_ = 0;
  emptyLabel_->show();
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
      openGLCanvas_->hide();
      openGLCanvas_->deleteLater();
      openGLCanvas_ = nullptr;
      update();
    }
  });
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
