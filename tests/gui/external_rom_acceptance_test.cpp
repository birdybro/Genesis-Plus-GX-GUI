#include "genplusgx/core_adapter.h"
#include "genplusgx/emulation_worker.h"
#include "genplusgx/game_patch.h"
#include "genplusgx/video/display_widget.h"

#include <QApplication>
#include <QImage>
#include <QOpenGLWidget>
#include <QTest>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

using genplusgx::CoreAudioSettings;
using genplusgx::CoreInputDevice;
using genplusgx::CoreInputSettings;
using genplusgx::CoreSystemSettings;
using genplusgx::CoreVideoFrameInfo;
using genplusgx::CoreVideoSettings;

bool check(bool condition, const std::string& message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::optional<genplusgx::EmulationEvent> waitForOperation(
  genplusgx::EmulationWorker& worker,
  std::uint64_t operationId)
{
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = worker.waitForEvent(100ms);
    if (event && event->operationId == operationId) {
      return event;
    }
  }
  return std::nullopt;
}

bool submitAndSucceed(
  genplusgx::EmulationWorker& worker,
  genplusgx::EmulationCommand command,
  genplusgx::EmulationEvent& event)
{
  const auto operationId = command.operationId;
  if (!worker.submit(std::move(command))) {
    return false;
  }
  auto completed = waitForOperation(worker, operationId);
  if (!completed || !completed->succeeded()) {
    return false;
  }
  event = std::move(*completed);
  return true;
}

CoreInputSettings oneDevice(CoreInputDevice device)
{
  CoreInputSettings settings;
  settings.devices.fill(CoreInputDevice::none);
  if (device != CoreInputDevice::none) {
    settings.devices[0] = device;
  }
  return settings;
}

bool copyCurrentFrame(
  genplusgx::CoreAdapter& adapter,
  std::vector<std::uint16_t>& pixels,
  CoreVideoFrameInfo& frame)
{
  if (!adapter.videoFrameInfo(frame) || frame.pixelCount() == 0U ||
      frame.pixelCount() > genplusgx::VideoFrameExchange::maximumSurfacePixels) {
    return false;
  }
  pixels.resize(frame.pixelCount());
  return adapter.copyVideoFrame(pixels, frame).ok() &&
    std::ranges::any_of(pixels, [](std::uint16_t pixel) { return pixel != 0U; });
}

bool workerFrameIsNonBlack(genplusgx::EmulationWorker& worker)
{
  std::vector<std::uint16_t> pixels(
    genplusgx::VideoFrameExchange::maximumSurfacePixels);
  CoreVideoFrameInfo frame;
  std::uint64_t generation = 0U;
  return worker.videoFrames()->copyLatest(pixels, frame, generation).ok() &&
    generation > 0U && frame.pixelCount() > 0U &&
    frame.pixelCount() <= pixels.size() &&
    std::ranges::any_of(
      pixels.begin(), pixels.begin() +
        static_cast<std::ptrdiff_t>(frame.pixelCount()),
      [](std::uint16_t pixel) { return pixel != 0U; });
}

bool publishFrame(
  const std::shared_ptr<genplusgx::VideoFrameExchange>& exchange,
  genplusgx::video::DisplayWidget& widget,
  const std::vector<std::uint16_t>& pixels,
  const CoreVideoFrameInfo& frame)
{
  auto lease = exchange->beginWrite();
  if (!lease || lease->pixels().size() < pixels.size()) {
    return false;
  }
  std::ranges::copy(pixels, lease->pixels().begin());
  return lease->publish(frame).ok() && widget.presentLatestFrame();
}

