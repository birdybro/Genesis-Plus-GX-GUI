#include "genplusgx/core_adapter.h"
#include "synthetic_rom.h"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

} // namespace

int main()
{
  const genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".bin"};
  genplusgx::CoreAdapter adapter;
  if (!check(adapter.initialize(), "Video test could not initialize the adapter") ||
      !check(adapter.loadGame(fixture.path()), "Video test could not load the fixture")) {
    return 1;
  }

  genplusgx::CoreVideoFrameInfo initialInfo;
  if (!check(adapter.videoFrameInfo(initialInfo), "Initial viewport was invalid") ||
      !check(initialInfo.width == 256U && initialInfo.height == 192U,
        "Reset viewport geometry was unexpected") ||
      !check(initialInfo.sourceSurfaceWidth == 720U &&
          initialInfo.sourceSurfaceHeight == 576U &&
          initialInfo.sourcePitchPixels == 720U,
        "Reusable core surface geometry was unexpected")) {
    return 2;
  }

  std::vector<std::uint16_t> tooSmall(initialInfo.pixelCount() - 1U);
  genplusgx::CoreVideoFrameInfo rejectedInfo;
  if (!check(adapter.copyVideoFrame(tooSmall, rejectedInfo).error ==
          genplusgx::CoreError::videoBufferTooSmall,
        "An undersized video destination was accepted")) {
    return 3;
  }

  if (!check(adapter.runFrame(false), "First video frame failed") ||
      !check(adapter.runFrame(false), "Viewport-change frame failed")) {
    return 4;
  }

  genplusgx::CoreVideoFrameInfo frameInfo;
  if (!check(adapter.videoFrameInfo(frameInfo), "Changed viewport was invalid") ||
      !check(frameInfo.width == 320U && frameInfo.height == 224U,
        "Generated VDP mode change did not produce 320x224 output") ||
      !check(frameInfo.frameNumber == 2U, "Video metadata lost the emulated frame number") ||
      !check(frameInfo.viewportChanged, "Dynamic viewport change was not reported") ||
      !check(!frameInfo.interlaced, "Synthetic progressive frame was marked interlaced")) {
    return 5;
  }

  std::vector<std::uint16_t> firstCapture(frameInfo.pixelCount());
  if (!check(adapter.copyVideoFrame(firstCapture, frameInfo),
        "Complete RGB565 frame copy failed")) {
    return 6;
  }
  genplusgx::CoreVideoFrameInfo acknowledgedInfo;
  if (!check(adapter.videoFrameInfo(acknowledgedInfo),
        "Acknowledged viewport became invalid") ||
      !check(!acknowledgedInfo.viewportChanged,
        "Successful frame copy did not acknowledge its viewport change")) {
    return 7;
  }
  const auto firstHash = genplusgx::hashVideoFrame(firstCapture);
  if (firstCapture.front() != 0xEF7DU || firstCapture.back() != 0xEF7DU) {
    std::cerr << "Generated VDP program produced an invalid backdrop (first 0x"
              << std::hex << firstCapture.front() << ", last 0x"
              << firstCapture.back() << ")\n";
    return 8;
  }

  if (!check(adapter.reset(), "Reset before deterministic recapture failed") ||
      !check(adapter.runFrame(false), "Recapture first frame failed") ||
      !check(adapter.runFrame(false), "Recapture viewport frame failed")) {
    return 9;
  }

  genplusgx::CoreVideoFrameInfo secondInfo;
  std::vector<std::uint16_t> secondCapture(firstCapture.size());
  if (!check(adapter.copyVideoFrame(secondCapture, secondInfo),
        "Deterministic frame recapture failed") ||
      !check(secondInfo.width == frameInfo.width && secondInfo.height == frameInfo.height,
        "Reset changed deterministic viewport geometry") ||
      !check(secondCapture == firstCapture, "Reset did not reproduce identical pixels") ||
      !check(genplusgx::hashVideoFrame(secondCapture) == firstHash,
        "Reset did not reproduce the same video hash")) {
    return 10;
  }

  // Genesis 3-bit maximum intensity expands to 0xEF7D in the core's RGB565 path.
  constexpr std::uint64_t expectedFrameHash = 0x0CFD2D0B9AF92325ULL;
  if (firstHash != expectedFrameHash) {
    std::cerr << "Generated RGB565 frame hash was 0x" << std::hex << std::setw(16)
              << std::setfill('0') << firstHash << '\n';
    return 11;
  }

  const genplusgx::CoreVideoSettings enhanced{
    .overscan = genplusgx::CoreOverscanMode::full,
    .ntscFilter = genplusgx::CoreNtscFilter::sVideo,
    .interlacedRender = genplusgx::CoreInterlacedRenderMode::doubleField,
    .gameGearExtendedScreen = true,
  };
  genplusgx::CoreVideoSettings applied;
  if (!check(adapter.applyVideoSettings(enhanced),
        "Core video settings could not be applied") ||
      !check(adapter.videoSettings(applied) && applied == enhanced,
        "Core video setting values were not retained") ||
      !check(adapter.runFrame(false), "Filtered overscan frame failed")) {
    return 12;
  }
  genplusgx::CoreVideoFrameInfo enhancedInfo;
  std::vector<std::uint16_t> enhancedPixels(720U * 576U);
  if (!check(adapter.copyVideoFrame(enhancedPixels, enhancedInfo),
        "Filtered overscan output could not be copied") ||
      !check(enhancedInfo.width > 320U && enhancedInfo.height > 224U,
        "Core NTSC and overscan settings did not change output geometry")) {
    return 13;
  }

  for (const auto filter : {
         genplusgx::CoreNtscFilter::monochrome,
         genplusgx::CoreNtscFilter::composite,
         genplusgx::CoreNtscFilter::sVideo,
         genplusgx::CoreNtscFilter::rgb}) {
    auto preset = enhanced;
    preset.ntscFilter = filter;
    if (!check(adapter.applyVideoSettings(preset),
          "A bundled NTSC preset could not be initialized") ||
        !check(adapter.runFrame(false),
          "A bundled NTSC preset could not render a frame") ||
        !check(adapter.copyVideoFrame(enhancedPixels, enhancedInfo) &&
            enhancedInfo.width > 320U,
          "A bundled NTSC preset did not produce expanded output")) {
      return 14;
    }
  }
  if (!check(adapter.applyVideoSettings(enhanced),
        "Active video settings could not be restored after preset coverage")) {
    return 15;
  }

  auto invalidSettings = enhanced;
  invalidSettings.overscan = static_cast<genplusgx::CoreOverscanMode>(99);
  if (!check(adapter.applyVideoSettings(invalidSettings).error ==
          genplusgx::CoreError::invalidSettings,
        "An unsupported core video setting was accepted") ||
      !check(adapter.videoSettings(applied) && applied == enhanced,
        "A rejected setting changed the active core configuration")) {
    return 16;
  }

  const auto defaults = genplusgx::CoreVideoSettings{};
  if (!check(adapter.applyVideoSettings(defaults),
        "Default core video settings could not be restored") ||
      !check(adapter.runFrame(false), "Default output frame failed")) {
    return 17;
  }
  genplusgx::CoreVideoFrameInfo restoredInfo;
  if (!check(adapter.videoFrameInfo(restoredInfo) &&
        restoredInfo.width == 320U && restoredInfo.height == 224U,
      "Disabling the core video options did not restore native geometry")) {
    return 18;
  }

  return adapter.shutdown() ? 0 : 19;
}
