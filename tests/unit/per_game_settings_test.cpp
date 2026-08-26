#include "genplusgx/settings/per_game_settings.h"

#include <QTemporaryDir>

#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>

namespace {

bool check(bool condition, std::string_view message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

genplusgx::GameIdentity identity(char hashDigit, std::string slug)
{
  return {
    .sha256 = std::string(64U, hashDigit),
    .titleSlug = std::move(slug),
  };
}

} // namespace

int main()
{
  using namespace genplusgx;
  using namespace genplusgx::settings;

  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "Temporary directory could not be created")) {
    return 1;
  }
#if defined(_WIN32)
  const auto temporaryPath = std::filesystem::path{temporary.path().toStdWString()};
#else
  const auto temporaryPath = std::filesystem::path{temporary.path().toStdString()};
#endif

  GlobalGameSettings global;
  global.video.aspect = video::AspectMode::native;
  global.audio.masterVolumePercent = 80;
  global.system.region = CoreSystemRegion::ntscU;
  global.inputProfile = "Default";
  global.bios.setPath(
    platform::BiosSlot::segaCdUsa, temporaryPath / "global" / "us.bin");

  PerGameSettings overrides;
  auto overrideVideo = global.video;
  overrideVideo.aspect = video::AspectMode::fourThree;
  overrides.video = overrideVideo;
  auto overrideAudio = global.audio;
  overrideAudio.masterVolumePercent = 35;
  overrideAudio.latencyMilliseconds = 20;
  overrideAudio.outputDeviceName = "Ignored per-game device";
  overrideAudio.core.fmLevelPercent = 125;
  overrides.audio = overrideAudio;
  auto overrideSystem = global.system;
  overrideSystem.region = CoreSystemRegion::palEurope;
  overrides.system = overrideSystem;
  overrides.inputProfile = "Arcade Stick";
  auto overrideBios = global.bios;
  overrideBios.setPath(
    platform::BiosSlot::segaCdUsa, temporaryPath / "games" / "us.bin");
  overrides.bios = overrideBios;

  const auto effective = resolvePerGameSettings(global, overrides);
  auto requestedAudio = effective.audio;
  requestedAudio.masterVolumePercent = 55;
  requestedAudio.latencyMilliseconds = 135;
  requestedAudio.outputDeviceName = "New global output";
  requestedAudio.core.psgLevelPercent = 140;
  const auto layeredAudio = planAudioSettingsLayerUpdate(
    global.audio, overrides.audio, requestedAudio);
  const auto globalOnlyAudio = planAudioSettingsLayerUpdate(
    global.audio, std::nullopt, requestedAudio);
  if (!check(
        effective.video == overrideVideo, "Video override did not win precedence") ||
      !check(
        effective.audio.masterVolumePercent == 35 &&
          effective.audio.core.fmLevelPercent == 125 &&
          effective.audio.latencyMilliseconds == global.audio.latencyMilliseconds &&
          effective.audio.outputDeviceName == global.audio.outputDeviceName,
        "Per-game audio precedence was resolved incorrectly") ||
      !check(
        effective.system == overrideSystem, "System override did not win precedence") ||
      !check(effective.inputProfile == "Arcade Stick",
        "Input profile override did not win precedence") ||
      !check(effective.bios == overrideBios, "BIOS override did not win precedence") ||
      !check(
        validatePerGameSettings(overrides), "Valid sparse overrides were rejected")) {
    return 1;
  }
  if (!check(layeredAudio.global.latencyMilliseconds == 135 &&
        layeredAudio.global.outputDeviceName == "New global output" &&
        layeredAudio.global.masterVolumePercent == global.audio.masterVolumePercent &&
        layeredAudio.perGame &&
        layeredAudio.perGame->masterVolumePercent == 55 &&
        layeredAudio.perGame->core.psgLevelPercent == 140 &&
        layeredAudio.perGame->latencyMilliseconds ==
          overrides.audio->latencyMilliseconds &&
        layeredAudio.perGame->outputDeviceName ==
          overrides.audio->outputDeviceName,
      "Audio layer update did not isolate global host resources") ||
      !check(globalOnlyAudio.global == requestedAudio &&
          !globalOnlyAudio.perGame,
        "Global audio update unexpectedly created a per-game layer")) {
    return 1;
  }

  auto invalid = overrides;
  invalid.video->aspect = static_cast<video::AspectMode>(99);
  if (!check(!validatePerGameSettings(invalid),
        "Invalid nested video settings were accepted")) {
    return 2;
  }
  invalid = overrides;
  invalid.inputProfile = std::string{"bad\nprofile"};
  if (!check(!validatePerGameSettings(invalid),
        "Control characters in an input profile were accepted")) {
    return 3;
  }
  invalid = overrides;
  invalid.bios->setPath(platform::BiosSlot::segaCdEurope, "relative.bin");
  if (!check(!validatePerGameSettings(invalid),
        "A relative per-game BIOS path was accepted")) {
    return 4;
  }

  const auto root = temporaryPath / "per-game";
  PerGameSettingsStore store{root};
  const auto firstIdentity = identity('a', "first-game");
  const auto secondIdentity = identity('b', "second-game");
  const auto missing = store.load(firstIdentity);
  if (!check(missing.status && !missing.exists && missing.settings.empty(),
        "A missing override did not resolve to an empty success") ||
      !check(store.save(firstIdentity, overrides),
        "Valid per-game settings could not be saved") ||
      !check(std::filesystem::is_regular_file(store.pathFor(firstIdentity)),
        "The override file was not created")) {
    return 6;
  }
  const auto loaded = store.load(firstIdentity);
  if (!check(loaded.status && loaded.exists && loaded.settings == overrides,
        "Per-game settings did not round-trip exactly")) {
    return 7;
  }

  const auto currentData = readFileBounded(
    store.pathFor(firstIdentity), PerGameSettingsStore::maximumFileBytes);
  std::string futureJson{
    reinterpret_cast<const char*>(currentData.data.data()), currentData.data.size()};
  const auto schema = futureJson.find("\"schemaVersion\": 1");
  if (!check(currentData.status && schema != std::string::npos,
        "Could not prepare a future-schema settings fixture")) {
    return 8;
  }
  futureJson.replace(
    schema, std::string{"\"schemaVersion\": 1"}.size(), "\"schemaVersion\": 99");
  const auto futureStatus = writeFileAtomically(store.pathFor(firstIdentity),
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(futureJson.data()), futureJson.size()},
    PerGameSettingsStore::maximumFileBytes);
  if (!check(futureStatus && !store.load(firstIdentity).status,
        "A future per-game settings schema was accepted") ||
      !check(store.save(firstIdentity, overrides),
        "Could not replace a future-schema settings file")) {
    return 8;
  }

  std::filesystem::copy_file(store.pathFor(firstIdentity),
    store.pathFor(secondIdentity),
    std::filesystem::copy_options::overwrite_existing);
  if (!check(!store.load(secondIdentity).status,
        "A per-game file with a mismatched embedded identity was accepted")) {
    return 9;
  }

  constexpr std::string_view corrupt = "{not-json";
  const auto corruptStatus = writeFileAtomically(store.pathFor(firstIdentity),
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(corrupt.data()), corrupt.size()},
    PerGameSettingsStore::maximumFileBytes);
  if (!check(corruptStatus && !store.load(firstIdentity).status,
        "Corrupt per-game JSON was accepted")) {
    return 10;
  }

  if (!check(store.save(firstIdentity, overrides),
        "Could not replace corrupt settings atomically") ||
      !check(store.save(firstIdentity, PerGameSettings{}),
        "An empty override could not be cleared") ||
      !check(!std::filesystem::exists(store.pathFor(firstIdentity)),
        "Clearing the final override left an unnecessary file") ||
      !check(!store.load(firstIdentity).exists,
        "A cleared override still appeared to exist")) {
    return 11;
  }

  PerGameSettingsStore relativeStore{"relative-root"};
  if (!check(!relativeStore.save(firstIdentity, overrides) &&
               relativeStore.pathFor(firstIdentity).empty(),
        "A relative per-game settings root was accepted")) {
    return 12;
  }

  return 0;
}
