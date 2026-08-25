#pragma once

#include "genplusgx/audio_ring_buffer.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace genplusgx {

struct AudioOutputConfig final {
  int sampleRate{48'000};
  std::chrono::milliseconds latency{80};
  std::uint32_t deviceId{0};
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

[[nodiscard]] std::size_t audioRingCapacityFrames(
  const AudioOutputConfig& config) noexcept;

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

  [[nodiscard]] bool isInitialized() const noexcept;
  [[nodiscard]] bool isPaused() const noexcept;
  [[nodiscard]] AudioOutputConfig config() const noexcept;
  [[nodiscard]] std::string deviceName() const;
  [[nodiscard]] AudioOutputMetrics metrics() const noexcept;
  [[nodiscard]] std::shared_ptr<StereoAudioRingBuffer> ringBuffer() const noexcept;

private:
  class Private;
  std::unique_ptr<Private> private_;
};

} // namespace genplusgx
