#include "genplusgx/audio_output.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>

#include <array>
#include <algorithm>
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

  std::array<genplusgx::StereoAudioFrame, 3> gainFrames{{
    {.left = 1'000, .right = -1'000},
    {.left = 32'000, .right = -32'000},
    {.left = -1, .right = 1},
  }};
  genplusgx::applyAudioOutputGain(gainFrames, 50, false);
  if (!check(gainFrames[0].left == 500 && gainFrames[0].right == -500 &&
        gainFrames[1].left == 16'000 && gainFrames[1].right == -16'000,
      "Master gain did not scale signed stereo samples deterministically")) {
    return 2;
  }
  genplusgx::applyAudioOutputGain(gainFrames, 100, true);
  if (!check(std::ranges::all_of(gainFrames, [](const auto& frame) {
        return frame.left == 0 && frame.right == 0;
      }), "Mute did not silence every output sample")) {
    return 2;
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
  const auto sharedRing = output.ringBuffer();
  if (!check(output.pollDeviceEvents().processedEvents == 0U,
        "Uninitialized audio output consumed SDL events") ||
      !check(output.pause().error == genplusgx::AudioOutputError::notInitialized,
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
        "Host audio ring capacity differs from configured latency") ||
      !check(output.volumePercent() == 100 && !output.isMuted(),
        "Host audio defaults were not retained") ||
      !check(output.setVolumePercent(55) && output.volumePercent() == 55,
        "Live host volume update failed") ||
      !check(output.setVolumePercent(101).error ==
          genplusgx::AudioOutputError::invalidConfiguration &&
          output.volumePercent() == 55,
        "Invalid live volume changed the host setting")) {
    return 3;
  }
  auto invalidLiveConfig = output.config();
  invalidLiveConfig.latency = 501ms;
  const auto beforeInvalidLiveConfig = output.config();
  if (!check(output.reconfigure(invalidLiveConfig).error ==
        genplusgx::AudioOutputError::invalidConfiguration,
      "Invalid live output configuration was accepted") ||
      !check(output.config().latency == beforeInvalidLiveConfig.latency &&
          output.ringBuffer() == sharedRing &&
          output.ringBuffer()->capacityFrames() == 1'920U,
        "Rejected live output configuration changed active resources")) {
    return 3;
  }
  auto missingDevice = output.config();
  missingDevice.deviceId = 0x7F00'0001U;
  const auto missingDeviceStatus = output.reconfigure(missingDevice);
  if (!check(missingDeviceStatus.error ==
        genplusgx::AudioOutputError::deviceOpenFailed,
      "Unavailable live output device did not fail transactionally") ||
      !check(output.isInitialized() && output.isPaused() &&
          output.config().deviceId == beforeInvalidLiveConfig.deviceId &&
          output.ringBuffer() == sharedRing,
        "Unavailable live device replaced or stopped the active output")) {
    return 3;
  }
  auto enlarged = output.config();
  enlarged.latency = 120ms;
  if (!check(output.reconfigure(enlarged) && output.isPaused(),
        "Paused output could not change latency live") ||
      !check(output.ringBuffer() == sharedRing &&
          output.ringBuffer()->capacityFrames() == 5'760U &&
          output.config().latency == 120ms,
        "Live latency change replaced the shared ring or used the wrong bound") ||
      !check(output.resume(), "Reconfigured output could not resume")) {
    return 3;
  }
  auto reduced = output.config();
  reduced.latency = 30ms;
  if (!check(output.reconfigure(reduced) && !output.isPaused(),
        "Running output did not preserve its state across live latency change") ||
      !check(output.ringBuffer() == sharedRing &&
          output.ringBuffer()->capacityFrames() == 1'440U,
        "Running live latency change broke ring ownership or capacity") ||
      !check(output.pause(), "Live-reconfigured output could not pause")) {
    return 3;
  }
  output.setMuted(true);
  if (!check(output.isMuted(), "Live mute update was not retained")) {
    return 3;
  }
  output.setMuted(false);

  // SDL's event queue can receive hot-plug bursts. The frontend drains only a
  // bounded batch per GUI tick so device churn cannot stall the event loop.
  static_cast<void>(output.pollDeviceEvents());
  for (std::size_t index = 0U; index < 70U; ++index) {
    SDL_Event event{};
    event.type = index == 0U
      ? SDL_EVENT_AUDIO_DEVICE_ADDED
      : SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED;
    event.adevice.which = static_cast<SDL_AudioDeviceID>(index + 1U);
    event.adevice.recording = false;
    if (!check(SDL_PushEvent(&event), "Could not queue an SDL audio-device event")) {
      return 4;
    }
  }
  const auto firstDeviceBatch = output.pollDeviceEvents();
  const auto secondDeviceBatch = output.pollDeviceEvents();
  if (!check(firstDeviceBatch.processedEvents == 64U,
        "Audio device polling did not enforce its per-tick bound") ||
      !check(firstDeviceBatch.playbackDevicesChanged &&
        firstDeviceBatch.formatChanged,
        "Playback-device events were not classified") ||
      !check(secondDeviceBatch.processedEvents == 6U &&
        secondDeviceBatch.formatChanged,
        "Deferred audio-device events were not drained on the next tick")) {
    return 4;
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
  for (int snapshotIndex = 0; snapshotIndex < 10'000; ++snapshotIndex) {
    const auto snapshot = output.metrics();
    if (!check(snapshot.requestedFrames ==
          snapshot.suppliedFrames + snapshot.silenceFrames,
        "Concurrent audio metrics snapshot did not balance")) {
      return 5;
    }
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
  auto reconnect = config;
  reconnect.latency = 65ms;
  reconnect.volumePercent = 42;
  reconnect.muted = true;
  if (!check(output.reconfigure(reconnect) && output.isInitialized() &&
        output.isPaused(),
      "Live configuration could not retry a stopped audio output") ||
      !check(output.ringBuffer() == sharedRing &&
          output.ringBuffer()->capacityFrames() == 3'120U &&
          output.volumePercent() == 42 && output.isMuted(),
        "Stopped-output retry lost ring ownership or requested settings") ||
      !check(output.shutdown(), "Retried audio output did not shut down")) {
    return 6;
  }

  if (!check(SDL_InitSubSystem(SDL_INIT_AUDIO),
        "Could not initialize SDL for selected-device recovery")) {
    return 7;
  }
  const auto devices = genplusgx::availableAudioOutputDevices();
  if (!check(!devices.empty(), "SDL dummy driver exposed no playback device")) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return 7;
  }
  auto selectedConfig = config;
  selectedConfig.deviceId = devices.front().id;
  genplusgx::AudioOutput selected{selectedConfig};
  if (!check(selected.initialize(), "Selected SDL dummy output did not initialize")) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return 7;
  }
  const auto selectedRing = selected.ringBuffer();
  auto defaultConfig = selected.config();
  defaultConfig.deviceId = 0U;
  defaultConfig.latency = 55ms;
  if (!check(selected.reconfigure(defaultConfig) && selected.isPaused() &&
        selected.config().deviceId == 0U,
      "Selected output could not switch to the default device live") ||
      !check(selected.ringBuffer() == selectedRing &&
          selectedRing->capacityFrames() == 2'640U,
        "Live device change replaced the worker-owned audio ring")) {
    return 7;
  }
  selectedConfig = selected.config();
  selectedConfig.deviceId = devices.front().id;
  if (!check(selected.reconfigure(selectedConfig),
        "Default output could not switch back to the selected device live")) {
    return 7;
  }
  static_cast<void>(selected.pollDeviceEvents());
  SDL_Event removed{};
  removed.type = SDL_EVENT_AUDIO_DEVICE_REMOVED;
  removed.adevice.which = static_cast<SDL_AudioDeviceID>(selectedConfig.deviceId);
  removed.adevice.recording = false;
  if (!check(SDL_PushEvent(&removed), "Could not queue selected-device removal") ) {
    return 7;
  }
  const auto recovered = selected.pollDeviceEvents();
  if (!check(recovered.selectedDeviceRemoved && recovered.recoveredToDefault &&
        recovered.recoveryStatus && selected.isInitialized() &&
        selected.config().deviceId == 0U && !selected.deviceName().empty(),
        "Removed selected output did not recover to the default device") ||
      !check(selected.shutdown(), "Recovered audio output did not shut down")) {
    return 7;
  }
  SDL_QuitSubSystem(SDL_INIT_AUDIO);

  return 0;
}
