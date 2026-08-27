#include "genplusgx/core_adapter.h"
#include "synthetic_rom.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

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
  const genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".bin"};
  genplusgx::CoreAdapter adapter;
  if (!check(adapter.initialize(), "System settings adapter initialization failed")) {
    return 1;
  }
  genplusgx::CoreSystemSettings exposed;
  if (!check(adapter.systemSettings(exposed) &&
        exposed == genplusgx::CoreSystemSettings{},
      "System settings defaults were not exposed")) {
    return 2;
  }
  auto invalid = exposed;
  invalid.hardware = static_cast<genplusgx::CoreSystemHardware>(99);
  if (!check(adapter.applySystemSettings(invalid).error ==
        genplusgx::CoreError::invalidSettings &&
        adapter.systemSettings(exposed) &&
        exposed == genplusgx::CoreSystemSettings{},
      "Invalid system settings partially mutated the adapter")) {
    return 3;
  }

  std::size_t optionCases = 0U;
  const auto exerciseEnumeration = [&adapter, &optionCases](auto values, auto assign) {
    for (const auto value : values) {
      auto settings = genplusgx::CoreSystemSettings{};
      assign(settings, value);
      genplusgx::CoreSystemSettings retained;
      if (!adapter.applySystemSettings(settings) ||
          !adapter.systemSettings(retained) || retained != settings) {
        return false;
      }
      ++optionCases;
    }
    return true;
  };
  if (!check(exerciseEnumeration(
        std::array{genplusgx::CoreSystemHardware::automatic,
          genplusgx::CoreSystemHardware::sg1000,
          genplusgx::CoreSystemHardware::sg1000II,
          genplusgx::CoreSystemHardware::sg1000IIRamExtension,
          genplusgx::CoreSystemHardware::markIII,
          genplusgx::CoreSystemHardware::masterSystem,
          genplusgx::CoreSystemHardware::masterSystemII,
          genplusgx::CoreSystemHardware::gameGear,
          genplusgx::CoreSystemHardware::genesis},
        [](auto& settings, auto value) { settings.hardware = value; }),
      "A hardware override option was not retained") ||
      !check(exerciseEnumeration(
        std::array{genplusgx::CoreSystemRegion::automatic,
          genplusgx::CoreSystemRegion::ntscU,
          genplusgx::CoreSystemRegion::palEurope,
          genplusgx::CoreSystemRegion::ntscJapan,
          genplusgx::CoreSystemRegion::palJapan},
        [](auto& settings, auto value) { settings.region = value; }),
      "A region option was not retained") ||
      !check(exerciseEnumeration(
        std::array{genplusgx::CoreVideoStandard::automatic,
          genplusgx::CoreVideoStandard::ntsc,
          genplusgx::CoreVideoStandard::pal},
        [](auto& settings, auto value) { settings.videoStandard = value; }),
      "A VDP standard option was not retained") ||
      !check(exerciseEnumeration(
        std::array{genplusgx::CoreMasterClock::automatic,
          genplusgx::CoreMasterClock::ntsc,
          genplusgx::CoreMasterClock::pal},
        [](auto& settings, auto value) { settings.masterClock = value; }),
      "A master-clock option was not retained")) {
    return 4;
  }
  for (const bool enabled : {false, true}) {
    auto lockups = genplusgx::CoreSystemSettings{};
    lockups.emulateIllegalAccessLockups = enabled;
    auto addressErrors = genplusgx::CoreSystemSettings{};
    addressErrors.enableAddressErrors = enabled;
    genplusgx::CoreSystemSettings retained;
    if (!check(adapter.applySystemSettings(lockups) &&
          adapter.systemSettings(retained) && retained == lockups &&
          adapter.applySystemSettings(addressErrors) &&
          adapter.systemSettings(retained) && retained == addressErrors,
        "A system-accuracy toggle was not retained")) {
      return 4;
    }
    optionCases += 2U;
  }
  if (!check(optionCases == 24U,
        "The complete core system option inventory did not execute") ||
      !check(adapter.applySystemSettings(genplusgx::CoreSystemSettings{}),
        "System defaults could not be restored after the option matrix")) {
    return 4;
  }

  genplusgx::CoreSystemSettings palVideoNtscClock;
  palVideoNtscClock.region = genplusgx::CoreSystemRegion::palEurope;
  palVideoNtscClock.videoStandard = genplusgx::CoreVideoStandard::pal;
  palVideoNtscClock.masterClock = genplusgx::CoreMasterClock::ntsc;
  palVideoNtscClock.emulateIllegalAccessLockups = false;
  palVideoNtscClock.enableAddressErrors = false;
  if (!check(adapter.applySystemSettings(palVideoNtscClock),
        "Pre-load system settings were rejected") ||
      !check(adapter.loadGame(fixture.path()),
        "Configured Genesis fixture could not load")) {
    return 5;
  }
  genplusgx::CoreTimingInfo firstTiming;
  if (!check(adapter.timingInfo(firstTiming) && firstTiming.pal &&
        firstTiming.linesPerFrame == 313U &&
        firstTiming.masterClockHz == 53'693'175U,
      "Independent PAL VDP and NTSC master clock settings were not applied") ||
      !check(adapter.hardware() == 0x80U,
        "Automatic hardware selection changed the Genesis fixture")) {
    return 6;
  }

  genplusgx::CoreSystemSettings nextLoad;
  nextLoad.hardware = genplusgx::CoreSystemHardware::sg1000;
  nextLoad.region = genplusgx::CoreSystemRegion::ntscU;
  nextLoad.videoStandard = genplusgx::CoreVideoStandard::ntsc;
  nextLoad.masterClock = genplusgx::CoreMasterClock::pal;
  if (!check(adapter.applySystemSettings(nextLoad),
        "Loaded-session system settings were rejected") ||
      !check(adapter.systemSettings(exposed) && exposed == nextLoad,
        "Deferred system settings snapshot was not retained")) {
    return 7;
  }
  genplusgx::CoreTimingInfo unchangedTiming;
  if (!check(adapter.timingInfo(unchangedTiming) &&
        unchangedTiming.masterClockHz == firstTiming.masterClockHz &&
        unchangedTiming.linesPerFrame == firstTiming.linesPerFrame &&
        adapter.hardware() == 0x80U,
      "Reload-required settings mutated the active machine")) {
    return 8;
  }

  if (!check(adapter.unloadGame() && adapter.loadGame(fixture.path()),
        "Deferred system settings reload failed")) {
    return 9;
  }
  genplusgx::CoreTimingInfo secondTiming;
  if (!check(adapter.hardware() == 0x01U,
        "Forced SG-1000 hardware was not selected on reload") ||
      !check(adapter.timingInfo(secondTiming) && !secondTiming.pal &&
        secondTiming.linesPerFrame == 262U &&
        secondTiming.masterClockHz == 53'203'424U,
        "Forced NTSC VDP and PAL master clock were not independently applied")) {
    return 10;
  }

  if (!check(adapter.unloadGame() &&
        adapter.applySystemSettings(genplusgx::CoreSystemSettings{}) &&
        adapter.loadGame(fixture.path()) && adapter.hardware() == 0x80U,
      "Restored automatic hardware did not detect Genesis on the next load")) {
    return 11;
  }
  return adapter.shutdown() ? 0 : 12;
}
