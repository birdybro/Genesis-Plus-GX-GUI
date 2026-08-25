#include "genplusgx/emulation_worker.h"
#include "genplusgx/settings/per_game_settings.h"
#include "synthetic_rom.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string_view>

namespace {

using namespace std::chrono_literals;

bool check(bool condition, std::string_view message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::optional<genplusgx::EmulationEvent> waitForOperation(
  genplusgx::EmulationWorker& worker, std::uint64_t operationId)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = worker.waitForEvent(50ms);
    if (event && event->operationId == operationId) {
      return event;
    }
  }
  return std::nullopt;
}

} // namespace

int main()
{
  using namespace genplusgx;
  using namespace genplusgx::settings;
  const test::TemporaryFixture fixture{test::makeGenesisRamMarkerRom(), ".bin"};

  GlobalGameSettings global;
  global.inputProfile = "Default";
  global.system.region = CoreSystemRegion::ntscU;
  global.system.videoStandard = CoreVideoStandard::ntsc;
  global.system.masterClock = CoreMasterClock::ntsc;
  PerGameSettings overrides;
  auto pal = global.system;
  pal.region = CoreSystemRegion::palEurope;
  pal.videoStandard = CoreVideoStandard::pal;
  pal.masterClock = CoreMasterClock::pal;
  overrides.system = pal;
  auto video = global.video;
  video.core.overscan = CoreOverscanMode::full;
  overrides.video = video;
  const auto effective = resolvePerGameSettings(global, overrides);

  EmulationWorker worker;
  if (!check(worker.start(), "Per-game workflow worker did not start") ||
      !check(worker.waitForEvent(2s).has_value(),
        "Per-game workflow start event was missing") ||
      !check(
        worker.submit(EmulationCommand::updateSystemSettings(1U, effective.system)),
        "PAL system override was not queued") ||
      !check(
        worker.submit(EmulationCommand::updateVideoSettings(2U, effective.video.core)),
        "Video override was not queued") ||
      !check(
        worker.submit(EmulationCommand::updateAudioSettings(3U, effective.audio.core)),
        "Inherited audio settings were not queued") ||
      !check(worker.submit(EmulationCommand::load(4U, fixture.path())),
        "Configured load was not queued")) {
    return 1;
  }
  const auto palLoaded = waitForOperation(worker, 4U);
  if (!check(palLoaded && palLoaded->succeeded(), "Configured per-game load failed") ||
      !check(worker.metrics().targetFramesPerSecond > 49.70 &&
               worker.metrics().targetFramesPerSecond < 49.71,
        "The system override was not applied before the load command")) {
    return 2;
  }

  if (!check(
        worker.submit(EmulationCommand::simple(EmulationCommandType::unloadGame, 5U)),
        "Unload was not queued")) {
    return 3;
  }
  const auto unloaded = waitForOperation(worker, 5U);
  if (!check(unloaded && unloaded->succeeded(), "Configured game did not unload")) {
    return 3;
  }
  const auto inherited = resolvePerGameSettings(global, {});
  if (!check(
        worker.submit(EmulationCommand::updateSystemSettings(6U, inherited.system)),
        "Global system settings were not queued") ||
      !check(worker.submit(EmulationCommand::load(7U, fixture.path())),
        "Inherited load was not queued")) {
    return 4;
  }
  const auto ntscLoaded = waitForOperation(worker, 7U);
  if (!check(ntscLoaded && ntscLoaded->succeeded(), "Inherited global load failed") ||
      !check(worker.metrics().targetFramesPerSecond > 59.92 &&
               worker.metrics().targetFramesPerSecond < 59.93,
        "Clearing the override did not restore global load-time settings") ||
      !check(worker.stop(), "Per-game workflow worker did not stop cleanly")) {
    return 5;
  }
  return 0;
}
