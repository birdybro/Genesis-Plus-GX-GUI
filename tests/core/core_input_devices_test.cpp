#include "genplusgx/core_adapter.h"
#include "synthetic_rom.h"

extern "C" {
#include "shared.h"
}

#include <iostream>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

genplusgx::CoreInputSettings oneDevice(genplusgx::CoreInputDevice device)
{
  genplusgx::CoreInputSettings settings;
  settings.devices.fill(genplusgx::CoreInputDevice::none);
  settings.devices[0] = device;
  return settings;
}

} // namespace

int main()
{
  const genplusgx::test::TemporaryFixture genesis{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  const genplusgx::test::TemporaryFixture masterSystem{
    genplusgx::test::makeZ80RamMarkerRom(), ".sms"};
  genplusgx::CoreAdapter adapter;
  if (!check(adapter.initialize(), "Input-device adapter initialization failed")) {
    return 1;
  }

  auto invalidGap = oneDevice(genplusgx::CoreInputDevice::pad6Button);
  invalidGap.devices[2] = genplusgx::CoreInputDevice::pad6Button;
  auto invalidMultitap = oneDevice(genplusgx::CoreInputDevice::segaMouse);
  invalidMultitap.devices[1] = genplusgx::CoreInputDevice::pad6Button;
  invalidMultitap.devices[2] = genplusgx::CoreInputDevice::pad6Button;
  if (!check(adapter.applyInputSettings(invalidGap).error ==
          genplusgx::CoreError::invalidSettings,
        "A gapped logical-device layout was accepted") ||
      !check(adapter.applyInputSettings(invalidMultitap).error ==
          genplusgx::CoreError::invalidSettings,
        "A non-pad multitap layout was accepted") ||
      !check(adapter.loadGame(genesis.path()),
        "Genesis fixture failed with default input devices") ||
      !check(input.system[0] == SYSTEM_GAMEPAD &&
             input.system[1] == SYSTEM_GAMEPAD &&
             input.dev[0] == DEVICE_PAD6B && input.dev[4] == DEVICE_PAD6B,
        "Default six-button devices were not applied to both Genesis ports")) {
    return 2;
  }

  genplusgx::CoreInputSettings mixed;
  mixed.devices.fill(genplusgx::CoreInputDevice::none);
  mixed.devices[0] = genplusgx::CoreInputDevice::pad3Button;
  mixed.devices[1] = genplusgx::CoreInputDevice::segaMouse;
  if (!check(adapter.applyInputSettings(mixed),
        "Live mixed-device update failed") ||
      !check(input.system[0] == SYSTEM_GAMEPAD &&
             input.system[1] == SYSTEM_MOUSE &&
             input.dev[0] == DEVICE_PAD3B && input.dev[4] == DEVICE_MOUSE,
        "Live pad/mouse configuration did not update core ports")) {
    return 3;
  }

  const auto lightGun = oneDevice(genplusgx::CoreInputDevice::lightGun);
  genplusgx::InputSnapshot snapshot;
  snapshot.sequence = 7U;
  snapshot.players[0].connected = true;
  snapshot.players[0].buttons = genplusgx::buttonMask(genplusgx::InputButton::b);
  if (!check(adapter.applyInputSettings(lightGun),
        "Genesis light-gun update failed") ||
      !check(input.system[0] == NO_SYSTEM && input.system[1] == SYSTEM_MENACER &&
             input.dev[4] == DEVICE_LIGHTGUN,
        "Genesis light gun was not assigned to the supported port B") ||
      !check(adapter.setInputSnapshot(snapshot) && adapter.runFrame(true),
        "Light-gun input snapshot failed") ||
      !check((input.pad[4] & INPUT_B) != 0U &&
             adapter.appliedInputSequence() == snapshot.sequence,
        "Logical player one did not map into the port-B light-gun slot")) {
    return 4;
  }

  genplusgx::CoreInputSettings threePads;
  threePads.devices.fill(genplusgx::CoreInputDevice::none);
  threePads.devices[0] = genplusgx::CoreInputDevice::pad3Button;
  threePads.devices[1] = genplusgx::CoreInputDevice::pad6Button;
  threePads.devices[2] = genplusgx::CoreInputDevice::pad3Button;
  if (!check(adapter.applyInputSettings(threePads),
        "Genesis Team Player update failed") ||
      !check(input.system[0] == SYSTEM_TEAMPLAYER &&
             input.system[1] == NO_SYSTEM &&
             input.dev[0] == DEVICE_PAD3B &&
             input.dev[1] == DEVICE_PAD6B &&
             input.dev[2] == DEVICE_PAD3B && input.dev[3] == NO_DEVICE,
        "Three-pad layout did not configure a bounded Team Player")) {
    return 5;
  }

  genplusgx::CoreInputSettings eightPads;
  eightPads.devices.fill(genplusgx::CoreInputDevice::pad6Button);
  if (!check(adapter.unloadGame(), "Genesis input-device unload failed") ||
      !check(adapter.applyInputSettings(eightPads),
        "Eight-pad settings were rejected") ||
      !check(adapter.loadGame(masterSystem.path()),
        "Master System fixture failed with multitaps") ||
      !check(input.system[0] == SYSTEM_MASTERTAP &&
             input.system[1] == SYSTEM_MASTERTAP,
        "Eight-pad Master System layout did not configure both Master Taps")) {
    return 6;
  }
  for (std::size_t slot = 0U; slot < MAX_DEVICES; ++slot) {
    if (!check(input.dev[slot] == DEVICE_PAD2B,
          "Master Tap did not expose an eight-bit pad in every configured slot")) {
      return 7;
    }
  }

  if (!check(adapter.applyInputSettings(
        oneDevice(genplusgx::CoreInputDevice::pico)) &&
        input.dev[0] == DEVICE_PICO,
        "Pico tablet selection did not reach the core device slot") ||
      !check(adapter.applyInputSettings(
        oneDevice(genplusgx::CoreInputDevice::terebiOekaki)) &&
        input.dev[0] == DEVICE_TEREBI,
        "Terebi Oekaki selection did not reach the core device slot")) {
    return 8;
  }

  genplusgx::CoreInputSettings exposed;
  if (!check(adapter.inputSettings(exposed) &&
             exposed == oneDevice(genplusgx::CoreInputDevice::terebiOekaki),
        "The active emulated-device layout was not exposed")) {
    return 9;
  }
  return check(adapter.shutdown(), "Input-device adapter shutdown failed") ? 0 : 10;
}
