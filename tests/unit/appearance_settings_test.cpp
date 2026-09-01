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
    .language = "en_XA",
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

  const genplusgx::settings::AppearanceSettings invalidLanguage{
    .language = "../outside",
  };
  if (!check(!genplusgx::settings::validateAppearanceSettings(invalidLanguage) &&
        store.save(invalidLanguage).error ==
          genplusgx::PersistenceError::invalidData,
      "An unrecognized language preference was accepted")) {
    return 5;
  }

  constexpr std::string_view legacy =
    R"json({"schemaVersion": 0, "darkTheme": false})json";
  if (!check(writeText(store.path(), legacy),
        "Legacy appearance settings could not be staged")) {
    return 6;
  }
  const auto migrated = store.load();
  if (!check(migrated.status && migrated.migrated &&
               migrated.settings.theme == genplusgx::settings::ThemeMode::light,
        "Schema-zero appearance settings did not migrate semantically")) {
    return 7;
  }

  constexpr std::string_view invalidTheme = R"json({
    "schemaVersion": 1,
    "appearance": {"theme": "ultraviolet"}
  })json";
  if (!check(writeText(store.path(), invalidTheme),
        "Invalid appearance settings could not be staged")) {
    return 8;
  }
  const auto invalidLoaded = store.load();
  if (!check(
        !invalidLoaded.status &&
          invalidLoaded.settings == genplusgx::settings::defaultAppearanceSettings(),
        "Invalid theme text did not fail closed")) {
    return 9;
  }

  constexpr std::string_view schemaOne = R"json({
    "schemaVersion": 1,
    "appearance": {"theme": "dark"}
  })json";
  if (!check(writeText(store.path(), schemaOne),
        "Schema-one appearance settings could not be staged")) {
    return 10;
  }
  const auto schemaOneLoaded = store.load();
  if (!check(schemaOneLoaded.status && schemaOneLoaded.migrated &&
          schemaOneLoaded.settings.theme ==
            genplusgx::settings::ThemeMode::dark &&
          !schemaOneLoaded.settings.developerToolsEnabled,
        "Schema-one settings did not migrate with debug tools hidden")) {
    return 11;
  }

  constexpr std::string_view schemaTwo = R"json({
    "schemaVersion": 2,
    "appearance": {"theme": "light", "developerToolsEnabled": true}
  })json";
  if (!check(writeText(store.path(), schemaTwo),
        "Schema-two appearance settings could not be staged")) {
    return 12;
  }
  const auto schemaTwoLoaded = store.load();
  if (!check(schemaTwoLoaded.status && schemaTwoLoaded.migrated &&
          schemaTwoLoaded.settings.language == "system" &&
          schemaTwoLoaded.settings.developerToolsEnabled,
        "Schema-two settings did not migrate to the system language")) {
    return 13;
  }

  constexpr std::string_view invalidStoredLanguage = R"json({
    "schemaVersion": 3,
    "appearance": {
      "theme": "system",
      "language": "../../outside",
      "developerToolsEnabled": false
    }
  })json";
  if (!check(writeText(store.path(), invalidStoredLanguage),
        "Invalid stored language could not be staged")) {
    return 14;
  }
  const auto invalidStoredLanguageResult = store.load();
  if (!check(!invalidStoredLanguageResult.status &&
          invalidStoredLanguageResult.settings ==
            genplusgx::settings::defaultAppearanceSettings(),
        "An invalid stored language did not fail closed")) {
    return 15;
  }

  constexpr std::string_view future =
    R"json({"schemaVersion": 999, "appearance": {"theme": "system"}})json";
  if (!check(writeText(store.path(), future),
        "Future appearance settings could not be staged")) {
    return 16;
  }
  const auto unsupported = store.load();
  if (!check(!unsupported.status &&
               unsupported.status.message.find("not supported") != std::string::npos,
        "A future appearance schema was silently accepted")) {
    return 17;
  }

  if (!check(writeText(store.path(), "{broken"),
        "Corrupt appearance settings could not be staged")) {
    return 18;
  }
  const auto corrupt = store.load();
  return check(!corrupt.status &&
                 corrupt.status.error == genplusgx::PersistenceError::invalidData &&
                 corrupt.settings == genplusgx::settings::defaultAppearanceSettings(),
           "Corrupt appearance settings did not fail closed")
           ? 0
           : 19;
}
