#include "genplusgx/video/libretro_shader_runtime.h"
#include "genplusgx/video/display_widget.h"

#include <QApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
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
  return 0;
}
