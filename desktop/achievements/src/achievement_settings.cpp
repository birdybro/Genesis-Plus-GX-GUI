#include "genplusgx/achievements/achievement_settings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <algorithm>
#include <cctype>
#include <span>
#include <utility>

namespace genplusgx::achievements {
namespace {

PersistenceStatus invalid(std::string message)
{
  return {
    .error = PersistenceError::invalidData,
    .message = std::move(message),
  };
}

SettingsLoadResult invalidResult(std::string message)
{
  return {
    .status = invalid(std::move(message)),
    .settings = defaultSettings(),
  };
}

} // namespace

Settings defaultSettings() noexcept { return {}; }

bool validUsername(const std::string& username) noexcept
{
  if (username.empty() || username.size() > maximumUsernameBytes) {
    return false;
  }
  return std::all_of(username.begin(), username.end(), [](unsigned char value) {
    return std::isalnum(value) != 0 || value == '_' || value == '-' || value == '.';
  });
}

bool validateSettings(const Settings& settings) noexcept
{
  return settings.username.empty() || validUsername(settings.username);
}

SettingsStore::SettingsStore(std::filesystem::path path) : path_(std::move(path)) {}

const std::filesystem::path& SettingsStore::path() const noexcept { return path_; }

SettingsLoadResult SettingsStore::load() const
{
  const auto loaded = readFileBounded(path_, maximumFileBytes);
  if (!loaded.status) {
    return {.status = loaded.status, .settings = defaultSettings()};
  }
  if (!loaded.exists) {
    return {.status = {}, .settings = defaultSettings()};
  }

  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(
    QByteArray{reinterpret_cast<const char*>(loaded.data.data()),
      static_cast<qsizetype>(loaded.data.size())},
    &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return invalidResult("The achievements settings file is not valid JSON.");
  }
  const auto root = document.object();
  const auto schema = root.value(QStringLiteral("schemaVersion"));
  const auto values = root.value(QStringLiteral("achievements"));
  if (!schema.isDouble() || schema.toInt(-1) != static_cast<int>(schemaVersion) ||
      !values.isObject()) {
    return invalidResult("The achievements settings schema is not supported.");
  }
  const auto object = values.toObject();
  const auto enabled = object.value(QStringLiteral("enabled"));
  const auto hardcore = object.value(QStringLiteral("hardcore"));
  const auto unofficial = object.value(QStringLiteral("unofficial"));
  const auto encore = object.value(QStringLiteral("encore"));
  const auto notifications = object.value(QStringLiteral("notifications"));
  const auto username = object.value(QStringLiteral("username"));
  if (!enabled.isBool() || !hardcore.isBool() || !unofficial.isBool() ||
      !encore.isBool() || !notifications.isBool() || !username.isString()) {
    return invalidResult("The achievements settings values are incomplete.");
  }
  Settings settings{
    .enabled = enabled.toBool(),
    .hardcore = hardcore.toBool(),
    .unofficial = unofficial.toBool(),
    .encore = encore.toBool(),
    .notifications = notifications.toBool(),
    .username = username.toString().toStdString(),
  };
  if (!validateSettings(settings)) {
    return invalidResult("The achievements settings values are invalid.");
  }
  return {.status = {}, .settings = std::move(settings)};
}

PersistenceStatus SettingsStore::save(const Settings& settings) const
{
  if (!validateSettings(settings)) {
    return invalid("Invalid achievements settings cannot be saved.");
  }
  const auto data = QJsonDocument{QJsonObject{
    {QStringLiteral("schemaVersion"), static_cast<int>(schemaVersion)},
    {QStringLiteral("achievements"), QJsonObject{
      {QStringLiteral("enabled"), settings.enabled},
      {QStringLiteral("hardcore"), settings.hardcore},
      {QStringLiteral("unofficial"), settings.unofficial},
      {QStringLiteral("encore"), settings.encore},
      {QStringLiteral("notifications"), settings.notifications},
      {QStringLiteral("username"), QString::fromStdString(settings.username)},
    }},
  }}.toJson(QJsonDocument::Indented);
  return writeFileAtomically(path_,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(data.constData()),
      static_cast<std::size_t>(data.size())},
    maximumFileBytes);
}

} // namespace genplusgx::achievements
