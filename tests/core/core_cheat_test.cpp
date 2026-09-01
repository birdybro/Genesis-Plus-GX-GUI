#include "genplusgx/cheats/cheat_manager.h"
#include "genplusgx/core_adapter.h"
#include "genplusgx/debug_analysis.h"
#include "synthetic_rom.h"

extern "C" {
#include "shared.h"
}

#include <array>
#include <algorithm>
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
  const genplusgx::test::TemporaryFixture masterSystemFixture{
    genplusgx::test::makeZ80RamMarkerRom(), ".sms"};
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

  constexpr std::uint32_t searchedOffset = 0x20U;
  genplusgx::CoreDebugRequest writeSearchValue;
  writeSearchValue.type = genplusgx::CoreDebugRequestType::writeMemory;
  writeSearchValue.region = genplusgx::CoreDebugMemoryRegion::m68kRam;
  writeSearchValue.offset = searchedOffset;
  writeSearchValue.bytes = {0xCAU, 0xFEU};
  genplusgx::CoreDebugRequest capture;
  capture.type = genplusgx::CoreDebugRequestType::captureSnapshot;
  genplusgx::CoreDebugResponse debugResponse;
  genplusgx::DebugRamSearch search;
  if (!check(adapter.debugRequest(writeSearchValue, debugResponse),
        "Search fixture RAM could not be written") ||
      !check(adapter.debugRequest(capture, debugResponse) && debugResponse.snapshot,
        "Search fixture snapshot could not be captured") ||
      !check(search.begin(debugResponse.snapshot->m68kRam,
          genplusgx::DebugValueWidth::word,
          genplusgx::DebugValueEndian::big) &&
          search.filter(debugResponse.snapshot->m68kRam,
            genplusgx::DebugRamComparison::equalTo,
            genplusgx::DebugValueFormat::unsignedInteger,
            0xCAFE),
        "Memory-backed cheat search could not locate the fixture value")) {
    return 5;
  }
  const auto found = std::ranges::find_if(search.candidates(), [](const auto& candidate) {
    return candidate.offset == searchedOffset;
  });
  const auto generated = genplusgx::cheats::makeRamCheatCode(
    genplusgx::cheats::CheatSystem::genesis, searchedOffset, 0xCAFEU);
  genplusgx::cheats::CheatConfiguration searchedConfiguration{
    .entries = {{.name = "Searched value",
      .code = generated.normalizedCode, .enabled = true}},
  };
  std::vector<genplusgx::CoreCheatPatch> searchedPatches;
  if (!check(found != search.candidates().end() && generated.status &&
          genplusgx::cheats::validateCheatConfiguration(
            genplusgx::cheats::CheatSystem::genesis,
            searchedConfiguration,
            &searchedPatches) && searchedPatches.size() == 1U,
        "A live search result did not cross the typed cheat parser")) {
    return 6;
  }
  writeSearchValue.bytes = {0U, 0U};
  if (!check(adapter.debugRequest(writeSearchValue, debugResponse),
        "Search fixture RAM could not be cleared") ||
      !check(adapter.applyCheats(searchedPatches),
        "The searched cheat patch was rejected") ||
      !check(adapter.runFrame(true), "The searched cheat frame failed") ||
      !check(adapter.debugRequest(capture, debugResponse) && debugResponse.snapshot &&
          debugResponse.snapshot->m68kRam[searchedOffset] == 0xCAU &&
          debugResponse.snapshot->m68kRam[searchedOffset + 1U] == 0xFEU,
        "The searched cheat did not restore the captured RAM value") ||
      !check(adapter.applyCheats({}), "The searched cheat could not be cleared")) {
    return 7;
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
    return 8;
  }

  constexpr std::uint32_t eightBitOffset = 0x123U;
  constexpr std::uint8_t eightBitValue = 0x7DU;
  if (!check(adapter.unloadGame(), "Genesis cheat fixture could not unload") ||
      !check(adapter.loadGame(masterSystemFixture.path()),
        "8-bit cheat fixture could not load")) {
    return 9;
  }
  genplusgx::CoreDebugRequest writeEightBit;
  writeEightBit.type = genplusgx::CoreDebugRequestType::writeMemory;
  writeEightBit.region = genplusgx::CoreDebugMemoryRegion::z80Ram;
  writeEightBit.offset = eightBitOffset;
  writeEightBit.bytes = {eightBitValue};
  if (!check(adapter.debugRequest(writeEightBit, debugResponse),
        "8-bit search fixture RAM could not be written") ||
      !check(adapter.debugRequest(capture, debugResponse) && debugResponse.snapshot &&
          !debugResponse.snapshot->m68kActive,
        "8-bit search fixture snapshot could not be captured")) {
    return 10;
  }
  genplusgx::DebugRamSearch eightBitSearch;
  const auto generatedEightBit = genplusgx::cheats::makeRamCheatCode(
    genplusgx::cheats::CheatSystem::masterSystem,
    eightBitOffset,
    eightBitValue);
  std::vector<genplusgx::CoreCheatPatch> eightBitPatches;
  const genplusgx::cheats::CheatConfiguration eightBitConfiguration{
    .entries = {{.name = "Searched 8-bit value",
      .code = generatedEightBit.normalizedCode, .enabled = true}},
  };
  if (!check(eightBitSearch.begin(debugResponse.snapshot->z80Ram,
          genplusgx::DebugValueWidth::byte,
          genplusgx::DebugValueEndian::little) &&
          eightBitSearch.filter(debugResponse.snapshot->z80Ram,
            genplusgx::DebugRamComparison::equalTo,
            genplusgx::DebugValueFormat::unsignedInteger,
            eightBitValue),
        "8-bit memory-backed search could not filter the fixture value") ||
      !check(std::ranges::any_of(eightBitSearch.candidates(), [](const auto& candidate) {
          return candidate.offset == eightBitOffset;
        }), "8-bit memory-backed search did not locate the fixture value") ||
      !check(generatedEightBit.status &&
          generatedEightBit.normalizedCode == "C123:7D" &&
          genplusgx::cheats::validateCheatConfiguration(
            genplusgx::cheats::CheatSystem::masterSystem,
            eightBitConfiguration,
            &eightBitPatches) && eightBitPatches.size() == 1U,
        "An 8-bit live search result did not cross the typed cheat parser")) {
    return 11;
  }
  writeEightBit.bytes = {0U};
  if (!check(adapter.debugRequest(writeEightBit, debugResponse),
        "8-bit search fixture RAM could not be cleared") ||
      !check(adapter.applyCheats(eightBitPatches),
        "The searched 8-bit cheat patch was rejected") ||
      !check(adapter.runFrame(true), "The searched 8-bit cheat frame failed") ||
      !check(adapter.debugRequest(capture, debugResponse) && debugResponse.snapshot &&
          debugResponse.snapshot->z80Ram[eightBitOffset] == eightBitValue,
        "The searched 8-bit cheat did not restore the captured RAM value") ||
      !check(adapter.applyCheats({}),
        "The searched 8-bit cheat could not be cleared")) {
    return 12;
  }
  return adapter.shutdown() ? 0 : 13;
}
