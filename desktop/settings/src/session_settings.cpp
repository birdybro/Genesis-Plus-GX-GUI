#include "genplusgx/settings/session_settings.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <span>
#include <utility>

namespace genplusgx::settings {
namespace {

PersistenceStatus invalid(std::string message)
{
  return {.error = PersistenceError::invalidData, .message = std::move(message)};
}

QString pathToQString(const std::filesystem::path& path)
{
#if defined(_WIN32)
  return QString::fromStdWString(path.native());
#else
  return QString::fromUtf8(path.native());
#endif
}

std::filesystem::path pathFromQString(const QString& path)
{
#if defined(_WIN32)
  return std::filesystem::path{path.toStdWString()};
#else
  const auto encoded = path.toUtf8();
  return std::filesystem::path{
    std::string{encoded.constData(), static_cast<std::size_t>(encoded.size())}};
#endif
}

SessionSettingsLoadResult invalidResult(std::string message)
{
  return {
    .status = invalid(std::move(message)),
    .settings = defaultSessionSettings(),
  };
}

} // namespace

SessionSettings defaultSessionSettings() noexcept
{
  return {};
}

bool validateSessionSettings(const SessionSettings& settings) noexcept
{
  if (!settings.lastGamePath) {
    return true;
  }
  if (settings.lastGamePath->empty() || !settings.lastGamePath->is_absolute()) {
    return false;
  }
  return settings.lastGamePath->native().size() <=
    SessionSettingsStore::maximumPathBytes;
}

SessionSettingsStore::SessionSettingsStore(std::filesystem::path path)
  : path_(std::move(path))
{
}

const std::filesystem::path& SessionSettingsStore::path() const noexcept
{
  return path_;
}

SessionSettingsLoadResult SessionSettingsStore::load() const
{
  const auto loaded = readFileBounded(path_, maximumFileBytes);
  if (!loaded.status) {
    return {.status = loaded.status, .settings = defaultSessionSettings()};
  }
  if (!loaded.exists) {
    return {.status = {}, .settings = defaultSessionSettings()};
  }

  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(
    QByteArray{reinterpret_cast<const char*>(loaded.data.data()),
      static_cast<qsizetype>(loaded.data.size())}, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return invalidResult("The session settings file is not valid JSON.");
  }
  const auto root = document.object();
  const auto schema = root.value(QStringLiteral("schemaVersion"));
  const auto resume = root.value(QStringLiteral("resumeOnLaunch"));
  const auto lastGame = root.value(QStringLiteral("lastGamePath"));
  if (!schema.isDouble() ||
      schema.toDouble() != static_cast<double>(schemaVersion) ||
      !resume.isBool() || (!lastGame.isNull() && !lastGame.isString())) {
    return invalidResult("The session settings values are invalid.");
  }

  SessionSettings settings{
    .resumeOnLaunch = resume.toBool(),
    .lastGamePath = std::nullopt,
  };
  if (lastGame.isString()) {
    settings.lastGamePath = pathFromQString(lastGame.toString());
  }
  if (!validateSessionSettings(settings)) {
    return invalidResult("The session settings path is invalid.");
  }
  return {.status = {}, .settings = std::move(settings)};
}

PersistenceStatus SessionSettingsStore::save(
  const SessionSettings& settings) const
{
  if (!validateSessionSettings(settings)) {
    return invalid("Invalid session settings cannot be saved.");
  }
  QJsonValue lastGame{QJsonValue::Null};
  if (settings.lastGamePath) {
    lastGame = pathToQString(*settings.lastGamePath);
  }
  const auto data = QJsonDocument{QJsonObject{
    {QStringLiteral("schemaVersion"), static_cast<int>(schemaVersion)},
    {QStringLiteral("resumeOnLaunch"), settings.resumeOnLaunch},
    {QStringLiteral("lastGamePath"), lastGame},
  }}.toJson(QJsonDocument::Indented);
  return writeFileAtomically(path_,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(data.constData()),
      static_cast<std::size_t>(data.size())}, maximumFileBytes);
}

} // namespace genplusgx::settings
