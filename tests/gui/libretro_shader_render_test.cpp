#include "genplusgx/video/libretro_shader_runtime.h"
#include "genplusgx/video/display_widget.h"

#include <QApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QOpenGLWidget>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr int skipped = 77;

struct AverageColor final {
  double red{0.0};
  double green{0.0};
  double blue{0.0};
};

AverageColor averageColor(const QImage& image, const QRect& area)
{
  AverageColor result;
  std::size_t count = 0U;
  const auto bounded = area.intersected(image.rect());
  for (int y = bounded.top(); y <= bounded.bottom(); ++y) {
    for (int x = bounded.left(); x <= bounded.right(); ++x) {
      const auto color = image.pixelColor(x, y);
      result.red += color.redF();
      result.green += color.greenF();
      result.blue += color.blueF();
      ++count;
    }
  }
  if (count != 0U) {
    result.red /= static_cast<double>(count);
    result.green /= static_cast<double>(count);
    result.blue /= static_cast<double>(count);
  }
  return result;
}

bool quadrantsAreUpright(
  const QImage& image,
  const genplusgx::video::VideoLayout& layout)
{
  if (!layout.valid()) {
    return false;
  }
  // Sample the interior of each quadrant. Keeping away from the outer 15%
  // makes this valid even at the built-in CRT shader's maximum curvature.
  const auto region = [&layout](double x, double y) {
    return QRect{
      layout.x + static_cast<int>(std::lround(layout.width * x)),
      layout.y + static_cast<int>(std::lround(layout.height * y)),
      std::max(1, static_cast<int>(std::lround(layout.width * 0.20))),
      std::max(1, static_cast<int>(std::lround(layout.height * 0.20))),
    };
  };
  const auto topLeft = averageColor(image, region(0.15, 0.15));
  const auto topRight = averageColor(image, region(0.65, 0.15));
  const auto bottomLeft = averageColor(image, region(0.15, 0.65));
  const auto bottomRight = averageColor(image, region(0.65, 0.65));
  return topLeft.red > topLeft.green * 1.5 &&
    topLeft.red > topLeft.blue * 1.5 &&
    topRight.green > topRight.red * 1.5 &&
    topRight.green > topRight.blue * 1.5 &&
    bottomLeft.blue > bottomLeft.red * 1.5 &&
    bottomLeft.blue > bottomLeft.green * 1.5 &&
    bottomRight.red > 0.15 && bottomRight.green > 0.15 &&
    bottomRight.blue > 0.15;
}

int unavailable(const char* detail)
{
  if (qEnvironmentVariableIsSet("GENPLUSGX_REQUIRE_OPENGL_SHADER_TEST")) {
    std::cerr << detail << '\n';
    return 1;
  }
  std::cout << "SKIP: " << detail << '\n';
  return skipped;
}

} // namespace

