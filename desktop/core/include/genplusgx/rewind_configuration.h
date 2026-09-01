#pragma once

#include <cstddef>
#include <cstdint>

namespace genplusgx {

struct RewindConfiguration final {
  bool enabled{true};
  std::uint32_t captureIntervalFrames{6U};
  std::size_t memoryLimitBytes{128U * 1024U * 1024U};

  friend bool operator==(const RewindConfiguration&, const RewindConfiguration&) =
    default;
};

[[nodiscard]] constexpr bool validateRewindConfiguration(
  const RewindConfiguration& configuration) noexcept
{
  constexpr std::size_t minimumMemoryBytes = 16U * 1024U * 1024U;
  constexpr std::size_t maximumMemoryBytes = 1024U * 1024U * 1024U;
  return configuration.captureIntervalFrames >= 1U &&
    configuration.captureIntervalFrames <= 60U &&
    configuration.memoryLimitBytes >= minimumMemoryBytes &&
    configuration.memoryLimitBytes <= maximumMemoryBytes;
}

} // namespace genplusgx
