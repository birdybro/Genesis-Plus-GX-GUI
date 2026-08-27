#include "genplusgx/core_adapter.h"
#include "synthetic_rom.h"

#include <algorithm>
#include <array>
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
  if (!check(adapter.initialize(), "Audio test could not initialize the adapter")) {
    return 1;
  }
  genplusgx::CoreAudioSettings initialSettings;
  if (!check(adapter.audioSettings(initialSettings) &&
        initialSettings == genplusgx::CoreAudioSettings{},
      "Core adapter did not expose deterministic audio defaults")) {
    return 1;
  }
  auto invalidSettings = initialSettings;
  invalidSettings.fmLevelPercent = 201;
  if (!check(adapter.applyAudioSettings(invalidSettings).error ==
        genplusgx::CoreError::invalidSettings &&
        adapter.audioSettings(initialSettings) &&
        initialSettings == genplusgx::CoreAudioSettings{},
      "Invalid audio settings mutated the controlled core snapshot") ||
      !check(adapter.loadGame(fixture.path()), "Audio test could not load the fixture")) {
    return 1;
  }

  genplusgx::CoreAudioBatchInfo noBatch;
  if (!check(adapter.audioBatchInfo(noBatch).error == genplusgx::CoreError::noAudioAvailable,
        "Audio was exposed before an emulated frame") ||
      !check(adapter.runFrame(true), "Audio-producing frame failed")) {
    return 2;
  }

  genplusgx::CoreAudioBatchInfo firstInfo;
  if (!check(adapter.audioBatchInfo(firstInfo), "Generated audio metadata was unavailable") ||
      !check(firstInfo.sampleRate == 48'000U && firstInfo.channels == 2U,
        "Generated audio format metadata was incorrect") ||
      !check(firstInfo.frameCount > 700U && firstInfo.frameCount < 1'100U,
        "Generated NTSC audio frame count was outside its safe range") ||
      !check(firstInfo.emulatedFrameNumber == 1U,
        "Audio batch did not retain its emulated frame number")) {
    return 3;
  }

  std::vector<genplusgx::StereoAudioFrame> tooSmall(firstInfo.frameCount - 1U);
  genplusgx::CoreAudioBatchInfo rejectedInfo;
  if (!check(adapter.copyAudioFrames(tooSmall, rejectedInfo).error ==
          genplusgx::CoreError::audioBufferTooSmall,
        "An undersized audio destination consumed the batch") ||
      !check(adapter.audioBatchInfo(firstInfo),
        "Rejected copy did not preserve the pending audio batch")) {
    return 4;
  }

  std::vector<genplusgx::StereoAudioFrame> firstBatch(firstInfo.frameCount);
  if (!check(adapter.copyAudioFrames(firstBatch, firstInfo),
        "Complete stereo audio copy failed") ||
      !check(adapter.audioBatchInfo(noBatch).error == genplusgx::CoreError::noAudioAvailable,
        "Copied audio remained pending")) {
    return 5;
  }

  const bool containsSignal = std::ranges::any_of(firstBatch, [](const auto& frame) {
    return frame.left != 0 || frame.right != 0;
  });
  const bool centeredStereo = std::ranges::all_of(firstBatch, [](const auto& frame) {
    return frame.left == frame.right;
  });
  if (!check(containsSignal, "Synthetic PSG program generated only silence") ||
      !check(centeredStereo, "Synthetic PSG signal was not centered stereo")) {
    return 6;
  }

  const auto firstHash = genplusgx::hashAudioFrames(firstBatch);
  if (!check(adapter.reset(), "Reset before deterministic audio recapture failed") ||
      !check(adapter.runFrame(true), "Audio recapture frame failed")) {
    return 7;
  }

  genplusgx::CoreAudioBatchInfo secondInfo;
  if (!check(adapter.audioBatchInfo(secondInfo), "Recaptured audio metadata was unavailable")) {
    return 8;
  }
  std::vector<genplusgx::StereoAudioFrame> secondBatch(secondInfo.frameCount);
  if (!check(adapter.copyAudioFrames(secondBatch, secondInfo),
        "Recaptured stereo audio copy failed") ||
      !check(secondBatch == firstBatch, "Hard reset did not reproduce identical audio") ||
      !check(genplusgx::hashAudioFrames(secondBatch) == firstHash,
        "Hard reset did not reproduce the same audio hash")) {
    return 9;
  }

  constexpr std::uint64_t expectedAudioHash = 0x3FDB01D7287AE391ULL;
  if (firstHash != expectedAudioHash) {
    std::cerr << "Generated stereo audio hash was 0x" << std::hex << std::setw(16)
              << std::setfill('0') << firstHash << ", with " << std::dec
              << firstBatch.size() << " frames\n";
    return 10;
  }

  if (!check(adapter.runFrame(true), "First bounded-overwrite frame failed") ||
      !check(adapter.runFrame(true), "Second bounded-overwrite frame failed")) {
    return 11;
  }
  genplusgx::CoreAudioBatchInfo overwrittenInfo;
  if (!check(adapter.audioBatchInfo(overwrittenInfo),
        "Newest bounded audio batch was unavailable") ||
      !check(overwrittenInfo.emulatedFrameNumber == 3U,
        "Bounded audio scratch did not retain the newest frame") ||
      !check(overwrittenInfo.droppedBatchCount == 1U &&
          overwrittenInfo.droppedFrameCount > 700U,
        "Overwritten pending audio was not instrumented")) {
    return 12;
  }

  auto silentSettings = genplusgx::CoreAudioSettings{};
  silentSettings.filter = genplusgx::CoreAudioFilter::disabled;
  silentSettings.psgLevelPercent = 0;
  if (!check(adapter.applyAudioSettings(silentSettings),
        "Live core audio settings update failed") ||
      !check(adapter.reset() && adapter.runFrame(true),
        "Audio settings propagation frame failed")) {
    return 13;
  }
  genplusgx::CoreAudioBatchInfo silentInfo;
  if (!check(adapter.audioBatchInfo(silentInfo),
        "Silent settings audio batch was unavailable")) {
    return 14;
  }
  std::vector<genplusgx::StereoAudioFrame> silentBatch(silentInfo.frameCount);
  genplusgx::CoreAudioSettings appliedSettings;
  if (!check(adapter.copyAudioFrames(silentBatch, silentInfo),
        "Silent settings audio batch copy failed") ||
      !check(genplusgx::hashAudioFrames(silentBatch) != firstHash,
        "Core mixer settings did not change deterministic audio output") ||
      !check(adapter.audioSettings(appliedSettings) &&
        appliedSettings == silentSettings,
        "Core adapter did not retain the applied audio snapshot") ||
      !check(adapter.applyAudioSettings(genplusgx::CoreAudioSettings{}),
        "Core audio defaults could not be restored")) {
    return 15;
  }

  const auto exerciseSetting = [&adapter](
    const genplusgx::CoreAudioSettings& setting) {
    genplusgx::CoreAudioSettings exposed;
    if (!adapter.applyAudioSettings(setting) ||
        !adapter.audioSettings(exposed) || exposed != setting ||
        !adapter.reset() || !adapter.runFrame(true)) {
      return false;
    }
    genplusgx::CoreAudioBatchInfo info;
    if (!adapter.audioBatchInfo(info) || info.frameCount == 0U ||
        info.frameCount > 2'000U) {
      return false;
    }
    std::vector<genplusgx::StereoAudioFrame> samples(info.frameCount);
    return adapter.copyAudioFrames(samples, info).ok();
  };
  std::size_t optionCases = 0U;
  const auto exerciseEnumeration = [&exerciseSetting, &optionCases](
    auto values, auto assign) {
    for (const auto value : values) {
      auto setting = genplusgx::CoreAudioSettings{};
      assign(setting, value);
      if (!exerciseSetting(setting)) {
        return false;
      }
      ++optionCases;
    }
    return true;
  };
  if (!check(exerciseEnumeration(
        std::array{genplusgx::CoreSoundOutput::stereo,
          genplusgx::CoreSoundOutput::mono},
        [](auto& setting, auto value) { setting.output = value; }),
      "A sound-output option failed to produce bounded audio") ||
      !check(exerciseEnumeration(
        std::array{genplusgx::CoreAudioFilter::disabled,
          genplusgx::CoreAudioFilter::lowPass,
          genplusgx::CoreAudioFilter::equalizer},
        [](auto& setting, auto value) { setting.filter = value; }),
      "An audio-filter option failed to produce bounded audio") ||
      !check(exerciseEnumeration(
        std::array{genplusgx::CoreYm2612Core::mameDiscrete,
          genplusgx::CoreYm2612Core::mameIntegrated,
          genplusgx::CoreYm2612Core::mameEnhanced,
          genplusgx::CoreYm2612Core::nukedYm2612,
          genplusgx::CoreYm2612Core::nukedYm3438},
        [](auto& setting, auto value) { setting.ym2612Core = value; }),
      "A YM2612/YM3438 core option failed to produce bounded audio") ||
      !check(exerciseEnumeration(
        std::array{genplusgx::CoreYm2413Mode::disabled,
          genplusgx::CoreYm2413Mode::enabled,
          genplusgx::CoreYm2413Mode::autoDetect},
        [](auto& setting, auto value) { setting.ym2413Mode = value; }),
      "A YM2413 mode failed to produce bounded audio") ||
      !check(exerciseEnumeration(
        std::array{genplusgx::CoreYm2413Core::mame,
          genplusgx::CoreYm2413Core::nuked},
        [](auto& setting, auto value) { setting.ym2413Core = value; }),
      "A YM2413 core failed to produce bounded audio")) {
    return 16;
  }

  const auto exerciseRange = [&exerciseSetting, &optionCases](
    int minimum, int maximum, auto assign) {
    for (const int value : {minimum, maximum}) {
      auto setting = genplusgx::CoreAudioSettings{};
      assign(setting, value);
      if (!exerciseSetting(setting)) {
        return false;
      }
      ++optionCases;
    }
    return true;
  };
  if (!check(exerciseRange(0, 200,
        [](auto& value, int level) { value.psgLevelPercent = level; }) &&
      exerciseRange(0, 200,
        [](auto& value, int level) { value.fmLevelPercent = level; }) &&
      exerciseRange(0, 100,
        [](auto& value, int level) { value.cddaLevelPercent = level; }) &&
      exerciseRange(0, 100,
        [](auto& value, int level) { value.pcmLevelPercent = level; }) &&
      exerciseRange(5, 95,
        [](auto& value, int level) { value.lowPassPercent = level; }) &&
      exerciseRange(0, 200,
        [](auto& value, int level) { value.equalizerLowPercent = level; }) &&
      exerciseRange(0, 200,
        [](auto& value, int level) { value.equalizerMidPercent = level; }) &&
      exerciseRange(0, 200,
        [](auto& value, int level) { value.equalizerHighPercent = level; }),
      "An audio level endpoint failed to produce bounded audio")) {
    return 17;
  }
  for (const bool enabled : {false, true}) {
    auto fm = genplusgx::CoreAudioSettings{};
    fm.highQualityFm = enabled;
    auto psg = genplusgx::CoreAudioSettings{};
    psg.highQualityPsg = enabled;
    if (!check(exerciseSetting(fm) && exerciseSetting(psg),
          "A high-quality audio option failed to produce bounded audio")) {
      return 18;
    }
    optionCases += 2U;
  }
  if (!check(optionCases == 35U,
        "The complete core audio option inventory did not execute") ||
      !check(adapter.applyAudioSettings(genplusgx::CoreAudioSettings{}),
        "Core audio defaults could not be restored after the option matrix")) {
    return 19;
  }

  return adapter.shutdown() ? 0 : 20;
}