int main(int argc, char** argv)
{
  genplusgx::video::configureOpenGLSurfaceFormat();
  const auto defaultFormat = QSurfaceFormat::defaultFormat();
  if (defaultFormat.renderableType() != QSurfaceFormat::OpenGL ||
      defaultFormat.majorVersion() < 3 ||
      (defaultFormat.majorVersion() == 3 &&
       defaultFormat.minorVersion() < 3) ||
      defaultFormat.profile() != QSurfaceFormat::CoreProfile) {
    std::cerr << "The application OpenGL surface format was not configured.\n";
    return 1;
  }
  QApplication application(argc, argv);
  QSurfaceFormat format;
  format.setRenderableType(QSurfaceFormat::OpenGL);
  format.setVersion(3, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
  QOpenGLContext context;
  context.setFormat(format);
  if (!context.create() || !context.isValid()) {
    return unavailable("An OpenGL 3.3 context could not be created.");
  }
  QOffscreenSurface surface;
  surface.setFormat(context.format());
  surface.create();
  if (!surface.isValid() || !context.makeCurrent(&surface)) {
    return unavailable("The OpenGL test surface could not be made current.");
  }
  const auto actualFormat = context.format();
  if (context.isOpenGLES() || actualFormat.majorVersion() < 3 ||
      (actualFormat.majorVersion() == 3 && actualFormat.minorVersion() < 3)) {
    context.doneCurrent();
    return unavailable("The host returned an OpenGL context older than 3.3.");
  }
  auto* gl = context.functions();
  gl->initializeOpenGLFunctions();

  GLuint inputTexture = 0U;
  GLuint outputTexture = 0U;
  GLuint framebuffer = 0U;
  gl->glGenTextures(1, &inputTexture);
  gl->glGenTextures(1, &outputTexture);
  gl->glGenFramebuffers(1, &framebuffer);
  constexpr std::array<std::uint8_t, 4U * 4U * 3U> inputPixels{
    255, 40, 40,  255, 40, 40,  40, 255, 40,  40, 255, 40,
    255, 40, 40,  255, 40, 40,  40, 255, 40,  40, 255, 40,
    40, 40, 255,  40, 40, 255,  255, 255, 255,  255, 255, 255,
    40, 40, 255,  40, 40, 255,  255, 255, 255,  255, 255, 255,
  };
  gl->glBindTexture(GL_TEXTURE_2D, inputTexture);
  gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
  gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGB,
    GL_UNSIGNED_BYTE, inputPixels.data());
  gl->glBindTexture(GL_TEXTURE_2D, outputTexture);
  gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
  gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 48, 0, GL_RGBA,
    GL_UNSIGNED_BYTE, nullptr);
  gl->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
    GL_TEXTURE_2D, inputTexture, 0);
  std::array<std::uint8_t, 4U> inputCheck{};
  gl->glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, inputCheck.data());
  gl->glBindFramebuffer(GL_FRAMEBUFFER, 0U);
  if (inputCheck[0] < 200U) {
    std::cerr << "The OpenGL shader input texture was not populated.\n";
    return 2;
  }

  bool rendered = false;
  std::string error;
  {
    genplusgx::video::LibretroShaderRuntime runtime;
    const genplusgx::video::ShaderConfiguration configuration{
      .mode = genplusgx::video::ShaderMode::libretroPreset,
      .presetPath = std::filesystem::path{GENPLUSGX_SHADER_TEST_FIXTURE_DIR} /
        "libretro-pass.slangp",
      .parameters = {},
    };
    if (!runtime.initialize(configuration)) {
      std::cerr << runtime.lastError() << '\n';
      return 2;
    }
    rendered = runtime.render(inputTexture, GL_RGBA8, 4U, 4U,
      outputTexture, GL_RGBA8, 64U, 48U, 1U, 60.0F, 4.0F / 3.0F);
    error = runtime.lastError();
    runtime.reset();
  }
  if (!rendered) {
    std::cerr << error << '\n';
    return 3;
  }
  const auto renderGlError = gl->glGetError();
  if (renderGlError != GL_NO_ERROR) {
    std::cerr << "The shader render left OpenGL error " << renderGlError << ".\n";
    return 3;
  }

  gl->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
    GL_TEXTURE_2D, outputTexture, 0);
  if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "The shader output framebuffer is incomplete.\n";
    return 4;
  }
  std::vector<std::uint8_t> outputPixels(64U * 48U * 4U, 0U);
  gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0U);
  gl->glReadPixels(0, 0, 64, 48, GL_RGBA, GL_UNSIGNED_BYTE,
    outputPixels.data());
  std::size_t nonBlack = 0U;
  for (std::size_t index = 0U; index < outputPixels.size(); index += 4U) {
    if (outputPixels[index] > 8U || outputPixels[index + 1U] > 8U ||
        outputPixels[index + 2U] > 8U) {
      ++nonBlack;
    }
  }
  gl->glDeleteFramebuffers(1, &framebuffer);
  gl->glDeleteTextures(1, &outputTexture);
  gl->glDeleteTextures(1, &inputTexture);
  context.doneCurrent();
  if (nonBlack < (64U * 48U) / 4U) {
    std::cerr << "The pass-through shader did not sample its input image.\n";
    return 5;
  }

  auto exchange = std::make_shared<genplusgx::VideoFrameExchange>(16U);
  QTemporaryDir artworkDirectory;
  if (!artworkDirectory.isValid()) {
    std::cerr << "The artwork test directory is unavailable.\n";
    return 6;
  }
  const auto artworkPath = std::filesystem::path{
    artworkDirectory.path().toStdString()} / "asymmetric-overlay.png";
  QImage artwork(320, 240, QImage::Format_ARGB32);
  artwork.fill(Qt::transparent);
  for (int y = 0; y < 20; ++y) {
    for (int x = 0; x < artwork.width(); ++x) {
      artwork.setPixelColor(x, y, QColor{255, 0, 255, 255});
      artwork.setPixelColor(x, artwork.height() - 1 - y,
        QColor{0, 255, 255, 255});
    }
  }
  if (!artwork.save(QString::fromStdString(artworkPath.string()), "PNG")) {
    std::cerr << "The asymmetric artwork fixture could not be written.\n";
    return 6;
  }
  genplusgx::video::DisplayWidget widget;
  std::string shaderFailure;
  widget.setShaderFailureSink([&shaderFailure](const std::string& detail) {
    shaderFailure = detail;
  });
  widget.setFrameExchange(exchange);
  widget.resize(320, 240);
  widget.show();
  if (!QTest::qWaitForWindowExposed(&widget)) {
    return unavailable("The display test window was not exposed.");
  }
  if (!widget.usesAcceleratedRenderer()) {
    return unavailable("The display widget could not initialize OpenGL.");
  }
  auto lease = exchange->beginWrite();
  if (!lease) {
    std::cerr << "A display test frame could not be acquired.\n";
    return 6;
  }
  std::array<std::uint16_t, 16U> displayPixels{
    0xf800U, 0xf800U, 0x07e0U, 0x07e0U,
    0xf800U, 0xf800U, 0x07e0U, 0x07e0U,
    0x001fU, 0x001fU, 0xffffU, 0xffffU,
    0x001fU, 0x001fU, 0xffffU, 0xffffU,
  };
  std::ranges::copy(displayPixels, lease->pixels().begin());
  if (!lease->publish({
        .format = genplusgx::CorePixelFormat::rgb565,
        .width = 4U,
        .height = 4U,
        .sourceSurfaceWidth = 4U,
        .sourceSurfaceHeight = 4U,
        .sourcePitchPixels = 4U,
        .frameNumber = 2U,
      }) || !widget.presentLatestFrame()) {
    std::cerr << "The display test frame could not be presented.\n";
    return 7;
  }
  QTest::qWait(50);
  auto* canvas = widget.findChild<QOpenGLWidget*>(
    QStringLiteral("openGLCanvas"));
  if (canvas == nullptr) {
    std::cerr << "The accelerated display canvas is missing.\n";
    return 8;
  }
  const auto baseline = canvas->grabFramebuffer();
  int baselineMaximum = 0;
  for (int y = 0; y < baseline.height(); ++y) {
    for (int x = 0; x < baseline.width(); ++x) {
      baselineMaximum = std::max(
        baselineMaximum, baseline.pixelColor(x, y).value());
    }
  }
  if (baseline.isNull() || baselineMaximum < 8) {
    std::cerr << "The display widget baseline framebuffer is empty.\n";
    return 8;
  }
  if (!quadrantsAreUpright(baseline, widget.currentLayout())) {
    std::cerr << "The unshaded display frame is not upright.\n";
    return 8;
  }
  widget.setShaderConfiguration({
    .mode = genplusgx::video::ShaderMode::builtinCrt,
    .presetPath = {},
    .parameters = {
      {.name = "CURVATURE", .value = 0.0F},
      {.name = "VIGNETTE", .value = 0.0F},
    },
  });
  if (widget.shaderConfiguration().mode !=
      genplusgx::video::ShaderMode::builtinCrt) {
    std::cerr << "The display widget rejected a valid shader configuration.\n";
    return 9;
  }
  QTest::qWait(50);
  const QImage displayed = canvas->grabFramebuffer();
  int maximumValue = 0;
  std::size_t changedPixels = 0U;
  for (int y = 0; y < displayed.height(); ++y) {
    for (int x = 0; x < displayed.width(); ++x) {
      maximumValue = std::max(maximumValue, displayed.pixelColor(x, y).value());
      if (baseline.size() == displayed.size() &&
          baseline.pixelColor(x, y).rgb() != displayed.pixelColor(x, y).rgb()) {
        ++changedPixels;
      }
    }
  }
  if (!shaderFailure.empty()) {
    std::cerr << "The display widget reported a shader failure: "
              << shaderFailure << '\n';
    return 9;
  }
  if (displayed.isNull() || maximumValue < 8) {
    std::cerr << "The display widget did not present the shader output ("
              << displayed.width() << 'x' << displayed.height()
              << ", maximum value " << maximumValue << ").\n";
    return 9;
  }
  if (baseline.size() != displayed.size() ||
      changedPixels < static_cast<std::size_t>(displayed.width())) {
    std::cerr << "The CRT preset did not materially change the presented image ("
              << changedPixels << " changed pixels).\n";
    return 9;
  }
  if (!quadrantsAreUpright(displayed, widget.currentLayout())) {
    std::cerr << "The CRT shader output is vertically inverted.\n";
    return 9;
  }
  if (!widget.setArtworkConfiguration({
        .mode = genplusgx::video::ArtworkMode::overlay,
        .imagePath = artworkPath,
        .opacityPercent = 100U,
        .constrainVideoToViewport = false,
        .viewportInsets = {},
      })) {
    std::cerr << "The valid OpenGL artwork fixture was rejected.\n";
    return 9;
  }
  QTest::qWait(30);
  const auto artworkOutput = canvas->grabFramebuffer();
  const auto topArtwork = averageColor(artworkOutput,
    QRect{20, 3, artworkOutput.width() - 40, 10});
  const auto bottomArtwork = averageColor(artworkOutput,
    QRect{20, artworkOutput.height() - 13,
      artworkOutput.width() - 40, 10});
  if (!(topArtwork.red > 0.7 && topArtwork.blue > 0.7 &&
        topArtwork.green < 0.3 && bottomArtwork.green > 0.7 &&
        bottomArtwork.blue > 0.7 && bottomArtwork.red < 0.3)) {
    std::cerr << "OpenGL artwork was inverted or not alpha-composited.\n";
    return 9;
  }

  const std::array aspects{
    genplusgx::video::AspectMode::native,
    genplusgx::video::AspectMode::fourThree,
    genplusgx::video::AspectMode::stretch,
  };
  const std::array scales{
    genplusgx::video::ScaleMode::fit,
    genplusgx::video::ScaleMode::integer,
  };
  const std::array filters{
    genplusgx::video::VideoFilter::nearest,
    genplusgx::video::VideoFilter::bilinear,
  };
  const std::array shaderConfigurations{
    genplusgx::video::ShaderConfiguration{},
    genplusgx::video::ShaderConfiguration{
      .mode = genplusgx::video::ShaderMode::builtinCrt,
      .presetPath = {},
      .parameters = {},
    },
    genplusgx::video::ShaderConfiguration{
      .mode = genplusgx::video::ShaderMode::libretroPreset,
      .presetPath = std::filesystem::path{GENPLUSGX_SHADER_TEST_FIXTURE_DIR} /
        "libretro-pass.slangp",
      .parameters = {},
    },
  };
  const std::array artworkConfigurations{
    genplusgx::video::ArtworkConfiguration{},
    genplusgx::video::ArtworkConfiguration{
      .mode = genplusgx::video::ArtworkMode::bezel,
      .imagePath = artworkPath,
      .opacityPercent = 80U,
      .constrainVideoToViewport = true,
      .viewportInsets = {
        .leftPercent = 10U,
        .topPercent = 10U,
        .rightPercent = 10U,
        .bottomPercent = 10U,
      },
    },
    genplusgx::video::ArtworkConfiguration{
      .mode = genplusgx::video::ArtworkMode::overlay,
      .imagePath = artworkPath,
      .opacityPercent = 60U,
      .constrainVideoToViewport = false,
      .viewportInsets = {},
    },
  };
  std::size_t presentationCases = 0U;
  for (const auto aspect : aspects) {
    widget.setAspectMode(aspect);
    for (const auto scale : scales) {
      widget.setScaleMode(scale);
      for (const auto filter : filters) {
        widget.setVideoFilter(filter);
        for (const auto& shader : shaderConfigurations) {
          widget.setShaderConfiguration(shader);
          for (const auto& artworkConfiguration : artworkConfigurations) {
            if (!widget.setArtworkConfiguration(artworkConfiguration)) {
              std::cerr << "A valid artwork matrix case was rejected.\n";
              return 10;
            }
            QTest::qWait(20);
            const auto matrixImage = canvas->grabFramebuffer();
            if (matrixImage.isNull() ||
                !quadrantsAreUpright(matrixImage, widget.currentLayout())) {
              std::cerr << "Video presentation case " << presentationCases
                        << " changed frame orientation (aspect "
                        << static_cast<int>(aspect) << ", scale "
                        << static_cast<int>(scale) << ", filter "
                        << static_cast<int>(filter) << ", shader "
                        << static_cast<int>(shader.mode) << ", artwork "
                        << static_cast<int>(artworkConfiguration.mode)
                        << ").\n";
              return 10;
            }
            ++presentationCases;
          }
        }
      }
    }
  }
  if (presentationCases != 108U) {
    std::cerr << "The complete presentation matrix did not execute.\n";
    return 10;
  }

  const genplusgx::video::ShaderConfiguration builtin{
    .mode = genplusgx::video::ShaderMode::builtinCrt,
    .presetPath = {},
    .parameters = {},
  };
  const auto inspection = genplusgx::video::inspectShaderConfiguration(builtin);
  if (!inspection.success || inspection.parameters.size() != 5U) {
    std::cerr << "The built-in CRT parameter inventory was incomplete.\n";
    return 11;
  }
  widget.setAspectMode(genplusgx::video::AspectMode::native);
  widget.setScaleMode(genplusgx::video::ScaleMode::fit);
  widget.setVideoFilter(genplusgx::video::VideoFilter::nearest);
  std::size_t parameterCases = 0U;
  for (const auto& parameter : inspection.parameters) {
    for (const float value : {parameter.minimum, parameter.maximum}) {
      auto configuration = builtin;
      configuration.parameters.push_back({parameter.name, value});
      widget.setShaderConfiguration(std::move(configuration));
      QTest::qWait(20);
      const auto parameterImage = canvas->grabFramebuffer();
      if (parameterImage.isNull() ||
          !quadrantsAreUpright(parameterImage, widget.currentLayout())) {
        std::cerr << "CRT parameter " << parameter.name << " at " << value
                  << " changed frame orientation.\n";
        return 11;
      }
      ++parameterCases;
    }
  }
  if (parameterCases != 10U) {
    std::cerr << "The complete built-in CRT parameter matrix did not execute.\n";
    return 11;
  }
  const std::array synchronizationModes{
    genplusgx::video::PresentationSyncMode::disabled,
    genplusgx::video::PresentationSyncMode::synchronized,
    genplusgx::video::PresentationSyncMode::adaptive,
  };
  const std::array bufferingModes{
    genplusgx::video::PresentationBufferingMode::doubleBuffer,
    genplusgx::video::PresentationBufferingMode::tripleBuffer,
  };
  std::size_t synchronizationCases = 0U;
  for (const auto sync : synchronizationModes) {
    for (const auto buffering : bufferingModes) {
      const genplusgx::video::PresentationConfiguration configuration{
        .sync = sync,
        .buffering = buffering,
      };
      widget.setPresentationConfiguration(configuration);
      QTest::qWait(75);
      canvas = widget.findChild<QOpenGLWidget*>(QStringLiteral("openGLCanvas"));
      const auto metrics = widget.presentationMetrics();
      if (canvas == nullptr || !widget.usesAcceleratedRenderer() ||
          !metrics.rendererInitialized || metrics.requested != configuration ||
          metrics.telemetry.maximumPendingFrames > 1U ||
          (metrics.effectiveBuffering !=
             genplusgx::video::PresentationBufferingMode::doubleBuffer &&
           metrics.effectiveBuffering !=
             genplusgx::video::PresentationBufferingMode::tripleBuffer)) {
        std::cerr << "Presentation synchronization case "
                  << synchronizationCases
                  << " did not rebuild into an observable bounded renderer.\n";
        return 12;
      }
      const auto synchronizedImage = canvas->grabFramebuffer();
      if (synchronizedImage.isNull() ||
          !quadrantsAreUpright(synchronizedImage, widget.currentLayout())) {
        std::cerr << "Presentation synchronization case "
                  << synchronizationCases << " corrupted the displayed frame.\n";
        return 12;
      }
      ++synchronizationCases;
    }
  }
  if (synchronizationCases != 6U) {
    std::cerr << "The complete synchronization/buffering matrix did not execute.\n";
    return 12;
  }
  return 0;
}
