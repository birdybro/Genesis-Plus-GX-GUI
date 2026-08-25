#include "genplusgx/core_adapter.h"
#include "synthetic_rom.h"

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
    return 4;
  }
  genplusgx::CoreTimingInfo firstTiming;
  if (!check(adapter.timingInfo(firstTiming) && firstTiming.pal &&
        firstTiming.linesPerFrame == 313U &&
        firstTiming.masterClockHz == 53'693'175U,
      "Independent PAL VDP and NTSC master clock settings were not applied") ||
      !check(adapter.hardware() == 0x80U,
        "Automatic hardware selection changed the Genesis fixture")) {
    return 5;
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
    return 6;
  }
  genplusgx::CoreTimingInfo unchangedTiming;
  if (!check(adapter.timingInfo(unchangedTiming) &&
        unchangedTiming.masterClockHz == firstTiming.masterClockHz &&
        unchangedTiming.linesPerFrame == firstTiming.linesPerFrame &&
        adapter.hardware() == 0x80U,
      "Reload-required settings mutated the active machine")) {
    return 7;
  }

  if (!check(adapter.unloadGame() && adapter.loadGame(fixture.path()),
        "Deferred system settings reload failed")) {
    return 8;
  }
  genplusgx::CoreTimingInfo secondTiming;
  if (!check(adapter.hardware() == 0x01U,
        "Forced SG-1000 hardware was not selected on reload") ||
      !check(adapter.timingInfo(secondTiming) && !secondTiming.pal &&
        secondTiming.linesPerFrame == 262U &&
        secondTiming.masterClockHz == 53'203'424U,
        "Forced NTSC VDP and PAL master clock were not independently applied")) {
    return 9;
  }

  if (!check(adapter.unloadGame() &&
        adapter.applySystemSettings(genplusgx::CoreSystemSettings{}) &&
        adapter.loadGame(fixture.path()) && adapter.hardware() == 0x80U,
      "Restored automatic hardware did not detect Genesis on the next load")) {
    return 10;
  }
  return adapter.shutdown() ? 0 : 11;
}
