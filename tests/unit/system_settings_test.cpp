#include "genplusgx/settings/system_settings.h"

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
    genplusgx::settings::SystemSettingsStore::maximumFileBytes);
}

} // namespace

int main()
{
  QTemporaryDir directory;
  if (!check(directory.isValid(), "Temporary directory was unavailable")) {
    return 1;
  }
  genplusgx::settings::SystemSettingsStore store{
    std::filesystem::path{directory.path().toStdString()} / "system-settings.json"};
  const auto missing = store.load();
  if (!check(missing.status && !missing.migrated &&
      missing.settings == genplusgx::CoreSystemSettings{},
      "Missing system settings did not load defaults")) {
    return 2;
  }

  genplusgx::CoreSystemSettings custom{
    .hardware = genplusgx::CoreSystemHardware::masterSystemII,
    .region = genplusgx::CoreSystemRegion::palJapan,
    .videoStandard = genplusgx::CoreVideoStandard::ntsc,
    .masterClock = genplusgx::CoreMasterClock::pal,
    .emulateIllegalAccessLockups = false,
    .enableAddressErrors = false,
  };
  if (!check(genplusgx::validateCoreSystemSettings(custom),
        "Valid system settings were rejected") ||
      !check(store.save(custom), "System settings could not be saved") ||
      !check(store.load().settings == custom,
        "System settings did not round-trip exactly")) {
    return 3;
  }

  auto invalid = custom;
  invalid.region = static_cast<genplusgx::CoreSystemRegion>(99);
  if (!check(!genplusgx::validateCoreSystemSettings(invalid) &&
      store.save(invalid).error == genplusgx::PersistenceError::invalidData,
      "Invalid system enum was accepted")) {
    return 4;
  }

  constexpr std::string_view legacy = R"json({
    "schemaVersion": 0,
    "hardware": 8,
    "region": 1,
    "videoStandard": 2,
    "masterClock": 1,
    "emulateIllegalAccessLockups": true,
    "enableAddressErrors": false
  })json";
  if (!check(writeText(store.path(), legacy), "Legacy settings could not be staged")) {
    return 5;
  }
  const auto migrated = store.load();
  if (!check(migrated.status && migrated.migrated &&
      migrated.settings.hardware == genplusgx::CoreSystemHardware::genesis &&
      migrated.settings.region == genplusgx::CoreSystemRegion::ntscU &&
      migrated.settings.videoStandard == genplusgx::CoreVideoStandard::pal &&
      migrated.settings.masterClock == genplusgx::CoreMasterClock::ntsc &&
      !migrated.settings.enableAddressErrors,
      "Schema-zero system settings did not migrate exactly")) {
    return 6;
  }

  if (!check(writeText(store.path(), "{broken"),
        "Corrupt settings could not be staged")) {
    return 7;
  }
  const auto corrupt = store.load();
  if (!check(!corrupt.status && corrupt.settings == genplusgx::CoreSystemSettings{},
      "Corrupt system settings did not fail closed")) {
    return 8;
  }
  constexpr std::string_view future = R"json({"schemaVersion":999})json";
  if (!check(writeText(store.path(), future), "Future schema could not be staged")) {
    return 9;
  }
  return check(!store.load().status, "Future system schema was accepted") ? 0 : 10;
}