double imageDifference(
  const QImage& reference,
  const QImage& candidate,
  const genplusgx::video::VideoLayout& layout,
  bool verticallyFlipReference)
{
  if (reference.size() != candidate.size() || !layout.valid()) {
    return std::numeric_limits<double>::infinity();
  }
  const int left = layout.x + layout.width / 5;
  const int right = layout.x + (layout.width * 4) / 5;
  const int top = layout.y + layout.height / 5;
  const int bottom = layout.y + (layout.height * 4) / 5;
  double difference = 0.0;
  std::size_t samples = 0U;
  for (int y = top; y < bottom; y += 2) {
    const int referenceY = verticallyFlipReference
      ? layout.y + layout.height - 1 - (y - layout.y) : y;
    for (int x = left; x < right; x += 2) {
      const auto first = reference.pixelColor(x, referenceY);
      const auto second = candidate.pixelColor(x, y);
      difference += std::abs(first.redF() - second.redF()) +
        std::abs(first.greenF() - second.greenF()) +
        std::abs(first.blueF() - second.blueF());
      ++samples;
    }
  }
  return samples == 0U ? std::numeric_limits<double>::infinity()
                       : difference / static_cast<double>(samples);
}

bool isNonBlack(const QImage& image)
{
  for (int y = 0; y < image.height(); y += 4) {
    for (int x = 0; x < image.width(); x += 4) {
      if (image.pixelColor(x, y).value() > 8) {
        return true;
      }
    }
  }
  return false;
}

QString outputPath(
  const std::filesystem::path& directory,
  const std::string& filename)
{
  return QString::fromStdString((directory / filename).string());
}

bool exerciseAudioCase(
  genplusgx::CoreAdapter& adapter,
  std::span<const std::uint8_t> state,
  const CoreAudioSettings& settings)
{
  CoreAudioSettings retained;
  // The upstream raw-state payload contains the active sound-core context,
  // whose size differs between MAME and Nuked implementations. Restore the
  // configuration under which this temporary baseline state was captured
  // before loading it, then exercise the live switch being tested.
  if (!adapter.applyAudioSettings(CoreAudioSettings{}) ||
      !adapter.loadRawState(state)) {
    std::cerr << "  audio diagnostic: state reload failed\n";
    return false;
  }
  if (!adapter.applyAudioSettings(settings)) {
    std::cerr << "  audio diagnostic: live setting application failed\n";
    return false;
  }
  if (!adapter.audioSettings(retained) || retained != settings) {
    std::cerr << "  audio diagnostic: setting was not retained\n";
    return false;
  }
  if (!adapter.runFrame(true)) {
    std::cerr << "  audio diagnostic: emulated frame failed\n";
    return false;
  }
  genplusgx::CoreAudioBatchInfo info;
  if (!adapter.audioBatchInfo(info) || info.frameCount == 0U ||
      info.frameCount > 2'000U) {
    std::cerr << "  audio diagnostic: invalid/missing batch ("
              << info.frameCount << " frames)\n";
    return false;
  }
  std::vector<genplusgx::StereoAudioFrame> samples(info.frameCount);
  if (!adapter.copyAudioFrames(samples, info)) {
    std::cerr << "  audio diagnostic: batch copy failed\n";
    return false;
  }
  return true;
}

