#include "genplusgx/settings/speed_settings.h"

#include "genplusgx/persistence.h"

#include <QTemporaryDir>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

namespace {

bool check(bool condition, const std::string& message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

bool writeFixture(
  const std::filesystem::path& path,
  std::string_view contents)
{
  return genplusgx::writeFileAtomically(path,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(contents.data()), contents.size()},
    genplusgx::settings::SpeedSettingsStore::maximumFileBytes);
}

} // namespace

int main()
{
  using namespace genplusgx;
  using namespace genplusgx::settings;

  QTemporaryDir directory;
  if (!check(directory.isValid(), "Could not create speed settings directory")) {
    return EXIT_FAILURE;
  }
  const auto path = std::filesystem::path{directory.path().toStdString()} /
    "nested" / "speed-settings.json";
  SpeedSettingsStore store{path};
  const auto defaults = defaultSpeedSettings();
  const auto missing = store.load();
  const EmulationSpeedConfiguration customized{
    .normalPercent = 125U,
    .slowMotionPercent = 25U,
    .fastForwardPercent = 800U,
  };
  if (!check(missing.status && missing.status.message.empty() &&
        missing.settings == defaults,
        "Missing speed settings did not use safe defaults") ||
      !check(store.path() == path, "Speed settings path changed") ||
      !check(store.save(customized), "Valid speed settings could not be saved")) {
    return EXIT_FAILURE;
  }
  const auto loaded = store.load();
  if (!check(loaded.status && loaded.settings == customized,
        "Speed settings did not round-trip exactly") ||
      !check(!store.save({
          .normalPercent = 49U,
          .slowMotionPercent = 50U,
          .fastForwardPercent = 400U,
        }), "An invalid normal speed was persisted") ||
      !check(!store.save({
          .normalPercent = 100U,
          .slowMotionPercent = 76U,
          .fastForwardPercent = 400U,
        }), "An invalid slow-motion speed was persisted") ||
      !check(!store.save({
          .normalPercent = 100U,
          .slowMotionPercent = 50U,
          .fastForwardPercent = 1'601U,
        }), "An invalid fast-forward speed was persisted")) {
    return EXIT_FAILURE;
  }

  constexpr std::string_view fixtures[]{
    R"({"schemaVersion":1,"speed":{"normalPercent":100,"slowMotionPercent":50}})",
    R"({"schemaVersion":1,"speed":{"normalPercent":100.5,"slowMotionPercent":50,"fastForwardPercent":400}})",
    R"({"schemaVersion":1,"speed":{"normalPercent":201,"slowMotionPercent":50,"fastForwardPercent":400}})",
    R"({"schemaVersion":999,"speed":{"normalPercent":100,"slowMotionPercent":50,"fastForwardPercent":400}})",
    R"({"schemaVersion":1,"speed":[]})",
    R"({ definitely not JSON)",
  };
  for (const auto fixture : fixtures) {
    if (!check(writeFixture(path, fixture),
          "Could not write invalid speed settings fixture")) {
      return EXIT_FAILURE;
    }
    const auto rejected = store.load();
    if (!check(!rejected.status &&
          rejected.status.error == PersistenceError::invalidData &&
          rejected.settings == defaults,
          "Invalid speed settings were not rejected safely")) {
      return EXIT_FAILURE;
    }
  }

  std::string oversized(SpeedSettingsStore::maximumFileBytes + 1U, 'x');
  if (!check(writeFileAtomically(path,
        std::span<const std::uint8_t>{
          reinterpret_cast<const std::uint8_t*>(oversized.data()),
          SpeedSettingsStore::maximumFileBytes},
        SpeedSettingsStore::maximumFileBytes),
        "Could not write maximum-size settings fixture")) {
    return EXIT_FAILURE;
  }
  {
    std::ofstream append{path, std::ios::binary | std::ios::app};
    append.put('x');
  }
  const auto oversizedResult = store.load();
  if (!check(!oversizedResult.status &&
        oversizedResult.status.error == PersistenceError::dataTooLarge &&
        oversizedResult.settings == defaults,
        "Oversized speed settings were not bounded safely")) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
