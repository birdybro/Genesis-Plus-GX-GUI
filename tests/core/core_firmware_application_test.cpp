#include "genplusgx/core_adapter.h"
#include "synthetic_rom.h"

#include "desktop_core_host.h"
extern "C" {
#include "shared.h"
}

#include <iostream>
#include <string>

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
  const genplusgx::test::TemporaryFixture genesisRom{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  const genplusgx::test::TemporaryFixture smsRom{
    genplusgx::test::makeZ80RamMarkerRom(), ".sms"};
  const genplusgx::test::TemporaryFixture ggRom{
    genplusgx::test::makeZ80RamMarkerRom(), ".gg"};
  const genplusgx::test::TemporaryFixture genesisBios{
    genplusgx::test::makeGenesisBootRom(), ".bin"};
  const genplusgx::test::TemporaryFixture smsUsaBios{
    genplusgx::test::makeZ80BootRom(), ".sms"};
  const genplusgx::test::TemporaryFixture smsEuropeBios{
    genplusgx::test::makeZ80BootRom(), ".sms"};
  const genplusgx::test::TemporaryFixture smsJapanBios{
    genplusgx::test::makeZ80BootRom(), ".sms"};
  const genplusgx::test::TemporaryFixture gameGearBios{
    genplusgx::test::makeZ80BootRom(), ".gg"};

  genplusgx::CoreFirmwareSettings firmware{
    .genesis = genesisBios.path(),
    .masterSystemUsa = smsUsaBios.path(),
    .masterSystemEurope = smsEuropeBios.path(),
    .masterSystemJapan = smsJapanBios.path(),
    .gameGear = gameGearBios.path(),
    .segaCdUsa = {},
    .segaCdEurope = {},
    .segaCdJapan = {},
  };
  genplusgx::CoreAdapter adapter;
  if (!check(adapter.initialize(), "Firmware adapter initialization failed") ||
      !check(adapter.applyFirmwareSettings(firmware),
        "Complete cartridge firmware settings were rejected") ||
      !check(adapter.loadGame(genesisRom.path()),
        "Genesis ROM failed with configured boot firmware") ||
      !check(config.bios == 3U && (system_bios & SYSTEM_MD) != 0U,
        "Genesis boot firmware was not activated in the core") ||
      !check(std::string{MD_BIOS} == genesisBios.path().string(),
        "Genesis boot firmware path did not cross the core host boundary")) {
    return 1;
  }

  if (!check(adapter.unloadGame(), "Genesis firmware session unload failed") ||
      !check(adapter.loadGame(smsRom.path()),
        "Master System ROM failed with configured boot firmware") ||
      !check((system_bios & SYSTEM_SMS) != 0U,
        "Master System boot firmware was not loaded by the core") ||
      !check(std::string{MS_BIOS_US} == smsUsaBios.path().string() &&
             std::string{MS_BIOS_EU} == smsEuropeBios.path().string() &&
             std::string{MS_BIOS_JP} == smsJapanBios.path().string(),
        "Regional Master System firmware paths were not propagated")) {
    return 2;
  }

  if (!check(adapter.unloadGame(), "Master System firmware session unload failed") ||
      !check(adapter.loadGame(ggRom.path()),
        "Game Gear ROM failed with configured boot firmware") ||
      !check((system_bios & SYSTEM_GG) != 0U,
        "Game Gear boot firmware was not loaded by the core") ||
      !check(std::string{GG_BIOS} == gameGearBios.path().string(),
        "Game Gear firmware path was not propagated")) {
    return 3;
  }

  genplusgx::CoreFirmwareSettings exposed;
  if (!check(adapter.firmwareSettings(exposed) && exposed == firmware,
        "The complete firmware snapshot was not retained") ||
      !check(adapter.unloadGame(), "Game Gear firmware session unload failed") ||
      !check(adapter.applyFirmwareSettings({}),
        "Clearing firmware settings failed") ||
      !check(adapter.loadGame(genesisRom.path()),
        "Genesis ROM failed after clearing firmware") ||
      !check(config.bios == 0U && system_bios == 0U,
        "Cleared firmware settings left cartridge firmware active")) {
    return 4;
  }

  return check(adapter.shutdown(), "Firmware adapter shutdown failed") ? 0 : 5;
}