std::vector<CoreAudioSettings> audioCases()
{
  std::vector<CoreAudioSettings> result;
  const auto add = [&result](auto values, auto assign) {
    for (const auto value : values) {
      CoreAudioSettings settings;
      assign(settings, value);
      result.push_back(settings);
    }
  };
  add(std::array{genplusgx::CoreSoundOutput::stereo,
        genplusgx::CoreSoundOutput::mono},
    [](auto& settings, auto value) { settings.output = value; });
  add(std::array{genplusgx::CoreAudioFilter::disabled,
        genplusgx::CoreAudioFilter::lowPass,
        genplusgx::CoreAudioFilter::equalizer},
    [](auto& settings, auto value) { settings.filter = value; });
  add(std::array{genplusgx::CoreYm2612Core::mameDiscrete,
        genplusgx::CoreYm2612Core::mameIntegrated,
        genplusgx::CoreYm2612Core::mameEnhanced,
        genplusgx::CoreYm2612Core::nukedYm2612,
        genplusgx::CoreYm2612Core::nukedYm3438},
    [](auto& settings, auto value) { settings.ym2612Core = value; });
  add(std::array{genplusgx::CoreYm2413Mode::disabled,
        genplusgx::CoreYm2413Mode::enabled,
        genplusgx::CoreYm2413Mode::autoDetect},
    [](auto& settings, auto value) { settings.ym2413Mode = value; });
  add(std::array{genplusgx::CoreYm2413Core::mame,
        genplusgx::CoreYm2413Core::nuked},
    [](auto& settings, auto value) { settings.ym2413Core = value; });
  const auto addRange = [&result](int minimum, int maximum, auto assign) {
    for (const int value : {minimum, maximum}) {
      CoreAudioSettings settings;
      assign(settings, value);
      result.push_back(settings);
    }
  };
  addRange(0, 200, [](auto& value, int level) { value.psgLevelPercent = level; });
  addRange(0, 200, [](auto& value, int level) { value.fmLevelPercent = level; });
  addRange(0, 100, [](auto& value, int level) { value.cddaLevelPercent = level; });
  addRange(0, 100, [](auto& value, int level) { value.pcmLevelPercent = level; });
  addRange(5, 95, [](auto& value, int level) { value.lowPassPercent = level; });
  addRange(0, 200,
    [](auto& value, int level) { value.equalizerLowPercent = level; });
  addRange(0, 200,
    [](auto& value, int level) { value.equalizerMidPercent = level; });
  addRange(0, 200,
    [](auto& value, int level) { value.equalizerHighPercent = level; });
  for (const bool enabled : {false, true}) {
    CoreAudioSettings fm;
    fm.highQualityFm = enabled;
    result.push_back(fm);
    CoreAudioSettings psg;
    psg.highQualityPsg = enabled;
    result.push_back(psg);
  }
  return result;
}

std::vector<std::pair<std::string, CoreVideoSettings>> videoCases()
{
  std::vector<std::pair<std::string, CoreVideoSettings>> result;
  for (const auto value : {genplusgx::CoreOverscanMode::disabled,
         genplusgx::CoreOverscanMode::vertical,
         genplusgx::CoreOverscanMode::horizontal,
         genplusgx::CoreOverscanMode::full}) {
    CoreVideoSettings settings;
    settings.overscan = value;
    result.emplace_back("overscan-" + std::to_string(static_cast<int>(value)), settings);
  }
  for (const auto value : {genplusgx::CoreNtscFilter::disabled,
         genplusgx::CoreNtscFilter::monochrome,
         genplusgx::CoreNtscFilter::composite,
         genplusgx::CoreNtscFilter::sVideo,
         genplusgx::CoreNtscFilter::rgb}) {
    CoreVideoSettings settings;
    settings.ntscFilter = value;
    result.emplace_back("ntsc-" + std::to_string(static_cast<int>(value)), settings);
  }
  for (const auto value : {genplusgx::CoreInterlacedRenderMode::singleField,
         genplusgx::CoreInterlacedRenderMode::doubleField}) {
    CoreVideoSettings settings;
    settings.interlacedRender = value;
    result.emplace_back("interlace-" + std::to_string(static_cast<int>(value)), settings);
  }
  for (const bool enabled : {false, true}) {
    CoreVideoSettings settings;
    settings.gameGearExtendedScreen = enabled;
    result.emplace_back(enabled ? "gamegear-extended" : "gamegear-native", settings);
  }
  return result;
}

} // namespace

