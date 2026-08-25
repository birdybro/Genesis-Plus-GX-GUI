#include "genplusgx/timing/frame_pacer.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

using namespace std::chrono_literals;

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

} // namespace

int main()
{
  constexpr genplusgx::FrameRateRatio ntsc{
    53'693'175U, 3'420U * 262U};
  constexpr genplusgx::FrameRateRatio pal{
    53'203'424U, 3'420U * 313U};
  constexpr std::uint64_t intervalNumerator =
    ntsc.framesDenominator * 1'000'000'000U;
  constexpr auto ntscWholeInterval = std::chrono::nanoseconds{
    static_cast<std::chrono::nanoseconds::rep>(
      intervalNumerator / ntsc.framesNumerator)};
  if (!check(ntsc.valid() && pal.valid(), "Authoritative video rates are invalid") ||
      !check(std::abs(ntsc.hertz() - 59.9227434) < 0.000001,
        "NTSC rational rate changed") ||
      !check(std::abs(pal.hertz() - 49.7014590) < 0.000001,
        "PAL rational rate changed") ||
      !check(!genplusgx::FrameRateRatio{}.valid(), "Zero frame rate was accepted") ||
      !check(!genplusgx::FrameRateRatio{
          std::numeric_limits<std::uint64_t>::max(), 1U}.valid(),
        "Overflowing frame rate was accepted")) {
    return 1;
  }

  genplusgx::FramePacer pacer;
  const genplusgx::FramePacer::TimePoint origin{};
  if (!check(pacer.configure(ntsc), "NTSC pacer configuration failed") ||
      !check(!pacer.isActive() && !pacer.nextDeadline().has_value(),
        "Configured pacer started without resume") ||
      !check(pacer.nominalFrameDuration() == ntscWholeInterval,
        "NTSC whole-nanosecond interval changed")) {
    return 2;
  }

  pacer.resume(origin);
  for (std::uint64_t frame = 0U; frame < 600U; ++frame) {
    const auto deadline = pacer.nextDeadline();
    if (!check(deadline.has_value(), "Active pacer lost its deadline")) {
      return 3;
    }
    pacer.frameExecuted(*deadline);
  }
  const auto expectedElapsed = std::chrono::nanoseconds{
    static_cast<std::chrono::nanoseconds::rep>(
      (intervalNumerator * 600U) / ntsc.framesNumerator)};
  if (!check(pacer.nextDeadline() == origin + expectedElapsed,
        "Rational remainder accumulation drifted over 600 frames") ||
      !check(pacer.metrics().scheduledFrames == 600U &&
          pacer.metrics().lateFrames == 0U &&
          pacer.metrics().resynchronizations == 0U,
        "On-deadline frames produced timing anomalies")) {
    return 4;
  }

  pacer.pause();
  pacer.frameExecuted(origin + 30s);
  if (!check(!pacer.nextDeadline().has_value() &&
          pacer.metrics().scheduledFrames == 600U,
        "Paused pacer scheduled a frame")) {
    return 5;
  }

  const auto fastOrigin = origin + 40s;
  pacer.resume(fastOrigin);
  if (!check(pacer.setFastForward(true, fastOrigin),
        "Fast-forward mode was rejected") ||
      !check(pacer.isFastForward() &&
          std::abs(pacer.metrics().targetFramesPerSecond - ntsc.hertz() * 4.0) <
            0.000001,
        "Fast-forward target rate is not exactly 4x")) {
    return 6;
  }
  for (int frame = 0; frame < 4; ++frame) {
    pacer.frameExecuted(*pacer.nextDeadline());
  }
  const auto oneNormalInterval = std::chrono::nanoseconds{
    static_cast<std::chrono::nanoseconds::rep>(
      intervalNumerator / ntsc.framesNumerator)};
  if (!check(*pacer.nextDeadline() == fastOrigin + oneNormalInterval,
        "Four fast-forward frames did not equal one normal interval")) {
    return 7;
  }

  if (!check(pacer.configure({60U, 1U}), "Integer-rate pacer failed") ||
      !check(pacer.setFastForward(false, origin), "Normal-rate selection failed")) {
    return 8;
  }
  pacer.resume(origin);
  pacer.frameExecuted(origin + 50ms);
  const auto lateMetrics = pacer.metrics();
  if (!check(lateMetrics.lateFrames == 1U &&
          lateMetrics.resynchronizations == 1U &&
          lateMetrics.maximumLateness == 50ms,
        "Late-frame instrumentation or bounded catch-up changed") ||
      !check(*pacer.nextDeadline() > origin + 50ms,
        "Severely late scheduler retained a past deadline")) {
    return 9;
  }

  pacer.resetMetrics();
  if (!check(pacer.metrics().scheduledFrames == 0U &&
          pacer.metrics().lateFrames == 0U &&
          pacer.metrics().resynchronizations == 0U &&
          pacer.metrics().targetFramesPerSecond == 60.0,
        "Metric reset changed configuration or retained counters")) {
    return 10;
  }
  return 0;
}
