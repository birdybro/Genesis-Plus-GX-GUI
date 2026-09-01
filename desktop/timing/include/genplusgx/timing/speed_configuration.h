#pragma once

#include <cstdint>

namespace genplusgx {

enum class EmulationSpeedMode : std::uint8_t {
  normal,
  fastForward,
  slowMotion,
};

struct EmulationSpeedConfiguration final {
  std::uint32_t normalPercent{100U};
  std::uint32_t slowMotionPercent{50U};
  std::uint32_t fastForwardPercent{400U};

  friend bool operator==(
    const EmulationSpeedConfiguration&,
    const EmulationSpeedConfiguration&) = default;
};

inline constexpr std::uint32_t minimumNormalSpeedPercent = 50U;
inline constexpr std::uint32_t maximumNormalSpeedPercent = 200U;
inline constexpr std::uint32_t minimumSlowMotionSpeedPercent = 25U;
inline constexpr std::uint32_t maximumSlowMotionSpeedPercent = 75U;
inline constexpr std::uint32_t minimumFastForwardSpeedPercent = 200U;
inline constexpr std::uint32_t maximumFastForwardSpeedPercent = 1'600U;

[[nodiscard]] constexpr bool validateEmulationSpeedForMode(
  EmulationSpeedMode mode,
  std::uint32_t speedPercent) noexcept
{
  switch (mode) {
    case EmulationSpeedMode::normal:
      return speedPercent >= minimumNormalSpeedPercent &&
        speedPercent <= maximumNormalSpeedPercent;
    case EmulationSpeedMode::fastForward:
      return speedPercent >= minimumFastForwardSpeedPercent &&
        speedPercent <= maximumFastForwardSpeedPercent;
    case EmulationSpeedMode::slowMotion:
      return speedPercent >= minimumSlowMotionSpeedPercent &&
        speedPercent <= maximumSlowMotionSpeedPercent;
  }
  return false;
}

[[nodiscard]] constexpr bool validateEmulationSpeedConfiguration(
  const EmulationSpeedConfiguration& configuration) noexcept
{
  return validateEmulationSpeedForMode(
           EmulationSpeedMode::normal, configuration.normalPercent) &&
    validateEmulationSpeedForMode(
      EmulationSpeedMode::slowMotion, configuration.slowMotionPercent) &&
    validateEmulationSpeedForMode(
      EmulationSpeedMode::fastForward, configuration.fastForwardPercent);
}

[[nodiscard]] constexpr std::uint32_t speedPercentForMode(
  const EmulationSpeedConfiguration& configuration,
  EmulationSpeedMode mode) noexcept
{
  switch (mode) {
    case EmulationSpeedMode::normal:
      return configuration.normalPercent;
    case EmulationSpeedMode::fastForward:
      return configuration.fastForwardPercent;
    case EmulationSpeedMode::slowMotion:
      return configuration.slowMotionPercent;
  }
  return configuration.normalPercent;
}

} // namespace genplusgx
