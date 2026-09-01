#include "genplusgx/settings/speed_settings.h"

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

SpeedSettingsLoadResult invalidResult(std::string message)
{
  return {
    .status = invalid(std::move(message)),
    .settings = defaultSpeedSettings(),
  };
}

} // namespace

EmulationSpeedConfiguration defaultSpeedSettings() noexcept
{
  return {};
}

SpeedSettingsStore::SpeedSettingsStore(std::filesystem::path path)
  : path_(std::move(path))
{
}

const std::filesystem::path& SpeedSettingsStore::path() const noexcept
{
  return path_;
}

SpeedSettingsLoadResult SpeedSettingsStore::load() const
{
  const auto loaded = readFileBounded(path_, maximumFileBytes);
  if (!loaded.status) {
    return {.status = loaded.status, .settings = defaultSpeedSettings()};
  }
  if (!loaded.exists) {
    return {.status = {}, .settings = defaultSpeedSettings()};
  }
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(
    QByteArray{reinterpret_cast<const char*>(loaded.data.data()),
      static_cast<qsizetype>(loaded.data.size())}, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return invalidResult("The emulation speed settings file is not valid JSON.");
  }
  const auto root = document.object();
  const auto schema = unsignedInteger(root, "schemaVersion");
  if (!schema || *schema != schemaVersion) {
    return invalidResult(
      "The emulation speed settings schema version is not supported.");
  }
  const auto member = root.value(QStringLiteral("speed"));
  if (!member.isObject()) {
    return invalidResult("The emulation speed settings object is missing.");
  }
  const auto object = member.toObject();
  const auto normal = unsignedInteger(object, "normalPercent");
  const auto slow = unsignedInteger(object, "slowMotionPercent");
  const auto fast = unsignedInteger(object, "fastForwardPercent");
  if (!normal || !slow || !fast) {
    return invalidResult("The emulation speed settings values are invalid.");
  }
  const EmulationSpeedConfiguration settings{
    .normalPercent = *normal,
    .slowMotionPercent = *slow,
    .fastForwardPercent = *fast,
  };
  if (!validateEmulationSpeedConfiguration(settings)) {
    return invalidResult(
      "The emulation speed settings values are outside safe limits.");
  }
  return {.status = {}, .settings = settings};
}

PersistenceStatus SpeedSettingsStore::save(
  const EmulationSpeedConfiguration& settings) const
{
  if (!validateEmulationSpeedConfiguration(settings)) {
    return invalid("Invalid emulation speed settings cannot be saved.");
  }
  const auto data = QJsonDocument{QJsonObject{
    {QStringLiteral("schemaVersion"), static_cast<int>(schemaVersion)},
    {QStringLiteral("speed"), QJsonObject{
      {QStringLiteral("normalPercent"),
        static_cast<int>(settings.normalPercent)},
      {QStringLiteral("slowMotionPercent"),
        static_cast<int>(settings.slowMotionPercent)},
      {QStringLiteral("fastForwardPercent"),
        static_cast<int>(settings.fastForwardPercent)},
    }},
  }}.toJson(QJsonDocument::Indented);
  return writeFileAtomically(path_,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(data.constData()),
      static_cast<std::size_t>(data.size())}, maximumFileBytes);
}

} // namespace genplusgx::settings
