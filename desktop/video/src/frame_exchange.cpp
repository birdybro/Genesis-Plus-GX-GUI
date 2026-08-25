#include "genplusgx/video/frame_exchange.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace genplusgx {

class VideoFrameExchange::Private final {
public:
  struct Slot final {
    std::vector<std::uint16_t> pixels;
    CoreVideoFrameInfo frame;
    std::uint64_t generation{0};
    std::size_t readers{0};
    bool writing{false};
  };

  explicit Private(std::size_t maximumPixelCount)
    : maximumPixels(maximumPixelCount)
  {
    for (auto& slot : slots) {
      slot.pixels.resize(maximumPixelCount, 0U);
    }
  }

  mutable std::mutex mutex;
  std::array<Slot, VideoFrameExchange::slotCount> slots;
  std::optional<std::size_t> latest;
  std::size_t nextCandidate{0};
  std::size_t maximumPixels;
  std::uint64_t nextGeneration{1};
  std::uint64_t lastCopiedGeneration{0};
  VideoExchangeMetrics metrics;
};

VideoWriteLease::VideoWriteLease(
  VideoFrameExchange& exchange,
  std::size_t slot,
  std::span<std::uint16_t> pixels) noexcept
  : exchange_(&exchange), slot_(slot), pixels_(pixels)
{
}

VideoWriteLease::~VideoWriteLease()
{
  cancel();
}

VideoWriteLease::VideoWriteLease(VideoWriteLease&& other) noexcept
  : exchange_(std::exchange(other.exchange_, nullptr)),
    slot_(other.slot_),
    pixels_(other.pixels_)
{
  other.pixels_ = {};
}

VideoWriteLease& VideoWriteLease::operator=(VideoWriteLease&& other) noexcept
{
  if (this != &other) {
    cancel();
    exchange_ = std::exchange(other.exchange_, nullptr);
    slot_ = other.slot_;
    pixels_ = other.pixels_;
    other.pixels_ = {};
  }
  return *this;
}

std::span<std::uint16_t> VideoWriteLease::pixels() noexcept
{
  return pixels_;
}

VideoExchangeStatus VideoWriteLease::publish(const CoreVideoFrameInfo& frame)
{
  if (exchange_ == nullptr) {
    return {VideoExchangeError::noWritableSlot};
  }
  auto* exchange = std::exchange(exchange_, nullptr);
  pixels_ = {};
  return exchange->publish(slot_, frame);
}

void VideoWriteLease::cancel() noexcept
{
  if (exchange_ != nullptr) {
    auto* exchange = std::exchange(exchange_, nullptr);
    exchange->cancel(slot_);
  }
  pixels_ = {};
}

VideoFrameExchange::VideoFrameExchange(std::size_t maximumPixels)
{
  if (maximumPixels == 0U) {
    throw std::invalid_argument{"A video frame exchange requires nonzero storage."};
  }
  private_ = std::make_unique<Private>(maximumPixels);
  private_->metrics.allocatedPixels = maximumPixels * slotCount;
}

VideoFrameExchange::~VideoFrameExchange() = default;

std::optional<VideoWriteLease> VideoFrameExchange::beginWrite()
{
  std::scoped_lock lock{private_->mutex};
  for (std::size_t offset = 0; offset < slotCount; ++offset) {
    const auto candidate = (private_->nextCandidate + offset) % slotCount;
    auto& slot = private_->slots[candidate];
    if (!slot.writing && slot.readers == 0U && private_->latest != candidate) {
      slot.writing = true;
      private_->nextCandidate = (candidate + 1U) % slotCount;
      return VideoWriteLease{*this, candidate, slot.pixels};
    }
  }
  ++private_->metrics.producerDrops;
  return std::nullopt;
}

VideoExchangeStatus VideoFrameExchange::publish(
  std::size_t slotIndex,
  const CoreVideoFrameInfo& frame)
{
  std::scoped_lock lock{private_->mutex};
  if (slotIndex >= slotCount || !private_->slots[slotIndex].writing) {
    return {VideoExchangeError::noWritableSlot};
  }
  auto& slot = private_->slots[slotIndex];
  slot.writing = false;
  if (frame.width == 0U || frame.height == 0U ||
      frame.pixelCount() > private_->maximumPixels) {
    return {VideoExchangeError::invalidFrame};
  }

  slot.frame = frame;
  slot.generation = private_->nextGeneration++;
  private_->latest = slotIndex;
  ++private_->metrics.publishedFrames;
  return {};
}

void VideoFrameExchange::cancel(std::size_t slotIndex) noexcept
{
  std::scoped_lock lock{private_->mutex};
  if (slotIndex < slotCount) {
    private_->slots[slotIndex].writing = false;
  }
}

VideoExchangeStatus VideoFrameExchange::copyLatest(
  std::span<std::uint16_t> destination,
  CoreVideoFrameInfo& frame,
  std::uint64_t& generation)
{
  std::size_t slotIndex = 0;
  {
    std::scoped_lock lock{private_->mutex};
    if (!private_->latest) {
      return {VideoExchangeError::noFrameAvailable};
    }
    slotIndex = *private_->latest;
    const auto& slot = private_->slots[slotIndex];
    if (destination.size() < slot.frame.pixelCount()) {
      return {VideoExchangeError::destinationTooSmall};
    }
    frame = slot.frame;
    generation = slot.generation;
    ++private_->slots[slotIndex].readers;
  }

  const auto pixelCount = frame.pixelCount();
  std::ranges::copy_n(
    private_->slots[slotIndex].pixels.begin(), pixelCount, destination.begin());

  {
    std::scoped_lock lock{private_->mutex};
    --private_->slots[slotIndex].readers;
    if (private_->lastCopiedGeneration != 0U &&
        generation > private_->lastCopiedGeneration + 1U) {
      private_->metrics.skippedFrames +=
        generation - private_->lastCopiedGeneration - 1U;
    }
    private_->lastCopiedGeneration = generation;
    ++private_->metrics.copiedFrames;
  }
  return {};
}

VideoExchangeMetrics VideoFrameExchange::metrics() const
{
  std::scoped_lock lock{private_->mutex};
  return private_->metrics;
}

std::size_t VideoFrameExchange::maximumPixels() const noexcept
{
  return private_->maximumPixels;
}

void VideoFrameExchange::clear() noexcept
{
  std::scoped_lock lock{private_->mutex};
  private_->latest.reset();
  private_->lastCopiedGeneration = 0;
}

} // namespace genplusgx
