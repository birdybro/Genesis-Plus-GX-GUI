#pragma once

#include "genplusgx/audio_ring_buffer.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace genplusgx {

struct AudioOutputConfig final {
  int sampleRate{48'000};
  std::chrono::milliseconds latency{80};
  std::uint32_t deviceId{0};
  int volumePercent{100};
  bool muted{false};
};

struct AudioOutputDevice final {
  std::uint32_t id{0};
  std::string name;
};

enum class AudioOutputError {
  none,
  notInitialized,
  alreadyInitialized,
  invalidConfiguration,
  subsystemInitializationFailed,
  deviceOpenFailed,
  deviceControlFailed,
};

struct AudioOutputStatus final {
  AudioOutputError error{AudioOutputError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept { return error == AudioOutputError::none; }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

struct AudioOutputMetrics final {
  std::uint64_t callbackCount{0};
  std::uint64_t requestedFrames{0};
  std::uint64_t suppliedFrames{0};
  std::uint64_t silenceFrames{0};
  std::uint64_t submissionFailures{0};
  AudioRingMetrics ring;
};

struct AudioDeviceEventSummary final {
  std::size_t processedEvents{0};
  bool playbackDevicesChanged{false};
  bool selectedDeviceRemoved{false};
  bool formatChanged{false};
  bool recoveredToDefault{false};
  AudioOutputStatus recoveryStatus;
};

[[nodiscard]] std::size_t audioRingCapacityFrames(
  const AudioOutputConfig& config) noexcept;
void applyAudioOutputGain(
  std::span<StereoAudioFrame> frames,
  int volumePercent,
  bool muted) noexcept;
[[nodiscard]] std::vector<AudioOutputDevice> availableAudioOutputDevices();

class AudioOutput final {
public:
  explicit AudioOutput(AudioOutputConfig config = {});
  ~AudioOutput();

  AudioOutput(const AudioOutput&) = delete;
  AudioOutput& operator=(const AudioOutput&) = delete;
  AudioOutput(AudioOutput&&) = delete;
  AudioOutput& operator=(AudioOutput&&) = delete;

  [[nodiscard]] AudioOutputStatus initialize();
  [[nodiscard]] AudioOutputStatus pause();
  [[nodiscard]] AudioOutputStatus resume();
  [[nodiscard]] AudioOutputStatus shutdown();
  [[nodiscard]] AudioOutputStatus reconfigure(AudioOutputConfig config);
  [[nodiscard]] AudioOutputStatus setVolumePercent(int volumePercent);
  [[nodiscard]] AudioDeviceEventSummary pollDeviceEvents();
  void setMuted(bool muted) noexcept;

  [[nodiscard]] bool isInitialized() const noexcept;
  [[nodiscard]] bool isPaused() const noexcept;
  [[nodiscard]] AudioOutputConfig config() const;
  [[nodiscard]] std::string deviceName() const;
  [[nodiscard]] int volumePercent() const noexcept;
  [[nodiscard]] bool isMuted() const noexcept;
  [[nodiscard]] AudioOutputMetrics metrics() const noexcept;
  [[nodiscard]] std::shared_ptr<StereoAudioRingBuffer> ringBuffer() const noexcept;

private:
  class Private;
  std::unique_ptr<Private> private_;
};

} // namespace genplusgx
