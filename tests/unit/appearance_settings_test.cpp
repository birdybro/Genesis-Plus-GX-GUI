#include "genplusgx/settings/appearance_settings.h"

#include <QTemporaryDir>

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
    genplusgx::settings::AppearanceSettingsStore::maximumFileBytes);
}

} // namespace

int main()
{
  QTemporaryDir directory;
  if (!check(directory.isValid(),
        "Temporary appearance settings directory was unavailable")) {
    return 1;
  }
  const std::filesystem::path root{directory.path().toStdString()};
  genplusgx::settings::AppearanceSettingsStore store{root / "appearance-settings.json"};

  const auto missing = store.load();
  if (!check(missing.status && !missing.migrated &&
               missing.settings == genplusgx::settings::defaultAppearanceSettings(),
        "Missing appearance settings did not use system defaults")) {
    return 2;
  }

  const genplusgx::settings::AppearanceSettings dark{
    .theme = genplusgx::settings::ThemeMode::dark,
    .developerToolsEnabled = true,
  };
  if (!check(store.save(dark), "Dark appearance settings could not be saved") ||
      !check(store.load().settings == dark,
        "Appearance settings did not round-trip exactly")) {
    return 3;
  }

  const genplusgx::settings::AppearanceSettings invalid{
    .theme = static_cast<genplusgx::settings::ThemeMode>(99),
  };
  if (!check(!genplusgx::settings::validateAppearanceSettings(invalid) &&
               store.save(invalid).error == genplusgx::PersistenceError::invalidData,
        "An invalid theme enum was accepted")) {
    return 4;
  }

  constexpr std::string_view legacy =
    R"json({"schemaVersion": 0, "darkTheme": false})json";
  if (!check(writeText(store.path(), legacy),
        "Legacy appearance settings could not be staged")) {
    return 5;
  }
  const auto migrated = store.load();
  if (!check(migrated.status && migrated.migrated &&
               migrated.settings.theme == genplusgx::settings::ThemeMode::light,
        "Schema-zero appearance settings did not migrate semantically")) {
    return 6;
  }

  constexpr std::string_view invalidTheme = R"json({
    "schemaVersion": 1,
    "appearance": {"theme": "ultraviolet"}
  })json";
  if (!check(writeText(store.path(), invalidTheme),
        "Invalid appearance settings could not be staged")) {
    return 7;
  }
  const auto invalidLoaded = store.load();
  if (!check(
        !invalidLoaded.status &&
          invalidLoaded.settings == genplusgx::settings::defaultAppearanceSettings(),
        "Invalid theme text did not fail closed")) {
    return 8;
  }

  constexpr std::string_view schemaOne = R"json({
    "schemaVersion": 1,
    "appearance": {"theme": "dark"}
  })json";
  if (!check(writeText(store.path(), schemaOne),
        "Schema-one appearance settings could not be staged")) {
    return 9;
  }
  const auto schemaOneLoaded = store.load();
  if (!check(schemaOneLoaded.status && schemaOneLoaded.migrated &&
          schemaOneLoaded.settings.theme ==
            genplusgx::settings::ThemeMode::dark &&
          !schemaOneLoaded.settings.developerToolsEnabled,
        "Schema-one settings did not migrate with debug tools hidden")) {
    return 10;
  }

  constexpr std::string_view future =
    R"json({"schemaVersion": 999, "appearance": {"theme": "system"}})json";
  if (!check(writeText(store.path(), future),
        "Future appearance settings could not be staged")) {
    return 11;
  }
  const auto unsupported = store.load();
  if (!check(!unsupported.status &&
               unsupported.status.message.find("not supported") != std::string::npos,
        "A future appearance schema was silently accepted")) {
    return 12;
  }

  if (!check(writeText(store.path(), "{broken"),
        "Corrupt appearance settings could not be staged")) {
    return 13;
  }
  const auto corrupt = store.load();
  return check(!corrupt.status &&
                 corrupt.status.error == genplusgx::PersistenceError::invalidData &&
                 corrupt.settings == genplusgx::settings::defaultAppearanceSettings(),
           "Corrupt appearance settings did not fail closed")
           ? 0
           : 14;
}
