#pragma once

#include "genplusgx/emulation_capture_sink.h"

#include <memory>
#include <vector>

namespace genplusgx::capture {

class CaptureFanout final : public EmulationCaptureSink {
public:
  explicit CaptureFanout(
    std::vector<std::shared_ptr<EmulationCaptureSink>> sinks);

  [[nodiscard]] bool active() const noexcept override;
  [[nodiscard]] bool submitFrame(
    const CoreVideoFrameInfo& video,
    std::span<const std::uint16_t> rgb565Pixels,
    const CoreAudioBatchInfo& audio,
    std::span<const StereoAudioFrame> audioFrames) noexcept override;

private:
  std::vector<std::shared_ptr<EmulationCaptureSink>> sinks_;
};

} // namespace genplusgx::capture
