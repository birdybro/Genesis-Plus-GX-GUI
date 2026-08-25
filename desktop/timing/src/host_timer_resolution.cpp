#include "genplusgx/timing/host_timer_resolution.h"

#include <limits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <timeapi.h>
#endif

namespace genplusgx {

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
