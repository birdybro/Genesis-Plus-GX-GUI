#include "genplusgx/settings/rewind_settings.h"

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

constexpr std::size_t bytesPerMebibyte = 1024U * 1024U;

PersistenceStatus invalid(std::string message)
{
  return {.error = PersistenceError::invalidData, .message = std::move(message)};
}

std::optional<int> integer(const QJsonObject& object, const char* key)
{
  const auto member = object.value(QString::fromLatin1(key));
  if (!member.isDouble()) {
    return std::nullopt;
  }
  const auto value = member.toDouble();
  if (!std::isfinite(value) || std::floor(value) != value ||
      value < static_cast<double>(std::numeric_limits<int>::min()) ||
      value > static_cast<double>(std::numeric_limits<int>::max())) {
    return std::nullopt;
  }
  return static_cast<int>(value);
}

RewindSettingsLoadResult invalidResult(std::string message)
{
  return {
    .status = invalid(std::move(message)),
    .settings = defaultRewindSettings(),
  };
}

} // namespace

RewindConfiguration defaultRewindSettings() noexcept
{
  return {};
}

RewindSettingsStore::RewindSettingsStore(std::filesystem::path path)
  : path_(std::move(path))
{
}

const std::filesystem::path& RewindSettingsStore::path() const noexcept
{
  return path_;
}

RewindSettingsLoadResult RewindSettingsStore::load() const
{
  const auto loaded = readFileBounded(path_, maximumFileBytes);
  if (!loaded.status) {
    return {.status = loaded.status, .settings = defaultRewindSettings()};
  }
  if (!loaded.exists) {
    return {.status = {}, .settings = defaultRewindSettings()};
  }
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(
    QByteArray{reinterpret_cast<const char*>(loaded.data.data()),
      static_cast<qsizetype>(loaded.data.size())}, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return invalidResult("The rewind settings file is not valid JSON.");
  }
  const auto root = document.object();
  const auto schema = integer(root, "schemaVersion");
  if (!schema || *schema != static_cast<int>(schemaVersion)) {
    return invalidResult("The rewind settings schema version is not supported.");
  }
  const auto member = root.value(QStringLiteral("rewind"));
  if (!member.isObject()) {
    return invalidResult("The rewind settings object is missing.");
  }
  const auto object = member.toObject();
  const auto enabled = object.value(QStringLiteral("enabled"));
  const auto interval = integer(object, "captureIntervalFrames");
  const auto memory = integer(object, "memoryLimitMiB");
  if (!enabled.isBool() || !interval || !memory || *memory < 0) {
    return invalidResult("The rewind settings values are invalid.");
  }
  const RewindConfiguration settings{
    .enabled = enabled.toBool(),
    .captureIntervalFrames = static_cast<std::uint32_t>(*interval),
    .memoryLimitBytes = static_cast<std::size_t>(*memory) * bytesPerMebibyte,
  };
  if (!validateRewindConfiguration(settings)) {
    return invalidResult("The rewind settings values are outside safe limits.");
  }
  return {.status = {}, .settings = settings};
}

PersistenceStatus RewindSettingsStore::save(
  const RewindConfiguration& settings) const
{
  if (!validateRewindConfiguration(settings) ||
      settings.memoryLimitBytes % bytesPerMebibyte != 0U) {
    return invalid("Invalid rewind settings cannot be saved.");
  }
  const auto data = QJsonDocument{QJsonObject{
    {QStringLiteral("schemaVersion"), static_cast<int>(schemaVersion)},
    {QStringLiteral("rewind"), QJsonObject{
      {QStringLiteral("enabled"), settings.enabled},
      {QStringLiteral("captureIntervalFrames"),
        static_cast<int>(settings.captureIntervalFrames)},
      {QStringLiteral("memoryLimitMiB"),
        static_cast<int>(settings.memoryLimitBytes / bytesPerMebibyte)},
    }},
  }}.toJson(QJsonDocument::Indented);
  return writeFileAtomically(path_,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(data.constData()),
      static_cast<std::size_t>(data.size())}, maximumFileBytes);
}

} // namespace genplusgx::settings
