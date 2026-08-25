#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace genplusgx {

enum class InputButton : std::uint16_t {
  up = 1U << 0U,
  down = 1U << 1U,
  left = 1U << 2U,
  right = 1U << 3U,
  a = 1U << 4U,
  b = 1U << 5U,
  c = 1U << 6U,
  start = 1U << 7U,
  x = 1U << 8U,
  y = 1U << 9U,
  z = 1U << 10U,
  mode = 1U << 11U,
};

using InputButtonSet = std::uint16_t;

[[nodiscard]] constexpr InputButtonSet buttonMask(InputButton button) noexcept
{
  return static_cast<InputButtonSet>(button);
}

[[nodiscard]] constexpr InputButtonSet operator|(
  InputButton left,
  InputButton right) noexcept
{
  return static_cast<InputButtonSet>(buttonMask(left) | buttonMask(right));
}

[[nodiscard]] constexpr InputButtonSet operator|(
  InputButtonSet buttons,
  InputButton button) noexcept
{
  return static_cast<InputButtonSet>(buttons | buttonMask(button));
}

[[nodiscard]] constexpr bool hasButton(
  InputButtonSet buttons,
  InputButton button) noexcept
{
  return (buttons & buttonMask(button)) != 0U;
}

struct InputDeviceState final {
  bool connected{false};
  InputButtonSet buttons{0};
  std::int16_t analogX{0};
  std::int16_t analogY{0};
};

struct InputSnapshot final {
  static constexpr std::size_t maximumPlayers = 8U;

  std::array<InputDeviceState, maximumPlayers> players{};
  std::uint64_t sequence{0};
};

} // namespace genplusgx
