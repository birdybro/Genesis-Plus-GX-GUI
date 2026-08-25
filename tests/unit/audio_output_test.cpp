#include "genplusgx/audio_output.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

namespace {

using namespace std::chrono_literals;

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
  genplusgx::AudioOutputConfig config;
  config.sampleRate = 48'000;
  config.latency = 40ms;
  if (!check(genplusgx::audioRingCapacityFrames(config) == 1'920U,
        "Audio latency did not produce the expected bounded frame capacity")) {
    return 1;
  }

  auto invalidConfig = config;
  invalidConfig.sampleRate = 7'999;
  genplusgx::AudioOutput invalid{invalidConfig};
  if (!check(genplusgx::audioRingCapacityFrames(invalidConfig) == 0U,
        "Invalid audio configuration unexpectedly produced a capacity") ||
      !check(invalid.initialize().error ==
          genplusgx::AudioOutputError::invalidConfiguration,
        "Invalid audio configuration was accepted")) {
    return 2;
  }

  genplusgx::AudioOutput output{config};
  if (!check(output.pause().error == genplusgx::AudioOutputError::notInitialized,
        "Uninitialized audio output accepted pause") ||
      !check(output.resume().error == genplusgx::AudioOutputError::notInitialized,
        "Uninitialized audio output accepted resume") ||
      !check(output.initialize(), "SDL dummy audio output failed to initialize") ||
      !check(output.initialize().error ==
          genplusgx::AudioOutputError::alreadyInitialized,
        "Repeated audio initialization was accepted") ||
      !check(output.isInitialized() && output.isPaused(),
        "New host audio stream did not begin initialized and paused") ||
      !check(!output.deviceName().empty(), "Opened audio device has no display name") ||
      !check(output.ringBuffer()->capacityFrames() == 1'920U,
        "Host audio ring capacity differs from configured latency")) {
    return 3;
  }

  std::array<genplusgx::StereoAudioFrame, 1'024> source{};
  for (std::size_t index = 0; index < source.size(); ++index) {
    source[index].left = static_cast<std::int16_t>(index);
    source[index].right = static_cast<std::int16_t>(-static_cast<int>(index));
  }
  const auto written = output.ringBuffer()->write(source);
  if (!check(written.acceptedFrames == source.size() && written.droppedFrames == 0U,
        "Host audio fixture did not enter the output ring") ||
      !check(output.resume(), "SDL dummy audio output failed to resume") ||
      !check(!output.isPaused(), "Resumed audio output still reports paused")) {
    return 4;
  }

  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (std::chrono::steady_clock::now() < deadline &&
         output.metrics().suppliedFrames < source.size()) {
    std::this_thread::sleep_for(5ms);
  }
  const auto runningMetrics = output.metrics();
  if (!check(runningMetrics.callbackCount > 0U,
        "SDL dummy device did not invoke the demand callback") ||
      !check(runningMetrics.suppliedFrames == source.size(),
        "Audio callback did not consume every queued stereo frame") ||
      !check(runningMetrics.requestedFrames ==
          runningMetrics.suppliedFrames + runningMetrics.silenceFrames,
        "Audio callback accounting did not balance") ||
      !check(runningMetrics.submissionFailures == 0U,
        "Audio callback could not submit data to SDL") ||
      !check(output.pause(), "SDL dummy audio output failed to pause") ||
      !check(output.isPaused() && output.ringBuffer()->occupancyFrames() == 0U,
        "Pause did not stop and clear queued audio")) {
    return 5;
  }

  static_cast<void>(output.ringBuffer()->write(source));
  if (!check(output.pause(), "Repeated audio pause failed") ||
      !check(output.ringBuffer()->occupancyFrames() == 0U,
        "Repeated pause retained stale audio") ||
      !check(output.resume(), "Second audio resume failed") ||
      !check(output.pause(), "Second resumed audio stream did not pause") ||
      !check(output.shutdown(), "Audio output shutdown failed") ||
      !check(output.shutdown(), "Repeated audio output shutdown failed") ||
      !check(!output.isInitialized() && output.isPaused(),
        "Audio shutdown retained an active state") ||
      !check(output.resume().error == genplusgx::AudioOutputError::notInitialized,
        "Shutdown audio output accepted resume")) {
    return 6;
  }

  return 0;
}
