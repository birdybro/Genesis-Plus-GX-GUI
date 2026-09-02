#include "genplusgx/capture/capture_fanout.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace genplusgx::capture {

CaptureFanout::CaptureFanout(
  std::vector<std::shared_ptr<EmulationCaptureSink>> sinks)
  : sinks_(std::move(sinks))
{
  if (sinks_.empty() ||
      std::ranges::any_of(sinks_, [](const auto& sink) { return !sink; })) {
    throw std::invalid_argument{
      "A capture fan-out requires at least one valid sink."};
  }
}

bool CaptureFanout::active() const noexcept
{
  return std::ranges::any_of(
    sinks_, [](const auto& sink) { return sink->active(); });
}

bool CaptureFanout::submitFrame(
  const CoreVideoFrameInfo& video,
  std::span<const std::uint16_t> rgb565Pixels,
  const CoreAudioBatchInfo& audio,
  std::span<const StereoAudioFrame> audioFrames) noexcept
{
  bool submitted = false;
  for (const auto& sink : sinks_) {
    if (sink->active()) {
      submitted = sink->submitFrame(
        video, rgb565Pixels, audio, audioFrames) || submitted;
    }
  }
  return submitted;
}

} // namespace genplusgx::capture
