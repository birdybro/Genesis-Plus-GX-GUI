#include "genplusgx/audio_ring_buffer.h"

#include <algorithm>
#include <stdexcept>

namespace genplusgx {

StereoAudioRingBuffer::StereoAudioRingBuffer(std::size_t capacityFrames)
  : storage_(capacityFrames)
{
  if (capacityFrames == 0U) {
    throw std::invalid_argument{"Audio ring capacity must be greater than zero."};
  }
}

AudioRingWriteResult StereoAudioRingBuffer::write(
  std::span<const StereoAudioFrame> frames) noexcept
{
  const auto writeSequence = writeSequence_.load(std::memory_order_relaxed);
  const auto readSequence = readSequence_.load(std::memory_order_acquire);
  const auto occupied = static_cast<std::size_t>(writeSequence - readSequence);
  const auto available = storage_.size() - std::min(occupied, storage_.size());
  const auto accepted = std::min(frames.size(), available);

  for (std::size_t index = 0; index < accepted; ++index) {
    storage_[static_cast<std::size_t>((writeSequence + index) % storage_.size())] =
      frames[index];
  }
  writeSequence_.store(writeSequence + accepted, std::memory_order_release);
  updatePeak(occupied + accepted);

  const auto dropped = frames.size() - accepted;
  if (dropped > 0U) {
    overrunCount_.fetch_add(1U, std::memory_order_relaxed);
    droppedFrames_.fetch_add(dropped, std::memory_order_relaxed);
  }
  return {.acceptedFrames = accepted, .droppedFrames = dropped};
}

AudioRingReadResult StereoAudioRingBuffer::read(
  std::span<StereoAudioFrame> destination) noexcept
{
  const auto readSequence = readSequence_.load(std::memory_order_relaxed);
  const auto writeSequence = writeSequence_.load(std::memory_order_acquire);
  const auto available = static_cast<std::size_t>(writeSequence - readSequence);
  const auto provided = std::min(destination.size(), available);

  for (std::size_t index = 0; index < provided; ++index) {
    destination[index] =
      storage_[static_cast<std::size_t>((readSequence + index) % storage_.size())];
  }
  readSequence_.store(readSequence + provided, std::memory_order_release);

  const auto missing = destination.size() - provided;
  if (missing > 0U) {
    underrunCount_.fetch_add(1U, std::memory_order_relaxed);
    missingFrames_.fetch_add(missing, std::memory_order_relaxed);
  }
  return {.providedFrames = provided, .missingFrames = missing};
}

std::size_t StereoAudioRingBuffer::capacityFrames() const noexcept
{
  return storage_.size();
}

std::size_t StereoAudioRingBuffer::occupancyFrames() const noexcept
{
  const auto writeSequence = writeSequence_.load(std::memory_order_acquire);
  const auto readSequence = readSequence_.load(std::memory_order_acquire);
  return std::min(
    static_cast<std::size_t>(writeSequence - readSequence), storage_.size());
}

AudioRingMetrics StereoAudioRingBuffer::metrics() const noexcept
{
  return {
    .overrunCount = overrunCount_.load(std::memory_order_relaxed),
    .underrunCount = underrunCount_.load(std::memory_order_relaxed),
    .droppedFrames = droppedFrames_.load(std::memory_order_relaxed),
    .missingFrames = missingFrames_.load(std::memory_order_relaxed),
    .peakOccupancyFrames = peakOccupancyFrames_.load(std::memory_order_relaxed),
  };
}

void StereoAudioRingBuffer::clear() noexcept
{
  const auto writeSequence = writeSequence_.load(std::memory_order_acquire);
  readSequence_.store(writeSequence, std::memory_order_release);
}

void StereoAudioRingBuffer::resetMetrics() noexcept
{
  overrunCount_.store(0, std::memory_order_relaxed);
  underrunCount_.store(0, std::memory_order_relaxed);
  droppedFrames_.store(0, std::memory_order_relaxed);
  missingFrames_.store(0, std::memory_order_relaxed);
  peakOccupancyFrames_.store(occupancyFrames(), std::memory_order_relaxed);
}

void StereoAudioRingBuffer::updatePeak(std::size_t occupancy) noexcept
{
  auto peak = peakOccupancyFrames_.load(std::memory_order_relaxed);
  while (peak < occupancy &&
         !peakOccupancyFrames_.compare_exchange_weak(
           peak, occupancy, std::memory_order_relaxed, std::memory_order_relaxed)) {
  }
}

} // namespace genplusgx
