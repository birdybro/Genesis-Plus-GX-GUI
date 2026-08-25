#pragma once

#include "genplusgx/audio_frame.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace genplusgx {

struct AudioRingWriteResult final {
  std::size_t acceptedFrames{0};
  std::size_t droppedFrames{0};
};

struct AudioRingReadResult final {
  std::size_t providedFrames{0};
  std::size_t missingFrames{0};
};

struct AudioRingMetrics final {
  std::uint64_t overrunCount{0};
  std::uint64_t underrunCount{0};
  std::uint64_t droppedFrames{0};
  std::uint64_t missingFrames{0};
  std::size_t peakOccupancyFrames{0};
};

class StereoAudioRingBuffer final {
public:
  explicit StereoAudioRingBuffer(std::size_t capacityFrames);

  StereoAudioRingBuffer(const StereoAudioRingBuffer&) = delete;
  StereoAudioRingBuffer& operator=(const StereoAudioRingBuffer&) = delete;

  [[nodiscard]] AudioRingWriteResult write(
    std::span<const StereoAudioFrame> frames) noexcept;
  [[nodiscard]] AudioRingReadResult read(
    std::span<StereoAudioFrame> destination) noexcept;

  [[nodiscard]] std::size_t capacityFrames() const noexcept;
  [[nodiscard]] std::size_t occupancyFrames() const noexcept;
  [[nodiscard]] AudioRingMetrics metrics() const noexcept;

  void clear() noexcept;
  void resetMetrics() noexcept;

private:
  void updatePeak(std::size_t occupancy) noexcept;

  std::vector<StereoAudioFrame> storage_;
  alignas(64) std::atomic<std::uint64_t> readSequence_{0};
  alignas(64) std::atomic<std::uint64_t> writeSequence_{0};
  std::atomic<std::uint64_t> overrunCount_{0};
  std::atomic<std::uint64_t> underrunCount_{0};
  std::atomic<std::uint64_t> droppedFrames_{0};
  std::atomic<std::uint64_t> missingFrames_{0};
  std::atomic<std::size_t> peakOccupancyFrames_{0};
};

} // namespace genplusgx
