#include "genplusgx/settings/audio_settings.h"

#include <QTemporaryDir>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

bool writeText(const std::filesystem::path& path, std::string_view text)
{
  return genplusgx::writeFileAtomically(path,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(text.data()), text.size()},
    genplusgx::settings::AudioSettingsStore::maximumFileBytes);
}

} // namespace

int main()
{
  QTemporaryDir directory;
  if (!check(directory.isValid(), "Temporary directory was unavailable")) {
    return 1;
  }
  genplusgx::settings::AudioSettingsStore store{
    std::filesystem::path{directory.path().toStdString()} / "audio-settings.json"};
  const auto missing = store.load();
  if (!check(missing.status && !missing.migrated &&
      missing.settings == genplusgx::settings::defaultAudioSettings(),
      "Missing audio settings did not load defaults")) {
    return 2;
  }

  genplusgx::settings::AudioSettings custom{
    .masterVolumePercent = 65,
    .muted = true,
    .latencyMilliseconds = 45,
    .outputDeviceName = "Test output",
    .core = {
      .output = genplusgx::CoreSoundOutput::mono,
      .filter = genplusgx::CoreAudioFilter::equalizer,
      .ym2612Core = genplusgx::CoreYm2612Core::nukedYm3438,
      .ym2413Mode = genplusgx::CoreYm2413Mode::enabled,
      .ym2413Core = genplusgx::CoreYm2413Core::nuked,
      .psgLevelPercent = 125,
      .fmLevelPercent = 90,
      .cddaLevelPercent = 80,
      .pcmLevelPercent = 70,
      .lowPassPercent = 55,
      .equalizerLowPercent = 110,
      .equalizerMidPercent = 95,
      .equalizerHighPercent = 105,
      .highQualityFm = false,
      .highQualityPsg = false,
    },
  };
  if (!check(genplusgx::settings::validateAudioSettings(custom),
        "Valid custom audio settings were rejected") ||
      !check(store.save(custom), "Audio settings could not be saved") ||
      !check(store.load().settings == custom,
        "Audio settings did not round-trip exactly")) {
    return 3;
  }

  auto invalid = custom;
  invalid.core.psgLevelPercent = 201;
  if (!check(!genplusgx::settings::validateAudioSettings(invalid) &&
      store.save(invalid).error == genplusgx::PersistenceError::invalidData,
      "Out-of-range core mixer setting was accepted")) {
    return 4;
  }
  invalid = custom;
  invalid.latencyMilliseconds = 501;
  if (!check(!genplusgx::settings::validateAudioSettings(invalid),
      "Out-of-range latency was accepted")) {
    return 5;
  }

  constexpr std::string_view legacy = R"json({
    "schemaVersion": 0, "volume": 35, "mute": true, "latency": 120
  })json";
  if (!check(writeText(store.path(), legacy), "Legacy settings could not be staged")) {
    return 6;
  }
  const auto migrated = store.load();
  if (!check(migrated.status && migrated.migrated &&
      migrated.settings.masterVolumePercent == 35 && migrated.settings.muted &&
      migrated.settings.latencyMilliseconds == 120 &&
      migrated.settings.core == genplusgx::CoreAudioSettings{},
      "Schema-zero audio settings did not migrate safely")) {
    return 7;
  }

  if (!check(writeText(store.path(), "{broken"),
        "Corrupt settings could not be staged")) {
    return 8;
  }
  const auto corrupt = store.load();
  if (!check(!corrupt.status &&
      corrupt.settings == genplusgx::settings::defaultAudioSettings(),
      "Corrupt audio settings did not fail closed")) {
    return 9;
  }
  constexpr std::string_view future = R"json({"schemaVersion":999})json";
  if (!check(writeText(store.path(), future), "Future settings could not be staged")) {
    return 10;
  }
  return check(!store.load().status, "Future audio schema was accepted") ? 0 : 11;
}
