#include "genplusgx/core_adapter.h"
#include "synthetic_rom.h"

extern "C" {
#include "shared.h"
}

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

struct SystemCase final {
  std::string_view extension;
  genplusgx::CoreSystemHardware forcedHardware;
  std::uint8_t expectedHardware;
  std::uint16_t expectedWidth;
  std::uint16_t expectedHeight;
};

} // namespace

int main()
{
  constexpr std::uint8_t marker = 0x5AU;
  const genplusgx::test::TemporaryFixture sms{
    genplusgx::test::makeZ80RamMarkerRom(marker), ".sms"};
  const genplusgx::test::TemporaryFixture gg{
    genplusgx::test::makeZ80RamMarkerRom(marker), ".gg"};
  const genplusgx::test::TemporaryFixture sg{
    genplusgx::test::makeZ80RamMarkerRom(marker), ".sg"};

  const std::array cases{
    SystemCase{".sg", genplusgx::CoreSystemHardware::automatic,
      SYSTEM_SG, 256U, 192U},
    SystemCase{".sg", genplusgx::CoreSystemHardware::sg1000,
      SYSTEM_SG, 256U, 192U},
    SystemCase{".sg", genplusgx::CoreSystemHardware::sg1000II,
      SYSTEM_SGII, 256U, 192U},
    SystemCase{".sg", genplusgx::CoreSystemHardware::sg1000IIRamExtension,
      SYSTEM_SGII_RAM_EXT, 256U, 192U},
    SystemCase{".sms", genplusgx::CoreSystemHardware::automatic,
      SYSTEM_SMS2, 256U, 192U},
    SystemCase{".sms", genplusgx::CoreSystemHardware::masterSystem,
      SYSTEM_SMS, 256U, 192U},
    SystemCase{".sms", genplusgx::CoreSystemHardware::masterSystemII,
      SYSTEM_SMS2, 256U, 192U},
    SystemCase{".gg", genplusgx::CoreSystemHardware::automatic,
      SYSTEM_GG, 160U, 144U},
    SystemCase{".sms", genplusgx::CoreSystemHardware::markIII,
      SYSTEM_MARKIII, 256U, 192U},
  };

  genplusgx::CoreAdapter adapter;
  if (!check(adapter.initialize(), "Eight-bit adapter initialization failed")) {
    return 1;
  }

  for (const auto& testCase : cases) {
    const auto& path = testCase.extension == ".sg"
      ? sg.path() : (testCase.extension == ".gg" ? gg.path() : sms.path());
    genplusgx::CoreSystemSettings settings;
    settings.hardware = testCase.forcedHardware;
    if (!check(adapter.applySystemSettings(settings),
          "Eight-bit system settings were rejected") ||
        !check(adapter.loadGame(path), "Generated Z80 ROM failed to load") ||
        !check(adapter.hardware() == testCase.expectedHardware,
          "Generated Z80 ROM selected the wrong hardware") ||
        !check(adapter.runFrame(false), "Generated Z80 frame execution failed") ||
        !check(work_ram[0] == marker,
          "Generated Z80 program did not write its semantic RAM marker")) {
      return 2;
    }

    genplusgx::CoreVideoFrameInfo frame;
    if (!check(adapter.videoFrameInfo(frame),
          "Eight-bit video geometry was unavailable") ||
        !check(frame.width == testCase.expectedWidth &&
               frame.height == testCase.expectedHeight,
          "Eight-bit viewport geometry was incorrect") ||
        !check(adapter.unloadGame(), "Eight-bit system unload failed")) {
      return 3;
    }
  }

  genplusgx::CoreVideoSettings extended;
  extended.gameGearExtendedScreen = true;
  if (!check(adapter.applySystemSettings(genplusgx::CoreSystemSettings{}),
        "Automatic hardware could not be restored for Game Gear coverage") ||
      !check(adapter.applyVideoSettings(extended),
        "Extended Game Gear viewport could not be enabled") ||
      !check(adapter.loadGame(gg.path()),
        "Game Gear fixture could not load with its extended viewport") ||
      !check(adapter.runFrame(false),
        "Extended Game Gear viewport did not render")) {
    return 4;
  }
  genplusgx::CoreVideoFrameInfo extendedFrame;
  if (!check(adapter.videoFrameInfo(extendedFrame) &&
        extendedFrame.width == 256U && extendedFrame.height == 192U,
      "Extended Game Gear viewport did not expose 256x192 output") ||
      !check(adapter.unloadGame(),
        "Extended Game Gear session did not unload") ||
      !check(adapter.applyVideoSettings(genplusgx::CoreVideoSettings{}),
        "Default Game Gear viewport could not be restored")) {
    return 4;
  }

  return check(adapter.shutdown(), "Eight-bit adapter shutdown failed") ? 0 : 5;
}
