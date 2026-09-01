#pragma once

#include "genplusgx/emulation_capture_sink.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace genplusgx::capture {

enum class RecordingError : std::uint8_t {
  none,
  invalidRequest,
  directoryCreationFailed,
  fileOpenFailed,
  encodeFailed,
  fileWriteFailed,
  fileCommitFailed,
  outputLimitReached,
  alreadyRunning,
  notRunning,
  busy,
  threadFailure,
};

struct RecordingStatus final {
  RecordingError error{RecordingError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept { return error == RecordingError::none; }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

struct RecordingRequest final {
  std::uint64_t operationId{0};
  std::filesystem::path baseDirectory;
  std::string gameTitle;
  std::string gameId;
  std::uint32_t audioSampleRate{48'000U};
  double nominalFramesPerSecond{60.0};
  std::chrono::system_clock::time_point timestamp{
    std::chrono::system_clock::now()};
};

struct RecordingMetrics final {
  bool active{false};
  std::size_t queuedFrames{0U};
  std::size_t queueCapacity{0U};
  std::size_t peakQueuedFrames{0U};
  std::uint64_t acceptedFrames{0U};
  std::uint64_t writtenFrames{0U};
  std::uint64_t droppedFrames{0U};
  std::uint64_t writtenAudioFrames{0U};
  std::uint64_t outputBytes{0U};
};

enum class RecordingEventType : std::uint8_t {
  serviceStarted,
  recordingStarted,
  recordingFinished,
  recordingFailed,
  serviceStopped,
};

struct RecordingEvent final {
  RecordingEventType type{RecordingEventType::recordingFailed};
  std::uint64_t operationId{0U};
  RecordingStatus status;
  std::filesystem::path path;
  RecordingMetrics metrics;

  [[nodiscard]] bool succeeded() const noexcept
  {
    return (type == RecordingEventType::recordingStarted ||
            type == RecordingEventType::recordingFinished) && status.ok();
  }
};

class RecordingService final : public EmulationCaptureSink {
public:
  static constexpr std::size_t defaultFrameCapacity = 8U;
  static constexpr std::size_t maximumAudioFramesPerBatch = 4'096U;
  static constexpr std::uint64_t maximumSessionFrames = 108'000U;
  static constexpr std::uint64_t maximumSessionBytes =
    8ULL * 1024ULL * 1024ULL * 1024ULL;
  static constexpr std::size_t maximumFramePngBytes = 4U * 1024U * 1024U;

  explicit RecordingService(
    std::size_t frameCapacity = defaultFrameCapacity,
    std::size_t eventCapacity = 16U);
  ~RecordingService() override;

  RecordingService(const RecordingService&) = delete;
  RecordingService& operator=(const RecordingService&) = delete;
  RecordingService(RecordingService&&) = delete;
  RecordingService& operator=(RecordingService&&) = delete;

  [[nodiscard]] RecordingStatus start();
  [[nodiscard]] RecordingStatus begin(RecordingRequest request);
  [[nodiscard]] RecordingStatus end(std::uint64_t operationId);
  [[nodiscard]] std::optional<RecordingEvent> pollEvent();
  [[nodiscard]] std::optional<RecordingEvent> waitForEvent(
    std::chrono::milliseconds timeout);
  [[nodiscard]] RecordingStatus stop();
  [[nodiscard]] RecordingMetrics metrics() const;

  [[nodiscard]] bool active() const noexcept override;
  [[nodiscard]] bool submitFrame(
    const CoreVideoFrameInfo& video,
    std::span<const std::uint16_t> rgb565Pixels,
    const CoreAudioBatchInfo& audio,
    std::span<const StereoAudioFrame> audioFrames) noexcept override;

private:
  class Private;
  std::unique_ptr<Private> private_;
};

} // namespace genplusgx::capture
