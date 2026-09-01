#pragma once

#include "genplusgx/timing/speed_configuration.h"

#include <chrono>
#include <cstdint>
#include <optional>

namespace genplusgx {

struct FrameRateRatio final {
  std::uint64_t framesNumerator{0};
  std::uint64_t framesDenominator{0};

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] double hertz() const noexcept;
};

struct FramePacerMetrics final {
  std::uint64_t scheduledFrames{0};
  std::uint64_t lateFrames{0};
  std::uint64_t resynchronizations{0};
  std::chrono::nanoseconds maximumLateness{0};
  double targetFramesPerSecond{0.0};
  std::uint32_t speedPercent{100U};
  bool fastForward{false};
  bool slowMotion{false};
};

class FramePacer final {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  [[nodiscard]] bool configure(FrameRateRatio frameRate) noexcept;
  [[nodiscard]] bool setSpeed(
    EmulationSpeedMode mode,
    std::uint32_t speedPercent,
    TimePoint now) noexcept;
  [[nodiscard]] bool setFastForward(bool enabled, TimePoint now) noexcept;

  void resume(TimePoint now) noexcept;
  void pause() noexcept;
  void frameExecuted(TimePoint now) noexcept;
  void resetMetrics() noexcept;

  [[nodiscard]] bool isActive() const noexcept;
  [[nodiscard]] bool isFastForward() const noexcept;
  [[nodiscard]] bool isSlowMotion() const noexcept;
  [[nodiscard]] EmulationSpeedMode speedMode() const noexcept;
  [[nodiscard]] std::uint32_t speedPercent() const noexcept;
  [[nodiscard]] FrameRateRatio frameRate() const noexcept;
  [[nodiscard]] std::optional<TimePoint> nextDeadline() const noexcept;
  [[nodiscard]] std::chrono::nanoseconds nominalFrameDuration() const noexcept;
  [[nodiscard]] FramePacerMetrics metrics() const noexcept;

private:
  [[nodiscard]] bool rebuildInterval() noexcept;
  void advanceDeadline() noexcept;

  FrameRateRatio frameRate_;
  TimePoint deadline_{};
  std::uint64_t intervalWholeNanoseconds_{0};
  std::uint64_t intervalRemainder_{0};
  std::uint64_t intervalDenominator_{1};
  std::uint64_t accumulatedRemainder_{0};
  FramePacerMetrics metrics_;
  bool active_{false};
  EmulationSpeedMode speedMode_{EmulationSpeedMode::normal};
  std::uint32_t speedPercent_{100U};
};

} // namespace genplusgx
