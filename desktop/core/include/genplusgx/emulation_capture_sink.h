#pragma once

#include "genplusgx/audio_frame.h"
#include "genplusgx/core_adapter.h"

#include <span>

namespace genplusgx {

// Optional frontend tap invoked on the emulation-owner thread after a complete core
// frame has been copied. Implementations must remain non-blocking and bounded.
class EmulationCaptureSink {
public:
  virtual ~EmulationCaptureSink() = default;

  [[nodiscard]] virtual bool active() const noexcept = 0;
  [[nodiscard]] virtual bool submitFrame(
    const CoreVideoFrameInfo& video,
    std::span<const std::uint16_t> rgb565Pixels,
    const CoreAudioBatchInfo& audio,
    std::span<const StereoAudioFrame> audioFrames) noexcept = 0;
};

} // namespace genplusgx
