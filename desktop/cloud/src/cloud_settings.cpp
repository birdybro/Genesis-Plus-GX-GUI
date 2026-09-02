#include "genplusgx/cloud/cloud_settings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrl>

#include <algorithm>
#include <cctype>
#include <span>
#include <utility>

namespace genplusgx::cloud {
namespace {

PersistenceStatus invalid(std::string message)
{
  return {.error = PersistenceError::invalidData, .message = std::move(message)};
}

bool boundedPlainText(const std::string& value, std::size_t maximum)
{
  return !value.empty() && value.size() <= maximum &&
    std::ranges::none_of(value, [](unsigned char byte) {
      return byte < 0x20U || byte == 0x7fU;
    });
}

bool validRemoteDirectory(const std::string& value)
{
  return boundedPlainText(value, 64U) && value != "." && value != ".." &&
    std::ranges::all_of(value, [](unsigned char byte) {
      return std::isalnum(byte) != 0 || byte == '-' || byte == '_';
    });
}

} // namespace

Settings defaultSettings() noexcept { return {}; }

Status validateSettings(const Settings& settings) noexcept
{
  if (!settings.syncSaves && !settings.syncStates) {
    return {Error::invalidSettings,
      "Cloud synchronization must include saves, states, or both."};
  }
  if (!validRemoteDirectory(settings.remoteDirectory)) {
    return {Error::invalidSettings,
      "The remote directory must use 1-64 letters, digits, '-' or '_'."};
  }
  if (!settings.enabled && settings.endpoint.empty() && settings.username.empty()) {
    return {};
  }
  if (!boundedPlainText(settings.username, 128U) ||
      settings.username.find(':') != std::string::npos) {
    return {Error::invalidSettings,
      "The WebDAV username must contain 1-128 printable characters and no ':'."};
  }
  if (settings.endpoint.empty() || settings.endpoint.size() > 2'048U) {
    return {Error::invalidSettings, "The WebDAV endpoint is missing or too long."};
  }
  const QUrl url{QString::fromStdString(settings.endpoint), QUrl::StrictMode};
  if (!url.isValid() || url.scheme() != QStringLiteral("https") ||
      url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() ||
      url.hasFragment()) {
    return {Error::invalidSettings,
      "Use an HTTPS WebDAV collection URL without credentials, query, or fragment."};
  }
  return {};
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
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(QByteArray{
    reinterpret_cast<const char*>(loaded.data.data()),
    static_cast<qsizetype>(loaded.data.size())}, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return {.status = invalid("The cloud settings file is not valid JSON."),
      .settings = defaultSettings()};
  }
  const auto root = document.object();
  const auto values = root.value(QStringLiteral("cloud"));
  if (root.value(QStringLiteral("schemaVersion")).toInt(-1) !=
        static_cast<int>(schemaVersion) || !values.isObject()) {
    return {.status = invalid("The cloud settings schema is not supported."),
      .settings = defaultSettings()};
  }
  const auto object = values.toObject();
  const auto enabled = object.value(QStringLiteral("enabled"));
  const auto saves = object.value(QStringLiteral("syncSaves"));
  const auto states = object.value(QStringLiteral("syncStates"));
  const auto startup = object.value(QStringLiteral("syncOnStartup"));
  const auto close = object.value(QStringLiteral("syncOnGameClose"));
  const auto endpoint = object.value(QStringLiteral("endpoint"));
  const auto username = object.value(QStringLiteral("username"));
  const auto directory = object.value(QStringLiteral("remoteDirectory"));
  if (!enabled.isBool() || !saves.isBool() || !states.isBool() ||
      !startup.isBool() || !close.isBool() || !endpoint.isString() ||
      !username.isString() || !directory.isString()) {
    return {.status = invalid("The cloud settings values are incomplete."),
      .settings = defaultSettings()};
  }
  Settings settings{
    .enabled = enabled.toBool(),
    .syncSaves = saves.toBool(),
    .syncStates = states.toBool(),
    .syncOnStartup = startup.toBool(),
    .syncOnGameClose = close.toBool(),
    .endpoint = endpoint.toString().toStdString(),
    .username = username.toString().toStdString(),
    .remoteDirectory = directory.toString().toStdString(),
  };
  const auto status = validateSettings(settings);
  if (!status) {
    return {.status = invalid(status.message), .settings = defaultSettings()};
  }
  return {.status = {}, .settings = std::move(settings)};
}

PersistenceStatus SettingsStore::save(const Settings& settings) const
{
  const auto validation = validateSettings(settings);
  if (!validation) {
    return invalid(validation.message);
  }
  const auto data = QJsonDocument{QJsonObject{
    {QStringLiteral("schemaVersion"), static_cast<int>(schemaVersion)},
    {QStringLiteral("cloud"), QJsonObject{
      {QStringLiteral("enabled"), settings.enabled},
      {QStringLiteral("syncSaves"), settings.syncSaves},
      {QStringLiteral("syncStates"), settings.syncStates},
      {QStringLiteral("syncOnStartup"), settings.syncOnStartup},
      {QStringLiteral("syncOnGameClose"), settings.syncOnGameClose},
      {QStringLiteral("endpoint"), QString::fromStdString(settings.endpoint)},
      {QStringLiteral("username"), QString::fromStdString(settings.username)},
      {QStringLiteral("remoteDirectory"),
        QString::fromStdString(settings.remoteDirectory)},
    }},
  }}.toJson(QJsonDocument::Indented);
  return writeFileAtomically(path_, {
    reinterpret_cast<const std::uint8_t*>(data.constData()),
    static_cast<std::size_t>(data.size())}, maximumFileBytes);
}

} // namespace genplusgx::cloud