int main(int argc, char** argv)
{
  genplusgx::video::configureOpenGLSurfaceFormat();
  QApplication application(argc, argv);
  if (argc != 3) {
    std::cerr << "Usage: genplusgx_external_rom_acceptance_test ROM OUTPUT_DIRECTORY\n";
    return 2;
  }
  const std::filesystem::path rom{argv[1]};
  const std::filesystem::path outputDirectory{argv[2]};
  std::error_code filesystemError;
  if (!check(std::filesystem::is_regular_file(rom, filesystemError),
        "The external ROM does not exist") ||
      !check(std::filesystem::create_directories(outputDirectory, filesystemError) ||
          std::filesystem::is_directory(outputDirectory, filesystemError),
        "The comparison output directory could not be created")) {
    return 2;
  }

  genplusgx::CoreAdapter adapter;
  if (!check(adapter.initialize(), "The core adapter could not initialize") ||
      !check(adapter.loadGame(rom), "The external game could not load")) {
    return 3;
  }
  for (std::size_t frame = 0U; frame < 600U; ++frame) {
    if (!check(adapter.runFrame(false), "The real-ROM warm-up frame failed")) {
      return 4;
    }
  }
  std::vector<std::uint8_t> stableState;
  if (!check(adapter.saveRawState(stableState),
        "The real-ROM comparison state could not be captured")) {
    return 4;
  }

  std::vector<std::uint16_t> referencePixels;
  CoreVideoFrameInfo referenceFrame;
  if (!check(copyCurrentFrame(adapter, referencePixels, referenceFrame),
        "The real-ROM reference frame was empty")) {
    return 5;
  }

  for (const auto& [name, settings] : videoCases()) {
    if (!check(adapter.loadRawState(stableState) &&
          adapter.applyVideoSettings(settings) && adapter.runFrame(false),
        "Core video option failed for the real ROM: " + name)) {
      return 6;
    }
    std::vector<std::uint16_t> pixels;
    CoreVideoFrameInfo frame;
    if (!check(copyCurrentFrame(adapter, pixels, frame),
          "Core video option produced an empty real-ROM frame: " + name)) {
      return 6;
    }
    const QImage image{
      reinterpret_cast<const uchar*>(pixels.data()),
      static_cast<int>(frame.width), static_cast<int>(frame.height),
      static_cast<int>(frame.width * sizeof(std::uint16_t)), QImage::Format_RGB16};
    if (!check(image.copy().save(outputPath(outputDirectory, "core-" + name + ".png")),
          "Core video comparison PNG could not be written: " + name)) {
      return 6;
    }
  }

  const auto actualAudioCases = audioCases();
  if (!check(actualAudioCases.size() == 35U,
        "The complete audio option inventory was not constructed")) {
    return 7;
  }
  for (std::size_t index = 0U; index < actualAudioCases.size(); ++index) {
    if (!check(exerciseAudioCase(adapter, stableState, actualAudioCases[index]),
          "Audio option failed on the real ROM at case " + std::to_string(index))) {
      return 7;
    }
  }

  constexpr std::array devices{
    CoreInputDevice::none,
    CoreInputDevice::pad3Button,
    CoreInputDevice::pad6Button,
    CoreInputDevice::segaMouse,
    CoreInputDevice::lightGun,
    CoreInputDevice::paddle,
    CoreInputDevice::sportsPad,
    CoreInputDevice::xe1Ap,
    CoreInputDevice::pico,
    CoreInputDevice::terebiOekaki,
    CoreInputDevice::graphicBoard,
    CoreInputDevice::activator,
  };
  std::uint64_t sequence = 1U;
  for (const auto device : devices) {
    genplusgx::InputSnapshot snapshot;
    snapshot.sequence = sequence++;
    snapshot.players[0].connected = device != CoreInputDevice::none;
    snapshot.players[0].buttons = 0x0FFFU;
    snapshot.players[0].analogX = 12'345;
    snapshot.players[0].analogY = -12'345;
    CoreInputSettings retained;
    const auto settings = oneDevice(device);
    if (!check(adapter.applyInputSettings(settings) &&
          adapter.inputSettings(retained) && retained == settings &&
          adapter.setInputSnapshot(snapshot) && adapter.runFrame(true) &&
          adapter.appliedInputSequence() == snapshot.sequence,
        "An input-device option failed on the real ROM")) {
      return 8;
    }
  }

  if (!check(adapter.loadRawState(stableState) &&
        adapter.applyVideoSettings(CoreVideoSettings{}) && adapter.runFrame(false) &&
        copyCurrentFrame(adapter, referencePixels, referenceFrame),
      "The default real-ROM frame could not be restored")) {
    return 9;
  }
  auto exchange = std::make_shared<genplusgx::VideoFrameExchange>();
  genplusgx::video::DisplayWidget widget;
  std::string shaderFailure;
  widget.setShaderFailureSink([&shaderFailure](std::string message) {
    shaderFailure = std::move(message);
  });
  widget.setFrameExchange(exchange);
  widget.resize(640, 480);
  widget.show();
  if (!QTest::qWaitForWindowExposed(&widget) || !widget.usesAcceleratedRenderer() ||
      !publishFrame(exchange, widget, referencePixels, referenceFrame)) {
    std::cerr << "The accelerated real-ROM comparison display was unavailable.\n";
    return 9;
  }
  auto* canvas = widget.findChild<QOpenGLWidget*>(QStringLiteral("openGLCanvas"));
  if (canvas == nullptr) {
    std::cerr << "The real-ROM OpenGL canvas was unavailable.\n";
    return 9;
  }

  constexpr std::array aspects{
    genplusgx::video::AspectMode::native,
    genplusgx::video::AspectMode::fourThree,
    genplusgx::video::AspectMode::stretch,
  };
  constexpr std::array scales{
    genplusgx::video::ScaleMode::fit,
    genplusgx::video::ScaleMode::integer,
  };
  constexpr std::array filters{
    genplusgx::video::VideoFilter::nearest,
    genplusgx::video::VideoFilter::bilinear,
  };
  const std::array shaders{
    genplusgx::video::ShaderConfiguration{},
    genplusgx::video::ShaderConfiguration{
      .mode = genplusgx::video::ShaderMode::builtinCrt,
      .presetPath = {}, .parameters = {}},
    genplusgx::video::ShaderConfiguration{
      .mode = genplusgx::video::ShaderMode::libretroPreset,
      .presetPath = std::filesystem::path{GENPLUSGX_SHADER_TEST_FIXTURE_DIR} /
        "libretro-pass.slangp",
      .parameters = {}},
  };
  std::size_t presentationCases = 0U;
  for (const auto aspect : aspects) {
    widget.setAspectMode(aspect);
    for (const auto scale : scales) {
      widget.setScaleMode(scale);
      for (const auto filter : filters) {
        widget.setVideoFilter(filter);
        QImage baseline;
        for (const auto& shader : shaders) {
          shaderFailure.clear();
          widget.setShaderConfiguration(shader);
          QTest::qWait(25);
          const auto image = canvas->grabFramebuffer();
          const auto name = "presentation-" + std::to_string(presentationCases) +
            "-a" + std::to_string(static_cast<int>(aspect)) +
            "-s" + std::to_string(static_cast<int>(scale)) +
            "-f" + std::to_string(static_cast<int>(filter)) +
            "-h" + std::to_string(static_cast<int>(shader.mode)) + ".png";
          if (!check(shaderFailure.empty(), "A real-ROM shader failed: " + shaderFailure) ||
              !check(!image.isNull() && isNonBlack(image),
                "A presentation option produced a black real-ROM image") ||
              !check(image.save(outputPath(outputDirectory, name)),
                "A presentation comparison PNG could not be written")) {
            return 10;
          }
          if (shader.mode == genplusgx::video::ShaderMode::disabled) {
            baseline = image;
          } else {
            const auto upright = imageDifference(
              baseline, image, widget.currentLayout(), false);
            const auto inverted = imageDifference(
              baseline, image, widget.currentLayout(), true);
            if (!check(upright < inverted,
                  "A shader presentation is closer to vertically inverted output")) {
              return 10;
            }
          }
          ++presentationCases;
        }
      }
    }
  }
  if (!check(presentationCases == 36U,
        "The complete real-ROM presentation matrix did not execute")) {
    return 10;
  }

  if (!check(adapter.unloadGame(), "The real-ROM session could not unload")) {
    return 11;
  }

  QTemporaryDir patchDirectory;
  const auto patchRoot = std::filesystem::path{
    patchDirectory.path().toStdString()};
  const auto patchPath = patchRoot / "external-rom.ips";
  std::uint8_t originalHeaderByte{};
  {
    std::ifstream source(rom, std::ios::binary);
    source.seekg(0x120, std::ios::beg);
    source.read(reinterpret_cast<char*>(&originalHeaderByte), 1);
    if (!check(static_cast<bool>(source),
          "The real ROM is too small for the safe header-only patch case")) {
      return 11;
    }
  }
  const std::vector<std::uint8_t> patch{
    'P', 'A', 'T', 'C', 'H',
    0x00, 0x01, 0x20, 0x00, 0x01,
    static_cast<std::uint8_t>(originalHeaderByte == 0x58U ? 0x59U : 0x58U),
    'E', 'O', 'F',
  };
  {
    std::ofstream output(patchPath, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(patch.data()),
      static_cast<std::streamsize>(patch.size()));
    if (!check(static_cast<bool>(output),
          "The temporary real-ROM IPS patch could not be written")) {
      return 11;
    }
  }
  const auto patched = genplusgx::applyGamePatchFile(
    rom, patchPath, patchRoot / "cache");
  if (!check(patched.status && patched.path != rom,
        "The real ROM could not be soft-patched non-destructively") ||
      !check(adapter.loadGame(patched.path),
        "The patched real ROM did not load through the core")) {
    return 11;
  }
  for (std::size_t frame = 0U; frame < 120U; ++frame) {
    if (!check(adapter.runFrame(false),
          "The patched real-ROM frame failed")) {
      return 11;
    }
  }
  std::vector<std::uint16_t> patchedPixels;
  CoreVideoFrameInfo patchedFrame;
  if (!check(copyCurrentFrame(adapter, patchedPixels, patchedFrame),
        "The patched real ROM produced a black frame") ||
      !check(adapter.unloadGame(),
        "The patched real-ROM session did not unload")) {
    return 11;
  }
  std::vector<CoreSystemSettings> systemCases;
  const auto addSystemCases = [&systemCases](auto values, auto assign) {
    for (const auto value : values) {
      CoreSystemSettings settings;
      assign(settings, value);
      systemCases.push_back(settings);
    }
  };
  addSystemCases(std::array{genplusgx::CoreSystemHardware::automatic,
      genplusgx::CoreSystemHardware::genesis},
    [](auto& settings, auto value) { settings.hardware = value; });
  addSystemCases(std::array{genplusgx::CoreSystemRegion::automatic,
      genplusgx::CoreSystemRegion::ntscU,
      genplusgx::CoreSystemRegion::palEurope,
      genplusgx::CoreSystemRegion::ntscJapan,
      genplusgx::CoreSystemRegion::palJapan},
    [](auto& settings, auto value) { settings.region = value; });
  addSystemCases(std::array{genplusgx::CoreVideoStandard::automatic,
      genplusgx::CoreVideoStandard::ntsc,
      genplusgx::CoreVideoStandard::pal},
    [](auto& settings, auto value) { settings.videoStandard = value; });
  addSystemCases(std::array{genplusgx::CoreMasterClock::automatic,
      genplusgx::CoreMasterClock::ntsc,
      genplusgx::CoreMasterClock::pal},
    [](auto& settings, auto value) { settings.masterClock = value; });
  for (const bool enabled : {false, true}) {
    CoreSystemSettings lockups;
    lockups.emulateIllegalAccessLockups = enabled;
    systemCases.push_back(lockups);
    CoreSystemSettings errors;
    errors.enableAddressErrors = enabled;
    systemCases.push_back(errors);
  }
  if (!check(systemCases.size() == 17U,
        "The real-ROM system option inventory was incomplete")) {
    return 11;
  }
  for (const auto& settings : systemCases) {
    CoreSystemSettings retained;
    genplusgx::CoreTimingInfo timing;
    if (!check(adapter.applySystemSettings(settings) &&
          adapter.systemSettings(retained) && retained == settings &&
          adapter.loadGame(rom) && adapter.runFrame(false) &&
          adapter.timingInfo(timing) && timing.framesPerSecond() > 40.0 &&
          timing.framesPerSecond() < 70.0 && adapter.unloadGame(),
        "A system option failed the real-ROM reload path")) {
      return 11;
    }
  }
  if (!check(adapter.applySystemSettings(CoreSystemSettings{}) && adapter.shutdown(),
        "The real-ROM acceptance session did not shut down cleanly")) {
    return 12;
  }
  genplusgx::EmulationWorker worker;
  genplusgx::EmulationEvent event;
  if (!check(worker.start() && worker.waitForEvent(2s).has_value(),
        "The real-ROM speed worker could not start") ||
      !check(submitAndSucceed(
          worker, genplusgx::EmulationCommand::load(1U, rom), event),
        "The real ROM could not load through the speed worker") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::updateSpeedSettings(2U, {
            .normalPercent = 100U,
            .slowMotionPercent = 25U,
            .fastForwardPercent = 800U,
          }), event),
        "The real-ROM speed settings were rejected") ||
      !check(event.speedPercent == 100U,
        "The real-ROM worker did not retain normal speed") ||
      !check(submitAndSucceed(
          worker, genplusgx::EmulationCommand::slowMotion(3U, true), event),
        "The real-ROM slow-motion mode failed") ||
      !check(event.slowMotion && !event.fastForward &&
          event.speedPercent == 25U,
        "The real-ROM slow-motion state was incorrect") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::frameAdvance, 4U), event),
        "The real-ROM slow-motion frame failed") ||
      !check(workerFrameIsNonBlack(worker),
        "The real-ROM slow-motion frame was black") ||
      !check(worker.audioFrames()->occupancyFrames() == 0U,
        "The real-ROM slow-motion frame queued host audio") ||
      !check(submitAndSucceed(
          worker, genplusgx::EmulationCommand::fastForward(5U, true), event),
        "The real-ROM fast-forward mode failed") ||
      !check(event.fastForward && !event.slowMotion &&
          event.speedPercent == 800U,
        "The real-ROM fast-forward state was incorrect") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::frameAdvance, 6U), event),
        "The real-ROM fast-forward frame failed") ||
      !check(workerFrameIsNonBlack(worker),
        "The real-ROM fast-forward frame was black") ||
      !check(worker.audioFrames()->occupancyFrames() == 0U,
        "The real-ROM fast-forward frame queued host audio") ||
      !check(submitAndSucceed(
          worker, genplusgx::EmulationCommand::fastForward(7U, false), event),
        "The real-ROM normal-speed restore failed") ||
      !check(event.speedPercent == 100U && !event.fastForward &&
          !event.slowMotion,
        "The real-ROM worker did not restore normal speed") ||
      !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::frameAdvance, 8U), event),
        "The real-ROM normal-speed frame failed") ||
      !check(workerFrameIsNonBlack(worker),
        "The real-ROM normal-speed frame was black") ||
      !check(worker.audioFrames()->occupancyFrames() > 0U,
        "The real-ROM normal-speed frame did not queue host audio")) {
    return 13;
  }

  for (std::uint32_t depth = genplusgx::minimumRunAheadFrames;
       depth <= genplusgx::maximumRunAheadFrames; ++depth) {
    worker.audioFrames()->clear();
    if (!check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::updateRunAheadSettings(
            10U + (depth * 2U), {.enabled = true, .frames = depth}), event),
          "A real-ROM run-ahead depth was rejected") ||
        !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::frameAdvance,
            11U + (depth * 2U)), event),
          "A real-ROM run-ahead frame failed") ||
        !check(event.runAheadActive && event.runAheadVerified &&
            event.runAheadFrames == depth,
          "A real-ROM run-ahead depth failed deterministic activation") ||
        !check(workerFrameIsNonBlack(worker),
          "A real-ROM run-ahead frame was black") ||
        !check(worker.audioFrames()->occupancyFrames() > 0U,
          "A real-ROM run-ahead frame lost authoritative host audio") ||
        !check(worker.metrics().runAheadDeterminismFailures == 0U &&
            worker.metrics().runAheadStateCapacityBytes <= 4U * 1024U * 1024U,
          "Real-ROM run-ahead was non-deterministic or unbounded")) {
      return 13;
    }
  }
  if (!check(submitAndSucceed(worker,
        genplusgx::EmulationCommand::simple(
          genplusgx::EmulationCommandType::unloadGame, 30U), event),
      "The real-ROM speed/run-ahead worker could not unload") ||
      !check(worker.stop(), "The real-ROM speed/run-ahead worker could not stop")) {
    return 13;
  }
  std::cout << "PASS: 13 core video cases, 35 core audio cases, 12 input "
               "devices, 36 accelerated presentation cases, 17 system "
               "reload cases, 3 emulation speed modes, 4 run-ahead depths, "
               "and one non-destructive "
               "IPS launch. Comparison PNGs: "
            << outputDirectory << '\n';
  return 0;
}
