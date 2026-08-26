#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace genplusgx {

class FrameRateSampler final {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  explicit FrameRateSampler(
    std::chrono::milliseconds minimumInterval = std::chrono::milliseconds{500});

  [[nodiscard]] std::optional<double> observe(
    std::uint64_t completedFrames,
    bool running,
    TimePoint now) noexcept;
  void reset() noexcept;

private:
  std::chrono::milliseconds minimumInterval_;
  TimePoint previousTime_{};
  std::uint64_t previousFrames_{0};
  bool running_{false};
  bool initialized_{false};
};

} // namespace genplusgx
