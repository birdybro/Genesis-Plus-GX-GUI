#include "genplusgx/timing/frame_pacer.h"

#include <algorithm>
#include <limits>

namespace genplusgx {
namespace {

constexpr std::uint64_t nanosecondsPerSecond = 1'000'000'000U;
constexpr std::uint64_t percentDenominator = 100U;
constexpr std::uint64_t maximumCatchUpFrames = 8U;

} // namespace

bool FrameRateRatio::valid() const noexcept
{
  if (framesNumerator == 0U || framesDenominator == 0U ||
      framesNumerator >
        std::numeric_limits<std::uint64_t>::max() /
          maximumFastForwardSpeedPercent ||
      framesDenominator >
        std::numeric_limits<std::uint64_t>::max() /
          (nanosecondsPerSecond * percentDenominator)) {
    return false;
  }
  return framesNumerator * maximumFastForwardSpeedPercent <=
    framesDenominator * nanosecondsPerSecond * percentDenominator;
}

double FrameRateRatio::hertz() const noexcept
{
  if (!valid()) {
    return 0.0;
  }
  return static_cast<double>(framesNumerator) /
         static_cast<double>(framesDenominator);
}

bool FramePacer::configure(FrameRateRatio frameRate) noexcept
{
  if (!frameRate.valid()) {
    return false;
  }
  frameRate_ = frameRate;
  active_ = false;
  speedMode_ = EmulationSpeedMode::normal;
  speedPercent_ = 100U;
  accumulatedRemainder_ = 0U;
  metrics_ = {};
  return rebuildInterval();
}

bool FramePacer::setSpeed(
  EmulationSpeedMode mode,
  std::uint32_t speedPercent,
  TimePoint now) noexcept
{
  if (!frameRate_.valid() ||
      !validateEmulationSpeedForMode(mode, speedPercent)) {
    return false;
  }
  if (speedMode_ == mode && speedPercent_ == speedPercent) {
    return true;
  }
  const auto previousMode = speedMode_;
  const auto previousPercent = speedPercent_;
  speedMode_ = mode;
  speedPercent_ = speedPercent;
  if (!rebuildInterval()) {
    speedMode_ = previousMode;
    speedPercent_ = previousPercent;
    static_cast<void>(rebuildInterval());
    return false;
  }
  if (active_) {
    deadline_ = now;
    accumulatedRemainder_ = 0U;
  }
  return true;
}

bool FramePacer::setFastForward(bool enabled, TimePoint now) noexcept
{
  return setSpeed(
    enabled ? EmulationSpeedMode::fastForward
            : EmulationSpeedMode::normal,
    enabled ? 400U : 100U,
    now);
}

void FramePacer::resume(TimePoint now) noexcept
{
  if (!frameRate_.valid()) {
    return;
  }
  deadline_ = now;
  accumulatedRemainder_ = 0U;
  active_ = true;
}

void FramePacer::pause() noexcept
{
  active_ = false;
}

void FramePacer::frameExecuted(TimePoint now) noexcept
{
  if (!active_) {
    return;
  }
  ++metrics_.scheduledFrames;
  if (now > deadline_) {
    ++metrics_.lateFrames;
    metrics_.maximumLateness =
      std::max(metrics_.maximumLateness,
        std::chrono::duration_cast<std::chrono::nanoseconds>(now - deadline_));
  }

  advanceDeadline();
  const auto intervalCeiling = std::chrono::nanoseconds{
    static_cast<std::chrono::nanoseconds::rep>(
      intervalWholeNanoseconds_ + (intervalRemainder_ == 0U ? 0U : 1U))};
  if (deadline_ < now &&
      now - deadline_ > intervalCeiling * maximumCatchUpFrames) {
    ++metrics_.resynchronizations;
    deadline_ = now;
    accumulatedRemainder_ = 0U;
    advanceDeadline();
  }
}

void FramePacer::resetMetrics() noexcept
{
  metrics_.scheduledFrames = 0U;
  metrics_.lateFrames = 0U;
  metrics_.resynchronizations = 0U;
  metrics_.maximumLateness = std::chrono::nanoseconds{0};
}

bool FramePacer::isActive() const noexcept
{
  return active_;
}

bool FramePacer::isFastForward() const noexcept
{
  return speedMode_ == EmulationSpeedMode::fastForward;
}

bool FramePacer::isSlowMotion() const noexcept
{
  return speedMode_ == EmulationSpeedMode::slowMotion;
}

EmulationSpeedMode FramePacer::speedMode() const noexcept
{
  return speedMode_;
}

std::uint32_t FramePacer::speedPercent() const noexcept
{
  return speedPercent_;
}

FrameRateRatio FramePacer::frameRate() const noexcept
{
  return frameRate_;
}

std::optional<FramePacer::TimePoint> FramePacer::nextDeadline() const noexcept
{
  if (!active_) {
    return std::nullopt;
  }
  return deadline_;
}

std::chrono::nanoseconds FramePacer::nominalFrameDuration() const noexcept
{
  if (!frameRate_.valid()) {
    return std::chrono::nanoseconds{0};
  }
  return std::chrono::nanoseconds{
    static_cast<std::chrono::nanoseconds::rep>(intervalWholeNanoseconds_)};
}

FramePacerMetrics FramePacer::metrics() const noexcept
{
  return metrics_;
}

bool FramePacer::rebuildInterval() noexcept
{
  if (!frameRate_.valid()) {
    return false;
  }
  const auto intervalNumerator = frameRate_.framesDenominator *
    nanosecondsPerSecond * percentDenominator;
  intervalDenominator_ = frameRate_.framesNumerator * speedPercent_;
  intervalWholeNanoseconds_ = intervalNumerator / intervalDenominator_;
  intervalRemainder_ = intervalNumerator % intervalDenominator_;
  if (intervalWholeNanoseconds_ == 0U) {
    return false;
  }
  metrics_.targetFramesPerSecond =
    frameRate_.hertz() * static_cast<double>(speedPercent_) /
      static_cast<double>(percentDenominator);
  metrics_.speedPercent = speedPercent_;
  metrics_.fastForward = speedMode_ == EmulationSpeedMode::fastForward;
  metrics_.slowMotion = speedMode_ == EmulationSpeedMode::slowMotion;
  return true;
}

void FramePacer::advanceDeadline() noexcept
{
  auto delta = intervalWholeNanoseconds_;
  if (intervalRemainder_ > 0U &&
      accumulatedRemainder_ >= intervalDenominator_ - intervalRemainder_) {
    ++delta;
    accumulatedRemainder_ -= intervalDenominator_ - intervalRemainder_;
  } else {
    accumulatedRemainder_ += intervalRemainder_;
  }
  deadline_ += std::chrono::nanoseconds{
    static_cast<std::chrono::nanoseconds::rep>(delta)};
}

} // namespace genplusgx
