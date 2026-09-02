#pragma once

#include "genplusgx/emulation_capture_sink.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace genplusgx::capture {

inline constexpr std::uint16_t defaultStreamingPort = 55'456U;

enum class StreamingError : std::uint8_t {
  none,
  invalidConfiguration,
  alreadyRunning,
  notRunning,
  listenFailed,
  threadFailure,
  shutdownFailed,
};

struct StreamingStatus final {
  StreamingError error{StreamingError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept { return error == StreamingError::none; }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

struct StreamingConfiguration final {
  std::uint16_t port{defaultStreamingPort};
  std::size_t maximumClients{2U};

  [[nodiscard]] bool valid() const noexcept
  {
    return port != 0U && maximumClients >= 1U && maximumClients <= 4U;
  }
};

struct StreamingMetrics final {
  bool active{false};
  std::uint16_t port{0U};
  std::size_t connectedClients{0U};
  std::size_t queueDepth{0U};
  std::size_t queueCapacity{0U};
  std::size_t peakQueueDepth{0U};
  std::uint64_t acceptedFrames{0U};
  std::uint64_t broadcastFrames{0U};
  std::uint64_t droppedFrames{0U};
  std::uint64_t disconnectedSlowClients{0U};
  std::uint64_t bytesSent{0U};
};

enum class StreamingEventType : std::uint8_t {
  started,
  clientConnected,
  clientDisconnected,
  failed,
  stopped,
};

struct StreamingEvent final {
  StreamingEventType type{StreamingEventType::failed};
  StreamingStatus status;
  StreamingMetrics metrics;
};

class StreamingService final : public EmulationCaptureSink {
public:
  static constexpr std::size_t defaultFrameCapacity = 4U;
  static constexpr std::size_t maximumAudioFramesPerBatch = 4'096U;
  static constexpr std::size_t maximumClientBacklogBytes = 8U * 1024U * 1024U;

  explicit StreamingService(
    std::size_t frameCapacity = defaultFrameCapacity,
    std::size_t eventCapacity = 32U);
  ~StreamingService() override;

  StreamingService(const StreamingService&) = delete;
  StreamingService& operator=(const StreamingService&) = delete;

  [[nodiscard]] StreamingStatus start(StreamingConfiguration configuration);
  [[nodiscard]] StreamingStatus stop();
  [[nodiscard]] std::optional<StreamingEvent> pollEvent();
  [[nodiscard]] std::optional<StreamingEvent> waitForEvent(
    std::chrono::milliseconds timeout);
  [[nodiscard]] StreamingMetrics metrics() const;

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
