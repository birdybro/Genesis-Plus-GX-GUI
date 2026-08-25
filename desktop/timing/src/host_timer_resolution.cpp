#include "genplusgx/timing/host_timer_resolution.h"

#include <algorithm>
#include <limits>
#include <thread>

#if defined(__APPLE__)
#include <mach/mach_time.h>
#include <pthread.h>
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <timeapi.h>
#endif

namespace genplusgx {

bool configureCurrentThreadForInteractiveTiming() noexcept
{
#if defined(__APPLE__)
  return pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) == 0;
#else
  return true;
#endif
}

void sleepUntilHostDeadline(
  std::chrono::steady_clock::time_point deadline) noexcept
{
#if defined(__APPLE__)
  const auto now = std::chrono::steady_clock::now();
  if (now >= deadline) {
    return;
  }
  const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(
    deadline - now);
  mach_timebase_info_data_t timebase{};
  if (mach_timebase_info(&timebase) != 0 || timebase.numer == 0U ||
      timebase.denom == 0U ||
      remaining.count() <= 0 ||
      static_cast<std::uint64_t>(remaining.count()) >
        std::numeric_limits<std::uint64_t>::max() / timebase.denom) {
    std::this_thread::sleep_until(deadline);
    return;
  }
  const auto ticks = std::max<std::uint64_t>(1U,
    static_cast<std::uint64_t>(remaining.count()) * timebase.denom /
      timebase.numer);
  const auto target = mach_absolute_time() + ticks;
  while (mach_absolute_time() < target) {
    static_cast<void>(mach_wait_until(target));
  }
#else
  std::this_thread::sleep_until(deadline);
#endif
}

ScopedHostTimerResolution::ScopedHostTimerResolution(
  std::chrono::milliseconds resolution) noexcept
{
  if (resolution.count() <= 0 ||
      static_cast<std::uint64_t>(resolution.count()) >
        std::numeric_limits<std::uint32_t>::max()) {
    return;
  }
  requestedMilliseconds_ = static_cast<std::uint32_t>(resolution.count());
#if defined(_WIN32)
  active_ = timeBeginPeriod(requestedMilliseconds_) == TIMERR_NOERROR;
#else
  active_ = true;
#endif
}

ScopedHostTimerResolution::~ScopedHostTimerResolution()
{
#if defined(_WIN32)
  if (active_) {
    static_cast<void>(timeEndPeriod(requestedMilliseconds_));
  }
#endif
}

bool ScopedHostTimerResolution::active() const noexcept
{
  return active_;
}

} // namespace genplusgx
