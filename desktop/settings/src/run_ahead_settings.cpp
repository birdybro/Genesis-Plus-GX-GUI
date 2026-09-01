#include "genplusgx/settings/run_ahead_settings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace genplusgx::settings {
namespace {

PersistenceStatus invalid(std::string message)
{
  return {.error = PersistenceError::invalidData, .message = std::move(message)};
}

std::optional<std::uint32_t> unsignedInteger(
  const QJsonObject& object,
  const char* key)
{
  const auto member = object.value(QString::fromLatin1(key));
  if (!member.isDouble()) {
    return std::nullopt;
  }
  const auto value = member.toDouble();
  if (!std::isfinite(value) || std::floor(value) != value || value < 0.0 ||
      value > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(value);
}

RunAheadSettingsLoadResult invalidResult(std::string message)
{
  return {
    .status = invalid(std::move(message)),
    .settings = defaultRunAheadSettings(),
  };
}

} // namespace

RunAheadConfiguration defaultRunAheadSettings() noexcept
{
  return {};
}

RunAheadSettingsStore::RunAheadSettingsStore(std::filesystem::path path)
  : path_(std::move(path))
{
}

const std::filesystem::path& RunAheadSettingsStore::path() const noexcept
{
  return path_;
}

RunAheadSettingsLoadResult RunAheadSettingsStore::load() const
{
  const auto loaded = readFileBounded(path_, maximumFileBytes);
  if (!loaded.status) {
    return {.status = loaded.status, .settings = defaultRunAheadSettings()};
  }
  if (!loaded.exists) {
    return {.status = {}, .settings = defaultRunAheadSettings()};
  }
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(
    QByteArray{reinterpret_cast<const char*>(loaded.data.data()),
      static_cast<qsizetype>(loaded.data.size())}, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return invalidResult("The run-ahead settings file is not valid JSON.");
  }
  const auto root = document.object();
  const auto schema = unsignedInteger(root, "schemaVersion");
  if (!schema || *schema != schemaVersion) {
    return invalidResult("The run-ahead settings schema version is not supported.");
  }
  const auto member = root.value(QStringLiteral("runAhead"));
  if (!member.isObject()) {
    return invalidResult("The run-ahead settings object is missing.");
  }
  const auto object = member.toObject();
  const auto enabled = object.value(QStringLiteral("enabled"));
  const auto frames = unsignedInteger(object, "frames");
  if (!enabled.isBool() || !frames) {
    return invalidResult("The run-ahead settings values are invalid.");
  }
  const RunAheadConfiguration settings{
    .enabled = enabled.toBool(),
    .frames = *frames,
  };
  if (!validateRunAheadConfiguration(settings)) {
    return invalidResult("The run-ahead settings values are outside safe limits.");
  }
  return {.status = {}, .settings = settings};
}

PersistenceStatus RunAheadSettingsStore::save(
  const RunAheadConfiguration& settings) const
{
  if (!validateRunAheadConfiguration(settings)) {
    return invalid("Invalid run-ahead settings cannot be saved.");
  }
  const auto data = QJsonDocument{QJsonObject{
    {QStringLiteral("schemaVersion"), static_cast<int>(schemaVersion)},
    {QStringLiteral("runAhead"), QJsonObject{
      {QStringLiteral("enabled"), settings.enabled},
      {QStringLiteral("frames"), static_cast<int>(settings.frames)},
    }},
  }}.toJson(QJsonDocument::Indented);
  return writeFileAtomically(path_,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(data.constData()),
      static_cast<std::size_t>(data.size())}, maximumFileBytes);
}

} // namespace genplusgx::settings
