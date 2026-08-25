#include "genplusgx/video/frame_exchange.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

genplusgx::CoreVideoFrameInfo frameInfo(
  std::uint64_t frameNumber,
  std::uint32_t width = 4U,
  std::uint32_t height = 2U)
{
  return {
    .format = genplusgx::CorePixelFormat::rgb565,
    .width = width,
    .height = height,
    .sourceSurfaceWidth = width,
    .sourceSurfaceHeight = height,
    .sourcePitchPixels = width,
    .frameNumber = frameNumber,
  };
}

} // namespace

int main()
{
  bool rejectedZeroStorage = false;
  try {
    const genplusgx::VideoFrameExchange invalid{0U};
    static_cast<void>(invalid);
  } catch (const std::invalid_argument&) {
    rejectedZeroStorage = true;
  }
  if (!check(rejectedZeroStorage, "A zero-sized frame exchange was accepted")) {
    return 1;
  }

  genplusgx::VideoFrameExchange exchange{8U};
  std::vector<std::uint16_t> destination(8U, 0U);
  genplusgx::CoreVideoFrameInfo copiedInfo;
  std::uint64_t generation = 0;
  if (!check(exchange.copyLatest(destination, copiedInfo, generation).error ==
          genplusgx::VideoExchangeError::noFrameAvailable,
        "Empty exchange returned a frame") ||
      !check(exchange.metrics().allocatedPixels == 24U,
        "Triple-buffer storage was not fixed at construction")) {
    return 2;
  }

  {
    auto write = exchange.beginWrite();
    if (!check(write.has_value() && write->pixels().size() == 8U,
          "Producer could not acquire a fixed frame slot")) {
      return 3;
    }
    const std::uint16_t pattern[]{
      0xF800U, 0xF800U, 0x07E0U, 0x07E0U,
      0x001FU, 0x001FU, 0xFFFFU, 0xFFFFU};
    std::ranges::copy(pattern, write->pixels().begin());
    if (!check(write->publish(frameInfo(10U)), "Initial frame publication failed")) {
      return 4;
    }
  }

  std::vector<std::uint16_t> tooSmall(7U, 0U);
  if (!check(exchange.copyLatest(tooSmall, copiedInfo, generation).error ==
          genplusgx::VideoExchangeError::destinationTooSmall,
        "Undersized frame destination was accepted") ||
      !check(exchange.copyLatest(destination, copiedInfo, generation),
        "Published frame could not be copied") ||
      !check(generation == 1U && copiedInfo.frameNumber == 10U &&
          destination.front() == 0xF800U && destination.back() == 0xFFFFU,
        "Copied frame data or metadata was torn")) {
    return 5;
  }

  for (std::uint64_t frame = 11U; frame <= 12U; ++frame) {
    auto write = exchange.beginWrite();
    if (!check(write.has_value(), "Triple buffer ran out of writable slots")) {
      return 6;
    }
    std::ranges::fill(write->pixels(), static_cast<std::uint16_t>(frame));
    if (!check(write->publish(frameInfo(frame)), "Replacement frame publication failed")) {
      return 7;
    }
  }
  if (!check(exchange.copyLatest(destination, copiedInfo, generation),
        "Newest replacement frame could not be copied") ||
      !check(generation == 3U && copiedInfo.frameNumber == 12U &&
          std::ranges::all_of(destination, [](std::uint16_t pixel) {
            return pixel == 12U;
          }),
        "Consumer did not receive the newest complete frame") ||
      !check(exchange.metrics().publishedFrames == 3U &&
          exchange.metrics().copiedFrames == 2U &&
          exchange.metrics().skippedFrames == 1U,
        "Frame exchange instrumentation was incorrect")) {
    return 8;
  }

  {
    auto cancelled = exchange.beginWrite();
    if (!check(cancelled.has_value(), "Cancelled lease could not be acquired")) {
      return 9;
    }
    cancelled->cancel();
  }
  {
    auto invalid = exchange.beginWrite();
    if (!check(invalid.has_value(), "Post-cancel lease could not be acquired") ||
        !check(invalid->publish(frameInfo(13U, 9U, 1U)).error ==
          genplusgx::VideoExchangeError::invalidFrame,
          "Oversized frame metadata was accepted")) {
      return 10;
    }
  }

  exchange.clear();
  return check(exchange.copyLatest(destination, copiedInfo, generation).error ==
      genplusgx::VideoExchangeError::noFrameAvailable,
    "Cleared exchange retained a published frame")
    ? 0
    : 11;
}
