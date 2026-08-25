#include "genplusgx/core_adapter.h"
#include "synthetic_rom.h"

extern "C" {
#include "shared.h"
}

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <thread>
#include <utility>

namespace {

using genplusgx::CoreAdapter;
using genplusgx::CoreError;
using genplusgx::CoreLifecycleState;

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::uint16_t hostWordAt(std::size_t offset)
{
  std::uint16_t value = 0;
  std::memcpy(&value, work_ram + offset, sizeof(value));
  return value;
}

bool markerWasWritten()
{
  return hostWordAt(0U) == 0x1357U && hostWordAt(2U) == 0x9BDFU &&
         hostWordAt(4U) == 0xCAFEU && work_ram[7U] == 0x42U;
}

} // namespace

int main()
{
  const genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".bin"};
  auto palRom = genplusgx::test::makeGenesisRamMarkerRom();
  palRom.at(0x1F0U) = static_cast<std::uint8_t>('E');
  const genplusgx::test::TemporaryFixture palFixture{std::move(palRom), ".bin"};
  const genplusgx::test::TemporaryFixture emptyFixture{{}, ".bin"};

  CoreAdapter adapter;
  genplusgx::CoreTimingInfo unloadedTiming;
  if (!check(adapter.state() == CoreLifecycleState::uninitialized,
        "A new adapter did not start uninitialized") ||
      !check(adapter.runFrame().error == CoreError::notInitialized,
        "Frame execution before initialization was not rejected") ||
      !check(adapter.shutdown(), "An uninitialized shutdown was not idempotent") ||
      !check(adapter.initialize(), "Core initialization failed") ||
      !check(adapter.initialize().error == CoreError::coreAlreadyOwned,
        "Repeated initialization was not rejected") ||
      !check(adapter.state() == CoreLifecycleState::ready,
        "Initialized adapter did not enter ready state") ||
      !check(adapter.reset().error == CoreError::noGameLoaded,
        "Reset without a loaded game was not rejected") ||
      !check(adapter.timingInfo(unloadedTiming).error == CoreError::noGameLoaded,
        "Timing metadata was exposed without a loaded game")) {
    return 1;
  }

  CoreAdapter competingAdapter;
  if (!check(competingAdapter.initialize().error == CoreError::coreAlreadyOwned,
        "A second adapter acquired the global core context") ||
      !check(adapter.loadGame(std::filesystem::path{"missing-fixture.bin"}).error ==
          CoreError::invalidPath,
        "A missing path did not fail validation") ||
      !check(adapter.loadGame(emptyFixture.path()).error == CoreError::loadFailed,
        "An empty image did not fail in the core loader") ||
      !check(adapter.state() == CoreLifecycleState::ready,
        "A failed load left stale lifecycle state")) {
    return 2;
  }

  genplusgx::CoreTimingInfo ntscTiming;
  if (!check(adapter.loadGame(fixture.path()), "Generated ROM failed to load through the adapter") ||
      !check(adapter.state() == CoreLifecycleState::loaded,
        "Successful load did not enter loaded state") ||
      !check(adapter.hardware() == SYSTEM_MD, "Loaded hardware metadata was incorrect") ||
      !check(adapter.loadedPath() == fixture.path(), "Loaded path was not retained") ||
      !check(adapter.frameCount() == 0U, "Fresh load had a nonzero frame count") ||
      !check(adapter.timingInfo(ntscTiming), "NTSC timing metadata was unavailable") ||
      !check(ntscTiming.masterClockHz == MCLOCK_NTSC &&
          ntscTiming.linesPerFrame == 262U &&
          ntscTiming.masterCyclesPerLine == MCYCLES_PER_LINE &&
          !ntscTiming.pal && !ntscTiming.segaCd &&
          ntscTiming.framesPerSecond() > 59.92 &&
          ntscTiming.framesPerSecond() < 59.93,
        "NTSC timing metadata differs from the authoritative core cadence") ||
      !check(adapter.runFrame(true), "Headless frame execution failed") ||
      !check(adapter.frameCount() == 1U, "Frame count did not advance") ||
      !check(markerWasWritten(), "Synthetic program did not run through the adapter")) {
    return 3;
  }

  if (!check(adapter.reset(), "Hard reset failed") ||
      !check(adapter.frameCount() == 0U, "Hard reset did not reset frame count") ||
      !check(!markerWasWritten(), "Hard reset did not clear synthetic work RAM") ||
      !check(adapter.runFrame(true), "Frame execution after reset failed") ||
      !check(markerWasWritten(), "Synthetic program did not restart after reset")) {
    return 4;
  }

  genplusgx::CoreResult crossThreadResult;
  genplusgx::CoreResult crossThreadFrameResult;
  std::thread wrongThread{[&adapter, &crossThreadResult, &crossThreadFrameResult] {
    crossThreadFrameResult = adapter.runFrame(true);
    crossThreadResult = adapter.shutdown();
  }};
  wrongThread.join();
  if (!check(crossThreadFrameResult.error == CoreError::wrongThread,
        "A non-owner thread reached frame execution") ||
      !check(crossThreadResult.error == CoreError::wrongThread,
        "A non-owner thread was allowed to shut down the emulator core") ||
      !check(adapter.state() == CoreLifecycleState::loaded,
        "Rejected cross-thread shutdown changed lifecycle state")) {
    return 5;
  }

  for (int iteration = 0; iteration < 25; ++iteration) {
    if (!check(adapter.loadGame(fixture.path()), "Repeated game load failed") ||
        !check(adapter.runFrame(true), "Repeated lifecycle frame failed") ||
        !check(adapter.unloadGame(), "Repeated game unload failed") ||
        !check(adapter.state() == CoreLifecycleState::ready,
          "Unload did not return adapter to ready state") ||
        !check(adapter.loadedPath().empty(), "Unload retained the previous path") ||
        !check(adapter.runFrame(true).error == CoreError::noGameLoaded,
          "Frame execution after unload was not rejected")) {
      return 6;
    }
  }

  genplusgx::CoreTimingInfo palTiming;
  if (!check(adapter.loadGame(palFixture.path()), "PAL fixture failed to load") ||
      !check(adapter.timingInfo(palTiming), "PAL timing metadata was unavailable") ||
      !check(palTiming.masterClockHz == MCLOCK_PAL &&
          palTiming.linesPerFrame == 313U &&
          palTiming.masterCyclesPerLine == MCYCLES_PER_LINE && palTiming.pal &&
          !palTiming.segaCd && palTiming.framesPerSecond() > 49.70 &&
          palTiming.framesPerSecond() < 49.71,
        "PAL timing metadata differs from the authoritative core cadence") ||
      !check(adapter.unloadGame(), "PAL fixture unload failed")) {
    return 7;
  }

  if (!check(adapter.shutdown(), "Core shutdown failed") ||
      !check(adapter.state() == CoreLifecycleState::uninitialized,
        "Shutdown did not release adapter state") ||
      !check(competingAdapter.initialize(), "Released core ownership could not be reacquired") ||
      !check(competingAdapter.shutdown(), "Second adapter shutdown failed")) {
    return 8;
  }

  {
    CoreAdapter scopedAdapter;
    if (!check(scopedAdapter.initialize(), "Scoped adapter initialization failed") ||
        !check(scopedAdapter.loadGame(fixture.path()), "Scoped adapter load failed")) {
      return 9;
    }
  }

  CoreAdapter afterDestructor;
  if (!check(afterDestructor.initialize(), "RAII destruction did not release core ownership") ||
      !check(afterDestructor.shutdown(), "Final adapter shutdown failed")) {
    return 10;
  }

  CoreAdapter invalidAudioRate{7'999};
  if (!check(invalidAudioRate.initialize().error == CoreError::audioInitializationFailed,
        "An unsupported audio rate was accepted") ||
      !check(invalidAudioRate.state() == CoreLifecycleState::uninitialized,
        "Rejected audio configuration acquired core ownership")) {
    return 11;
  }

  return 0;
}
