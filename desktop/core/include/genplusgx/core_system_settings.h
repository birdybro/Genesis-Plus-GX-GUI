#pragma once

#include <cstdint>

namespace genplusgx {

enum class CoreSystemHardware : std::uint8_t {
  automatic,
  sg1000,
  sg1000II,
  sg1000IIRamExtension,
  markIII,
  masterSystem,
  masterSystemII,
  gameGear,
  genesis,
};

enum class CoreSystemRegion : std::uint8_t {
  automatic,
  ntscU,
  palEurope,
  ntscJapan,
  palJapan,
};

enum class CoreVideoStandard : std::uint8_t { automatic, ntsc, pal };
enum class CoreMasterClock : std::uint8_t { automatic, ntsc, pal };

struct CoreSystemSettings final {
  CoreSystemHardware hardware{CoreSystemHardware::automatic};
  CoreSystemRegion region{CoreSystemRegion::automatic};
  CoreVideoStandard videoStandard{CoreVideoStandard::automatic};
  CoreMasterClock masterClock{CoreMasterClock::automatic};
  bool emulateIllegalAccessLockups{true};
  bool enableAddressErrors{true};

  [[nodiscard]] bool operator==(const CoreSystemSettings&) const = default;
};

[[nodiscard]] constexpr bool validateCoreSystemSettings(
  const CoreSystemSettings& settings) noexcept
{
  return static_cast<unsigned>(settings.hardware) <=
      static_cast<unsigned>(CoreSystemHardware::genesis) &&
    static_cast<unsigned>(settings.region) <=
      static_cast<unsigned>(CoreSystemRegion::palJapan) &&
    static_cast<unsigned>(settings.videoStandard) <=
      static_cast<unsigned>(CoreVideoStandard::pal) &&
    static_cast<unsigned>(settings.masterClock) <=
      static_cast<unsigned>(CoreMasterClock::pal);
}

} // namespace genplusgx
