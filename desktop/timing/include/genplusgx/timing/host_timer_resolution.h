#pragma once

#include <chrono>
#include <cstdint>

namespace genplusgx {

// Requests a process timer resolution suitable for short emulation deadlines.
// Windows requires an explicit request for condition-variable waits below the
// default scheduler tick; other supported platforms need no adjustment.
class ScopedHostTimerResolution final {
public:
  explicit ScopedHostTimerResolution(
    std::chrono::milliseconds resolution = std::chrono::milliseconds{1}) noexcept;
  ~ScopedHostTimerResolution();

  ScopedHostTimerResolution(const ScopedHostTimerResolution&) = delete;
  ScopedHostTimerResolution& operator=(const ScopedHostTimerResolution&) = delete;
  ScopedHostTimerResolution(ScopedHostTimerResolution&&) = delete;
  ScopedHostTimerResolution& operator=(ScopedHostTimerResolution&&) = delete;

  [[nodiscard]] bool active() const noexcept;

private:
  std::uint32_t requestedMilliseconds_{0};
  bool active_{false};
};

} // namespace genplusgx
