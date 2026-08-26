#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace genplusgx {

enum class CoreInputDevice : std::uint8_t {
  none,
  pad3Button,
  pad6Button,
  segaMouse,
  lightGun,
  paddle,
  sportsPad,
  xe1Ap,
  pico,
  terebiOekaki,
  graphicBoard,
  activator,
};

struct CoreInputSettings final {
  static constexpr std::size_t maximumPlayers = 8U;

  std::array<CoreInputDevice, maximumPlayers> devices{
    CoreInputDevice::pad6Button,
    CoreInputDevice::pad6Button,
    CoreInputDevice::none,
    CoreInputDevice::none,
    CoreInputDevice::none,
    CoreInputDevice::none,
    CoreInputDevice::none,
    CoreInputDevice::none,
  };

  [[nodiscard]] bool operator==(const CoreInputSettings&) const = default;
};

[[nodiscard]] constexpr bool isCorePad(CoreInputDevice device) noexcept
{
  return device == CoreInputDevice::pad3Button ||
         device == CoreInputDevice::pad6Button;
}

[[nodiscard]] constexpr bool validateCoreInputSettings(
  const CoreInputSettings& settings) noexcept
{
  bool disconnectedSeen = false;
  std::size_t connected = 0U;
  for (const auto device : settings.devices) {
    if (static_cast<unsigned>(device) >
        static_cast<unsigned>(CoreInputDevice::activator)) {
      return false;
    }
    if (device == CoreInputDevice::none) {
      disconnectedSeen = true;
      continue;
    }
    if (disconnectedSeen) {
      return false;
    }
    ++connected;
  }

  if (connected > 2U) {
    for (std::size_t index = 0U; index < connected; ++index) {
      if (!isCorePad(settings.devices[index])) {
        return false;
      }
    }
  }
  if (connected > 1U &&
      (settings.devices[0] == CoreInputDevice::pico ||
       settings.devices[0] == CoreInputDevice::terebiOekaki)) {
    return false;
  }
  return true;
}

} // namespace genplusgx
