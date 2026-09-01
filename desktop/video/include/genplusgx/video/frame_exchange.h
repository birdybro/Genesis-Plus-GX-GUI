#pragma once

#include "genplusgx/core_adapter.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace genplusgx {

enum class VideoExchangeError {
  none,
  noFrameAvailable,
  noWritableSlot,
  invalidFrame,
  destinationTooSmall,
};

struct VideoExchangeStatus final {
  VideoExchangeError error{VideoExchangeError::none};

  [[nodiscard]] bool ok() const noexcept { return error == VideoExchangeError::none; }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

struct VideoExchangeMetrics final {
  std::uint64_t publishedFrames{0};
  std::uint64_t copiedFrames{0};
  std::uint64_t skippedFrames{0};
  std::uint64_t producerDrops{0};
  std::size_t allocatedPixels{0};
};

class VideoFrameExchange;

class VideoWriteLease final {
public:
  VideoWriteLease() = default;
  ~VideoWriteLease();

  VideoWriteLease(const VideoWriteLease&) = delete;
  VideoWriteLease& operator=(const VideoWriteLease&) = delete;
  VideoWriteLease(VideoWriteLease&& other) noexcept;
  VideoWriteLease& operator=(VideoWriteLease&& other) noexcept;

  [[nodiscard]] std::span<std::uint16_t> pixels() noexcept;
  [[nodiscard]] VideoExchangeStatus publish(const CoreVideoFrameInfo& frame);
  void cancel() noexcept;

private:
  friend class VideoFrameExchange;
  VideoWriteLease(
    VideoFrameExchange& exchange,
    std::size_t slot,
    std::span<std::uint16_t> pixels) noexcept;

  VideoFrameExchange* exchange_{nullptr};
  std::size_t slot_{0};
  std::span<std::uint16_t> pixels_;
};

class VideoFrameExchange final {
public:
  static constexpr std::size_t slotCount = 3U;
  static constexpr std::size_t maximumSurfacePixels = maximumCoreSurfacePixels;

  explicit VideoFrameExchange(
    std::size_t maximumPixels = maximumSurfacePixels);
  ~VideoFrameExchange();

  VideoFrameExchange(const VideoFrameExchange&) = delete;
  VideoFrameExchange& operator=(const VideoFrameExchange&) = delete;
  VideoFrameExchange(VideoFrameExchange&&) = delete;
  VideoFrameExchange& operator=(VideoFrameExchange&&) = delete;

  [[nodiscard]] std::optional<VideoWriteLease> beginWrite();
  [[nodiscard]] VideoExchangeStatus copyLatest(
    std::span<std::uint16_t> destination,
    CoreVideoFrameInfo& frame,
    std::uint64_t& generation);
  [[nodiscard]] VideoExchangeMetrics metrics() const;
  [[nodiscard]] std::size_t maximumPixels() const noexcept;
  void clear() noexcept;

private:
  friend class VideoWriteLease;
  [[nodiscard]] VideoExchangeStatus publish(
    std::size_t slot,
    const CoreVideoFrameInfo& frame);
  void cancel(std::size_t slot) noexcept;

  class Private;
  std::unique_ptr<Private> private_;
};

} // namespace genplusgx
