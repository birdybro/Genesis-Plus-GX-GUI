#include "genplusgx/settings/appearance_settings.h"

#include "genplusgx/localization/localization.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <optional>
#include <span>
#include <utility>

namespace genplusgx::settings {
namespace {

PersistenceStatus invalid(std::string message)
{
  return {
    .error = PersistenceError::invalidData,
    .message = std::move(message),
  };
}

QString themeName(ThemeMode theme)
{
  switch (theme) {
    case ThemeMode::system:
      return QStringLiteral("system");
    case ThemeMode::light:
      return QStringLiteral("light");
    case ThemeMode::dark:
      return QStringLiteral("dark");
  }
  return {};
}

std::optional<ThemeMode> themeFromName(const QString& name)
{
  if (name == QStringLiteral("system")) {
    return ThemeMode::system;
  }
  if (name == QStringLiteral("light")) {
    return ThemeMode::light;
  }
  if (name == QStringLiteral("dark")) {
    return ThemeMode::dark;
  }
  return std::nullopt;
}

AppearanceSettingsLoadResult invalidResult(std::string message)
{
  return {
    .status = invalid(std::move(message)),
    .settings = defaultAppearanceSettings(),
  };
}

} // namespace

AppearanceSettings defaultAppearanceSettings() noexcept { return {}; }

bool validateAppearanceSettings(const AppearanceSettings& settings) noexcept
{
  return static_cast<unsigned>(settings.theme) <=
           static_cast<unsigned>(ThemeMode::dark) &&
         localization::isSupportedLanguagePreference(settings.language);
}

AppearanceSettingsStore::AppearanceSettingsStore(std::filesystem::path path)
    : path_(std::move(path))
{
}

const std::filesystem::path& AppearanceSettingsStore::path() const noexcept
{
  return path_;
}

AppearanceSettingsLoadResult AppearanceSettingsStore::load() const
{
  const auto loaded = readFileBounded(path_, maximumFileBytes);
  if (!loaded.status) {
    return {.status = loaded.status, .settings = defaultAppearanceSettings()};
  }
  if (!loaded.exists) {
    return {.status = {}, .settings = defaultAppearanceSettings()};
  }

  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(
    QByteArray{reinterpret_cast<const char*>(loaded.data.data()),
      static_cast<qsizetype>(loaded.data.size())},
    &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return invalidResult("The appearance settings file is not valid JSON.");
  }

  const auto root = document.object();
  const auto schema = root.value(QStringLiteral("schemaVersion"));
  if (!schema.isDouble()) {
    return invalidResult("The appearance settings schema version is missing.");
  }

  AppearanceSettings settings;
  bool migrated = false;
  if (schema.toInt(-1) == 0) {
    const auto darkTheme = root.value(QStringLiteral("darkTheme"));
    if (!darkTheme.isBool()) {
      return invalidResult("The legacy appearance settings are invalid.");
    }
    settings.theme = darkTheme.toBool() ? ThemeMode::dark : ThemeMode::light;
    migrated = true;
  } else if (schema.toInt(-1) >= 1 &&
             schema.toInt(-1) <= static_cast<int>(schemaVersion)) {
    const auto appearance = root.value(QStringLiteral("appearance"));
    if (!appearance.isObject()) {
      return invalidResult("The appearance settings values are invalid.");
    }
    const auto theme = appearance.toObject().value(QStringLiteral("theme"));
    if (!theme.isString()) {
      return invalidResult("The appearance theme is missing.");
    }
    const auto parsed = themeFromName(theme.toString());
    if (!parsed) {
      return invalidResult("The appearance theme is invalid.");
    }
    settings.theme = *parsed;
    if (schema.toInt(-1) <= 2) {
      settings.language = std::string{localization::systemLanguage};
    } else {
      const auto language = appearance.toObject().value(
        QStringLiteral("language"));
      if (!language.isString()) {
        return invalidResult("The interface language is missing.");
      }
      settings.language = language.toString().toStdString();
      if (!localization::isSupportedLanguagePreference(settings.language)) {
        return invalidResult("The interface language is invalid.");
      }
    }
    if (schema.toInt(-1) == 1) {
      settings.developerToolsEnabled = false;
    } else {
      const auto developerTools = appearance.toObject().value(
        QStringLiteral("developerToolsEnabled"));
      if (!developerTools.isBool()) {
        return invalidResult(
          "The developer-tools visibility setting is missing.");
      }
      settings.developerToolsEnabled = developerTools.toBool();
    }
    migrated = schema.toInt(-1) < static_cast<int>(schemaVersion);
  } else {
    return invalidResult("The appearance settings schema version is not supported.");
  }

  if (!validateAppearanceSettings(settings)) {
    return invalidResult("The appearance settings values are invalid.");
  }
  return {.status = {}, .settings = settings, .migrated = migrated};
}

PersistenceStatus AppearanceSettingsStore::save(
  const AppearanceSettings& settings) const
{
  if (!validateAppearanceSettings(settings)) {
    return invalid("Invalid appearance settings cannot be saved.");
  }
  const auto data = QJsonDocument{
    QJsonObject{
      {QStringLiteral("schemaVersion"), static_cast<int>(schemaVersion)},
      {QStringLiteral("appearance"),
        QJsonObject{
          {QStringLiteral("theme"), themeName(settings.theme)},
          {QStringLiteral("language"),
            QString::fromStdString(settings.language)},
          {QStringLiteral("developerToolsEnabled"),
            settings.developerToolsEnabled},
        }},
    }}.toJson(QJsonDocument::Indented);
  return writeFileAtomically(path_,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(data.constData()),
      static_cast<std::size_t>(data.size())},
    maximumFileBytes);
}

} // namespace genplusgx::settings
