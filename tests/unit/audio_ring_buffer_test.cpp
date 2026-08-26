#include "genplusgx/audio_ring_buffer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using genplusgx::StereoAudioFrame;
using genplusgx::StereoAudioRingBuffer;

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

StereoAudioFrame numberedFrame(std::size_t number)
{
  const auto value = static_cast<std::int16_t>(number % 30'000U);
  return {.left = value, .right = static_cast<std::int16_t>(-value)};
}

} // namespace

int main()
{
  try {
    const StereoAudioRingBuffer invalid{0};
    (void)invalid;
    std::cerr << "Zero-capacity audio ring was accepted\n";
    return 1;
  } catch (const std::invalid_argument&) {
  }

  StereoAudioRingBuffer ring{4};
  const std::array firstWrite{numberedFrame(1), numberedFrame(2), numberedFrame(3)};
  if (!check(ring.capacityFrames() == 4U, "Ring capacity was incorrect") ||
      !check(ring.write(firstWrite).acceptedFrames == 3U, "Initial ring write failed") ||
      !check(ring.occupancyFrames() == 3U, "Initial occupancy was incorrect")) {
    return 2;
  }

  std::array<StereoAudioFrame, 2> firstRead{};
  if (!check(ring.read(firstRead).providedFrames == 2U, "Initial ring read failed") ||
      !check(firstRead[0] == firstWrite[0] && firstRead[1] == firstWrite[1],
        "Ring did not preserve stereo frame order")) {
    return 3;
  }

  const std::array wrappedWrite{numberedFrame(4), numberedFrame(5), numberedFrame(6)};
  std::array<StereoAudioFrame, 4> wrappedRead{};
  if (!check(ring.write(wrappedWrite).acceptedFrames == 3U, "Wrapped ring write failed") ||
      !check(ring.occupancyFrames() == 4U, "Wrapped occupancy was incorrect") ||
      !check(ring.read(wrappedRead).providedFrames == 4U, "Wrapped ring read failed") ||
      !check(wrappedRead == std::array{numberedFrame(3), numberedFrame(4),
          numberedFrame(5), numberedFrame(6)},
        "Wrapped ring order was incorrect")) {
    return 4;
  }

  const std::array overrunWrite{
    numberedFrame(10), numberedFrame(11), numberedFrame(12), numberedFrame(13),
    numberedFrame(14)};
  const auto overrun = ring.write(overrunWrite);
  std::array<StereoAudioFrame, 6> underrunRead{};
  const auto underrun = ring.read(underrunRead);
  const auto metrics = ring.metrics();
  if (!check(overrun.acceptedFrames == 4U && overrun.droppedFrames == 1U,
        "Ring overrun policy was incorrect") ||
      !check(underrun.providedFrames == 4U && underrun.missingFrames == 2U,
        "Ring underrun policy was incorrect") ||
      !check(metrics.overrunCount == 1U && metrics.underrunCount == 1U &&
          metrics.droppedFrames == 1U && metrics.missingFrames == 2U &&
          metrics.peakOccupancyFrames == 4U,
        "Ring instrumentation was incorrect")) {
    return 5;
  }

  ring.resetMetrics();
  if (!check(ring.metrics().overrunCount == 0U && ring.metrics().underrunCount == 0U,
        "Ring metrics did not reset")) {
    return 6;
  }
  if (!check(ring.write(firstWrite).acceptedFrames == firstWrite.size(),
        "Write before clear failed")) {
    return 7;
  }
  ring.clear();
  if (!check(ring.occupancyFrames() == 0U, "Ring clear retained audio")) {
    return 8;
  }

  StereoAudioRingBuffer resizableRing{4U, 8U};
  if (!check(resizableRing.maximumCapacityFrames() == 8U,
        "Resizable ring did not expose its fixed storage bound") ||
      !check(resizableRing.write(firstWrite).acceptedFrames == firstWrite.size(),
        "Resizable ring fixture write failed") ||
      !check(resizableRing.setCapacityFrames(7U) &&
          resizableRing.capacityFrames() == 7U &&
          resizableRing.occupancyFrames() == 0U,
        "Live ring growth did not clear and publish the logical capacity") ||
      !check(resizableRing.setCapacityFrames(2U) &&
          resizableRing.capacityFrames() == 2U,
        "Live ring shrink failed") ||
      !check(!resizableRing.setCapacityFrames(0U) &&
          !resizableRing.setCapacityFrames(9U) &&
          resizableRing.capacityFrames() == 2U,
        "Invalid live ring capacity changed the active bound")) {
    return 9;
  }

  constexpr std::size_t concurrentFrameCount = 20'000U;
  StereoAudioRingBuffer concurrentRing{1'024};
  std::atomic<bool> failed{false};
  std::thread producer{[&] {
    std::size_t produced = 0;
    std::array<StereoAudioFrame, 97> batch{};
    while (produced < concurrentFrameCount) {
      const auto count = std::min(batch.size(), concurrentFrameCount - produced);
      while ((concurrentRing.capacityFrames() - concurrentRing.occupancyFrames()) < count) {
        std::this_thread::yield();
      }
      for (std::size_t index = 0; index < count; ++index) {
        batch[index] = numberedFrame(produced + index);
      }
      const auto result = concurrentRing.write(std::span{batch}.first(count));
      if (result.acceptedFrames != count || result.droppedFrames != 0U) {
        failed.store(true, std::memory_order_relaxed);
        return;
      }
      produced += count;
    }
  }};

  std::thread consumer{[&] {
    std::size_t consumed = 0;
    std::array<StereoAudioFrame, 83> batch{};
    while (consumed < concurrentFrameCount) {
      const auto count = std::min(
        {batch.size(), concurrentFrameCount - consumed,
          concurrentRing.occupancyFrames()});
      if (count == 0U) {
        std::this_thread::yield();
        continue;
      }
      const auto result = concurrentRing.read(std::span{batch}.first(count));
      if (result.providedFrames != count || result.missingFrames != 0U) {
        failed.store(true, std::memory_order_relaxed);
        return;
      }
      for (std::size_t index = 0; index < count; ++index) {
        if (batch[index] != numberedFrame(consumed + index)) {
          failed.store(true, std::memory_order_relaxed);
          return;
        }
      }
      consumed += count;
    }
  }};

  producer.join();
  consumer.join();
  if (!check(!failed.load(std::memory_order_relaxed),
        "Concurrent SPSC transfer corrupted or dropped frames") ||
      !check(concurrentRing.occupancyFrames() == 0U,
        "Concurrent SPSC transfer left buffered frames") ||
      !check(concurrentRing.metrics().overrunCount == 0U &&
          concurrentRing.metrics().underrunCount == 0U,
        "Concurrent bounded transfer reported an avoidable buffer fault")) {
    return 10;
  }

  StereoAudioRingBuffer reconfiguredConcurrentRing{256U, 2'048U};
  std::atomic<bool> stopReconfigurationWork{false};
  std::thread reconfigurationProducer{[&] {
    const std::array batch{numberedFrame(1), numberedFrame(2), numberedFrame(3)};
    while (!stopReconfigurationWork.load(std::memory_order_acquire)) {
      static_cast<void>(reconfiguredConcurrentRing.write(batch));
    }
  }};
  std::thread reconfigurationConsumer{[&] {
    std::array<StereoAudioFrame, 5> batch{};
    while (!stopReconfigurationWork.load(std::memory_order_acquire)) {
      static_cast<void>(reconfiguredConcurrentRing.read(batch));
    }
  }};
  for (std::size_t iteration = 0U; iteration < 1'000U; ++iteration) {
    const auto capacity = (iteration % 2U) == 0U ? 384U : 1'536U;
    if (!check(reconfiguredConcurrentRing.setCapacityFrames(capacity),
          "Concurrent ring reconfiguration was rejected")) {
      stopReconfigurationWork.store(true, std::memory_order_release);
      reconfigurationProducer.join();
      reconfigurationConsumer.join();
      return 11;
    }
  }
  stopReconfigurationWork.store(true, std::memory_order_release);
  reconfigurationProducer.join();
  reconfigurationConsumer.join();
  if (!check(reconfiguredConcurrentRing.capacityFrames() == 1'536U &&
        reconfiguredConcurrentRing.occupancyFrames() <= 1'536U,
      "Concurrent reconfiguration violated the final logical bound")) {
    return 11;
  }

  return 0;
}
