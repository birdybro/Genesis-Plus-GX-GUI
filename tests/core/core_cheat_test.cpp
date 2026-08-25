#include "genplusgx/core_adapter.h"
#include "synthetic_rom.h"

extern "C" {
#include "shared.h"
}

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::uint16_t hostWord(const std::uint8_t* data)
{
  std::uint16_t value = 0U;
  std::memcpy(&value, data, sizeof(value));
  return value;
}

} // namespace

int main()
{
  const genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".bin"};
  genplusgx::CoreAdapter adapter;
  if (!check(adapter.initialize(), "Cheat test could not initialize the adapter") ||
      !check(adapter.applyCheats({}).error == genplusgx::CoreError::noGameLoaded,
        "Cheats were accepted without a loaded game") ||
      !check(
        adapter.loadGame(fixture.path()), "Cheat test could not load the fixture")) {
    return 1;
  }

  const auto originalRomWord = hostWord(cart.rom + 0x300U);
  const std::array romPatch{
    genplusgx::CoreCheatPatch{
      .address = 0x300U,
      .data = 0x4e71U,
      .width = genplusgx::CoreCheatWidth::word,
    },
  };
  if (!check(adapter.applyCheats(romPatch), "ROM patch was rejected") ||
      !check(hostWord(cart.rom + 0x300U) == 0x4e71U,
        "ROM patch was not applied immediately") ||
      !check(adapter.applyCheats({}), "ROM patch clear was rejected") ||
      !check(hostWord(cart.rom + 0x300U) == originalRomWord,
        "ROM patch did not restore the original word")) {
    return 2;
  }

  constexpr std::size_t ramOffset = 0x10U;
  std::memset(work_ram + ramOffset, 0, sizeof(std::uint16_t));
  const std::array ramPatch{
    genplusgx::CoreCheatPatch{
      .address = 0xff0010U,
      .data = 0x1234U,
      .width = genplusgx::CoreCheatWidth::word,
    },
  };
  if (!check(adapter.applyCheats(ramPatch), "RAM patch was rejected") ||
      !check(adapter.runFrame(true), "Cheat frame execution failed") ||
      !check(hostWord(work_ram + ramOffset) == 0x1234U,
        "RAM patch was not applied at the frame boundary") ||
      !check(adapter.applyCheats({}), "RAM patch clear was rejected")) {
    return 3;
  }
  std::memset(work_ram + ramOffset, 0, sizeof(std::uint16_t));
  if (!check(adapter.runFrame(true), "Post-clear frame execution failed") ||
      !check(hostWord(work_ram + ramOffset) == 0U,
        "A cleared RAM patch continued mutating core memory")) {
    return 4;
  }

  std::vector<genplusgx::CoreCheatPatch> tooMany(
    genplusgx::maximumCoreCheatPatches + 1U, ramPatch.front());
  const std::array invalidPatch{
    genplusgx::CoreCheatPatch{
      .address = 0x100U,
      .data = 1U,
      .reference = 1U,
      .width = genplusgx::CoreCheatWidth::word,
      .referenceRequired = true,
    },
  };
  if (!check(
        adapter.applyCheats(tooMany).error == genplusgx::CoreError::invalidCheats &&
          adapter.applyCheats(invalidPatch).error ==
            genplusgx::CoreError::invalidCheats,
        "Invalid cheat patch sets crossed the typed core boundary")) {
    return 5;
  }
  return adapter.shutdown() ? 0 : 6;
}
