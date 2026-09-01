#pragma once

#include "genplusgx/rewind_configuration.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace genplusgx {

struct RewindSnapshot final {
  std::uint64_t frameNumber{0U};
  std::vector<std::uint8_t> rawState;
};

struct RewindBufferMetrics final {
  std::size_t snapshotCount{0U};
  std::size_t payloadBytes{0U};
  std::size_t memoryLimitBytes{0U};
  std::uint64_t discardedSnapshots{0U};
};

class RewindBuffer final {
public:
  explicit RewindBuffer(RewindConfiguration configuration = {});

  [[nodiscard]] bool configure(RewindConfiguration configuration);
  [[nodiscard]] const RewindConfiguration& configuration() const noexcept;
  void clear() noexcept;

  [[nodiscard]] bool capture(
    std::uint64_t frameNumber,
    std::vector<std::uint8_t> rawState);
  [[nodiscard]] bool shouldCapture(std::uint64_t frameNumber) const noexcept;
  [[nodiscard]] bool canRewind(std::uint64_t currentFrameNumber) const noexcept;
  [[nodiscard]] std::optional<RewindSnapshot> takePrevious(
    std::uint64_t currentFrameNumber);
  [[nodiscard]] RewindBufferMetrics metrics() const noexcept;

private:
  void trimToLimit() noexcept;

  RewindConfiguration configuration_;
  std::deque<RewindSnapshot> snapshots_;
  std::size_t payloadBytes_{0U};
  std::uint64_t discardedSnapshots_{0U};
};

} // namespace genplusgx
