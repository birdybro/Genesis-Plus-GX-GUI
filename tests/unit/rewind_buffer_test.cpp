#include "genplusgx/rewind_buffer.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const std::string& message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::vector<std::uint8_t> state(std::size_t bytes, std::uint8_t value)
{
  return std::vector<std::uint8_t>(bytes, value);
}

} // namespace

int main()
{
  using namespace genplusgx;
  constexpr std::size_t mebibyte = 1024U * 1024U;

  RewindBuffer history{{
    .enabled = true,
    .captureIntervalFrames = 3U,
    .memoryLimitBytes = 16U * mebibyte,
  }};
  if (!check(history.shouldCapture(0U) && !history.shouldCapture(1U) &&
        history.shouldCapture(3U), "Capture interval selection is incorrect") ||
      !check(history.capture(0U, state(6U * mebibyte, 0x10U)),
        "Initial rewind snapshot was rejected") ||
      !check(history.capture(3U, state(6U * mebibyte, 0x20U)) &&
        history.capture(6U, state(6U * mebibyte, 0x30U)),
        "Ordered rewind snapshots were rejected")) {
    return EXIT_FAILURE;
  }

  const auto bounded = history.metrics();
  if (!check(bounded.snapshotCount == 2U &&
        bounded.payloadBytes == 12U * mebibyte &&
        bounded.payloadBytes <= bounded.memoryLimitBytes &&
        bounded.discardedSnapshots == 1U,
        "Rewind history did not evict the oldest state at its byte limit") ||
      !check(!history.capture(6U, state(1U, 0x40U)),
        "A duplicate frame was accepted into rewind history") ||
      !check(history.canRewind(8U) && history.canRewind(7U) &&
        !history.canRewind(4U) && !history.canRewind(1U),
        "Rewind availability ignored the current frame")) {
    return EXIT_FAILURE;
  }

  const auto previous = history.takePrevious(8U);
  if (!check(previous && previous->frameNumber == 6U &&
        previous->rawState.front() == 0x30U,
        "The newest earlier snapshot was not restored") ||
      !check(!history.takePrevious(3U),
        "A snapshot at the current frame was incorrectly returned") ||
      !check(history.metrics().snapshotCount == 0U &&
        history.metrics().payloadBytes == 0U,
        "Popped rewind snapshots retained payload accounting")) {
    return EXIT_FAILURE;
  }

  const auto validConfiguration = history.configuration();
  if (!check(!history.configure({
          .enabled = true,
          .captureIntervalFrames = 0U,
          .memoryLimitBytes = 16U * mebibyte,
        }) && history.configuration() == validConfiguration,
        "Invalid rewind configuration changed the active limits") ||
      !check(history.configure({
          .enabled = false,
          .captureIntervalFrames = 6U,
          .memoryLimitBytes = 32U * mebibyte,
        }) && !history.shouldCapture(6U) &&
        !history.capture(6U, state(1U, 0x50U)),
        "Disabled rewind history accepted a snapshot")) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
