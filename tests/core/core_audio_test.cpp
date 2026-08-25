#include "genplusgx/core_adapter.h"
#include "synthetic_rom.h"

#include <algorithm>
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
  if (!check(adapter.initialize(), "Audio test could not initialize the adapter") ||
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

  return adapter.shutdown() ? 0 : 13;
}
