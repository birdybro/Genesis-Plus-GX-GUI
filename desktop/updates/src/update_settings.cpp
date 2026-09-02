#include "genplusgx/updates/update_settings.h"

#include "genplusgx/updates/update_types.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <span>
#include <utility>

namespace genplusgx::updates {
namespace {

PersistenceStatus invalid(std::string message)
{
  return {.error = PersistenceError::invalidData, .message = std::move(message)};
}

bool validTimestamp(const std::string& value)
{
  if (value.empty()) {
    return true;
  }
  const auto timestamp = QDateTime::fromString(
    QString::fromStdString(value), Qt::ISODateWithMs);
  return timestamp.isValid() && timestamp.timeSpec() == Qt::UTC &&
    timestamp.toString(Qt::ISODateWithMs).toStdString() == value;
}

} // namespace

Settings defaultSettings() noexcept
{
  return {};
}

bool validateSettings(const Settings& settings) noexcept
{
  return validTimestamp(settings.lastCheckUtc) &&
    (settings.highestSeenVersion.empty() ||
      parseSemanticVersion(settings.highestSeenVersion).has_value());
}

bool automaticCheckDue(const Settings& settings, const std::string& nowUtc) noexcept
{
  if (!settings.automaticChecks || !validTimestamp(nowUtc) || nowUtc.empty()) {
    return false;
  }
  if (settings.lastCheckUtc.empty()) {
    return true;
  }
  if (!validTimestamp(settings.lastCheckUtc)) {
    return false;
  }
  const auto last = QDateTime::fromString(
    QString::fromStdString(settings.lastCheckUtc), Qt::ISODateWithMs);
  const auto now = QDateTime::fromString(
    QString::fromStdString(nowUtc), Qt::ISODateWithMs);
  return last.secsTo(now) >= 24 * 60 * 60;
}

SettingsStore::SettingsStore(std::filesystem::path path) : path_(std::move(path))
{
}

const std::filesystem::path& SettingsStore::path() const noexcept
{
  return path_;
}

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
  const auto document = QJsonDocument::fromJson(QByteArray{
    reinterpret_cast<const char*>(loaded.data.data()),
    static_cast<qsizetype>(loaded.data.size())}, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return {.status = invalid("The update settings file is not valid JSON."),
      .settings = defaultSettings()};
  }
  const auto root = document.object();
  const auto schema = root.value(QStringLiteral("schemaVersion"));
  const auto automatic = root.value(QStringLiteral("automaticChecks"));
  const auto lastCheck = root.value(QStringLiteral("lastCheckUtc"));
  const auto highest = root.value(QStringLiteral("highestSeenVersion"));
  if (!schema.isDouble() || schema.toDouble() != schemaVersion ||
      !automatic.isBool() || !lastCheck.isString() || !highest.isString()) {
    return {.status = invalid("The update settings values are invalid."),
      .settings = defaultSettings()};
  }
  Settings settings{
    .automaticChecks = automatic.toBool(),
    .lastCheckUtc = lastCheck.toString().toStdString(),
    .highestSeenVersion = highest.toString().toStdString(),
  };
  if (!validateSettings(settings)) {
    return {.status = invalid("The update settings values are invalid."),
      .settings = defaultSettings()};
  }
  return {.status = {}, .settings = std::move(settings)};
}

PersistenceStatus SettingsStore::save(const Settings& settings) const
{
  if (!validateSettings(settings)) {
    return invalid("Invalid update settings cannot be saved.");
  }
  const auto data = QJsonDocument{QJsonObject{
    {QStringLiteral("schemaVersion"), static_cast<int>(schemaVersion)},
    {QStringLiteral("automaticChecks"), settings.automaticChecks},
    {QStringLiteral("lastCheckUtc"), QString::fromStdString(settings.lastCheckUtc)},
    {QStringLiteral("highestSeenVersion"),
      QString::fromStdString(settings.highestSeenVersion)},
  }}.toJson(QJsonDocument::Indented);
  return writeFileAtomically(path_,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(data.constData()),
      static_cast<std::size_t>(data.size())}, maximumFileBytes);
}

} // namespace genplusgx::updates
