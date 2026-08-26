#include "genplusgx/audio_ring_buffer.h"

#include <algorithm>
#include <stdexcept>
#include <thread>

namespace genplusgx {

StereoAudioRingBuffer::StereoAudioRingBuffer(
  std::size_t capacityFrames,
  std::size_t maximumCapacityFrames)
  : storage_(maximumCapacityFrames == 0U ? capacityFrames : maximumCapacityFrames),
    capacityFrames_(capacityFrames)
{
  if (capacityFrames == 0U || storage_.size() < capacityFrames) {
    throw std::invalid_argument{
      "Audio ring capacity must be nonzero and no greater than its storage."};
  }
}

AudioRingWriteResult StereoAudioRingBuffer::write(
  std::span<const StereoAudioFrame> frames) noexcept
{
  if (!beginOperation()) {
    if (!frames.empty()) {
      overrunCount_.fetch_add(1U, std::memory_order_relaxed);
      droppedFrames_.fetch_add(frames.size(), std::memory_order_relaxed);
    }
    return {.acceptedFrames = 0U, .droppedFrames = frames.size()};
  }
  const auto capacity = capacityFrames_.load(std::memory_order_relaxed);
  const auto writeSequence = writeSequence_.load(std::memory_order_relaxed);
  const auto readSequence = readSequence_.load(std::memory_order_acquire);
  const auto occupied = static_cast<std::size_t>(writeSequence - readSequence);
  const auto available = capacity - std::min(occupied, capacity);
  const auto accepted = std::min(frames.size(), available);

  for (std::size_t index = 0; index < accepted; ++index) {
    storage_[static_cast<std::size_t>((writeSequence + index) % capacity)] =
      frames[index];
  }
  writeSequence_.store(writeSequence + accepted, std::memory_order_release);
  updatePeak(occupied + accepted);
  endOperation();

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
  if (!beginOperation()) {
    if (!destination.empty()) {
      underrunCount_.fetch_add(1U, std::memory_order_relaxed);
      missingFrames_.fetch_add(destination.size(), std::memory_order_relaxed);
    }
    return {.providedFrames = 0U, .missingFrames = destination.size()};
  }
  const auto capacity = capacityFrames_.load(std::memory_order_relaxed);
  const auto readSequence = readSequence_.load(std::memory_order_relaxed);
  const auto writeSequence = writeSequence_.load(std::memory_order_acquire);
  const auto available = static_cast<std::size_t>(writeSequence - readSequence);
  const auto provided = std::min(destination.size(), available);

  for (std::size_t index = 0; index < provided; ++index) {
    destination[index] =
      storage_[static_cast<std::size_t>((readSequence + index) % capacity)];
  }
  readSequence_.store(readSequence + provided, std::memory_order_release);
  endOperation();

  const auto missing = destination.size() - provided;
  if (missing > 0U) {
    underrunCount_.fetch_add(1U, std::memory_order_relaxed);
    missingFrames_.fetch_add(missing, std::memory_order_relaxed);
  }
  return {.providedFrames = provided, .missingFrames = missing};
}

std::size_t StereoAudioRingBuffer::capacityFrames() const noexcept
{
  return capacityFrames_.load(std::memory_order_acquire);
}

std::size_t StereoAudioRingBuffer::maximumCapacityFrames() const noexcept
{
  return storage_.size();
}

std::size_t StereoAudioRingBuffer::occupancyFrames() const noexcept
{
  if (!beginOperation()) {
    return 0U;
  }
  const auto capacity = capacityFrames_.load(std::memory_order_relaxed);
  const auto writeSequence = writeSequence_.load(std::memory_order_acquire);
  const auto readSequence = readSequence_.load(std::memory_order_acquire);
  const auto result = std::min(
    static_cast<std::size_t>(writeSequence - readSequence), capacity);
  endOperation();
  return result;
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

bool StereoAudioRingBuffer::setCapacityFrames(std::size_t capacityFrames) noexcept
{
  if (capacityFrames == 0U || capacityFrames > storage_.size()) {
    return false;
  }
  std::scoped_lock lock{reconfigurationMutex_};
  reconfiguring_.store(true, std::memory_order_release);
  while (activeOperations_.load(std::memory_order_acquire) != 0U) {
    std::this_thread::yield();
  }
  readSequence_.store(0U, std::memory_order_relaxed);
  writeSequence_.store(0U, std::memory_order_relaxed);
  capacityFrames_.store(capacityFrames, std::memory_order_release);
  peakOccupancyFrames_.store(0U, std::memory_order_relaxed);
  reconfiguring_.store(false, std::memory_order_release);
  return true;
}

bool StereoAudioRingBuffer::beginOperation() const noexcept
{
  if (reconfiguring_.load(std::memory_order_acquire)) {
    return false;
  }
  activeOperations_.fetch_add(1U, std::memory_order_acq_rel);
  if (reconfiguring_.load(std::memory_order_acquire)) {
    activeOperations_.fetch_sub(1U, std::memory_order_release);
    return false;
  }
  return true;
}

void StereoAudioRingBuffer::endOperation() const noexcept
{
  activeOperations_.fetch_sub(1U, std::memory_order_release);
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
