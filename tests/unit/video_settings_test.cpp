#include "genplusgx/settings/video_settings.h"

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
    genplusgx::settings::VideoSettingsStore::maximumFileBytes);
}

} // namespace

int main()
{
  QTemporaryDir directory;
  if (!check(directory.isValid(), "Temporary settings directory was unavailable")) {
    return 1;
  }
  const std::filesystem::path path{directory.path().toStdString()};
  genplusgx::settings::VideoSettingsStore store{path / "video-settings.json"};

  const auto missing = store.load();
  if (!check(missing.status && !missing.migrated &&
        missing.settings == genplusgx::settings::defaultVideoSettings(),
      "Missing settings did not produce safe defaults")) {
    return 2;
  }

  genplusgx::settings::VideoSettings custom{
    .aspect = genplusgx::video::AspectMode::stretch,
    .scaling = genplusgx::video::ScaleMode::integer,
    .presentationFilter = genplusgx::video::VideoFilter::bilinear,
    .presentation = {
      .sync = genplusgx::video::PresentationSyncMode::adaptive,
      .buffering =
        genplusgx::video::PresentationBufferingMode::tripleBuffer,
    },
    .shader = {
      .mode = genplusgx::video::ShaderMode::libretroPreset,
      .presetPath = path / "custom-crt.slangp",
      .parameters = {
        {.name = "SCANLINE_STRENGTH", .value = 0.42F},
        {.name = "CURVATURE", .value = 0.08F},
      },
    },
    .core = {
      .overscan = genplusgx::CoreOverscanMode::full,
      .ntscFilter = genplusgx::CoreNtscFilter::sVideo,
      .interlacedRender = genplusgx::CoreInterlacedRenderMode::doubleField,
      .gameGearExtendedScreen = true,
    },
  };
  if (!check(genplusgx::settings::validateVideoSettings(custom),
        "Valid settings were rejected") ||
      !check(store.save(custom), "Valid settings could not be saved")) {
    return 3;
  }
  const auto loaded = store.load();
  if (!check(loaded.status && !loaded.migrated && loaded.settings == custom,
      "Settings did not round-trip exactly")) {
    return 4;
  }

  auto invalidEnum = custom;
  invalidEnum.core.ntscFilter = static_cast<genplusgx::CoreNtscFilter>(99);
  if (!check(!genplusgx::settings::validateVideoSettings(invalidEnum) &&
        store.save(invalidEnum).error == genplusgx::PersistenceError::invalidData,
      "Invalid enum settings were accepted")) {
    return 5;
  }

  auto invalidShader = custom;
  invalidShader.shader.presetPath = "relative.slangp";
  if (!check(!genplusgx::settings::validateVideoSettings(invalidShader) &&
        store.save(invalidShader).error ==
          genplusgx::PersistenceError::invalidData,
      "Invalid shader settings were accepted")) {
    return 6;
  }

  auto invalidPresentation = custom;
  invalidPresentation.presentation.sync =
    static_cast<genplusgx::video::PresentationSyncMode>(99);
  if (!check(!genplusgx::settings::validateVideoSettings(invalidPresentation) &&
        store.save(invalidPresentation).error ==
          genplusgx::PersistenceError::invalidData,
      "Invalid presentation settings were accepted")) {
    return 7;
  }

  constexpr std::string_view legacy = R"json({
    "schemaVersion": 0,
    "integerScale": true,
    "forceFourThree": true,
    "bilinear": true,
    "overscan": 2,
    "ntscComposite": true,
    "gameGearExtendedScreen": true,
    "doubleField": true
  })json";
  if (!check(writeText(store.path(), legacy), "Legacy settings could not be staged")) {
    return 8;
  }
  const auto migrated = store.load();
  if (!check(migrated.status && migrated.migrated &&
        migrated.settings.aspect == genplusgx::video::AspectMode::fourThree &&
        migrated.settings.scaling == genplusgx::video::ScaleMode::integer &&
        migrated.settings.presentationFilter ==
          genplusgx::video::VideoFilter::bilinear &&
        migrated.settings.core.overscan ==
          genplusgx::CoreOverscanMode::horizontal &&
        migrated.settings.core.ntscFilter ==
          genplusgx::CoreNtscFilter::composite &&
        migrated.settings.core.gameGearExtendedScreen &&
        migrated.settings.core.interlacedRender ==
          genplusgx::CoreInterlacedRenderMode::doubleField,
      "Schema-zero settings did not migrate semantically")) {
    return 9;
  }

  constexpr std::string_view schemaOne = R"json({
    "schemaVersion": 1,
    "video": {
      "aspect": "native",
      "scaling": "fit",
      "presentationFilter": "nearest",
      "overscan": "disabled",
      "ntscFilter": "disabled",
      "gameGearExtendedScreen": false,
      "interlacedRender": "single-field"
    }
  })json";
  if (!check(writeText(store.path(), schemaOne),
        "Schema-one settings could not be staged")) {
    return 10;
  }
  const auto schemaOneMigrated = store.load();
  if (!check(schemaOneMigrated.status && schemaOneMigrated.migrated &&
        schemaOneMigrated.settings ==
          genplusgx::settings::defaultVideoSettings(),
      "Schema-one settings did not gain safe shader defaults")) {
    return 11;
  }

  constexpr std::string_view schemaTwo = R"json({
    "schemaVersion": 2,
    "video": {
      "aspect": "native",
      "scaling": "fit",
      "presentationFilter": "nearest",
      "shader": {"mode": "disabled", "presetPath": "", "parameters": []},
      "overscan": "disabled",
      "ntscFilter": "disabled",
      "gameGearExtendedScreen": false,
      "interlacedRender": "single-field"
    }
  })json";
  if (!check(writeText(store.path(), schemaTwo),
        "Schema-two settings could not be staged")) {
    return 12;
  }
  const auto schemaTwoMigrated = store.load();
  if (!check(schemaTwoMigrated.status && schemaTwoMigrated.migrated &&
        schemaTwoMigrated.settings.presentation ==
          genplusgx::video::PresentationConfiguration{},
      "Schema-two settings did not gain safe synchronization defaults")) {
    return 13;
  }

  if (!check(writeText(store.path(), "{broken"),
        "Corrupt settings could not be staged")) {
    return 14;
  }
  const auto corrupt = store.load();
  if (!check(!corrupt.status &&
        corrupt.status.error == genplusgx::PersistenceError::invalidData &&
        corrupt.settings == genplusgx::settings::defaultVideoSettings(),
      "Corrupt settings did not fail closed to defaults")) {
    return 15;
  }

  constexpr std::string_view future =
    R"json({"schemaVersion": 999, "video": {}})json";
  if (!check(writeText(store.path(), future),
        "Future settings could not be staged")) {
    return 16;
  }
  const auto unsupported = store.load();
  return check(!unsupported.status &&
      unsupported.status.message.find("not supported") != std::string::npos,
      "A future settings schema was silently accepted")
    ? 0 : 17;
}
