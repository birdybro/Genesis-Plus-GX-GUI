#include "genplusgx/settings/system_settings.h"

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

std::optional<int> integer(const QJsonObject& object, const char* key)
{
  const auto member = object.value(QString::fromLatin1(key));
  if (!member.isDouble()) {
    return std::nullopt;
  }
  const double value = member.toDouble();
  if (!std::isfinite(value) || std::floor(value) != value ||
      value < static_cast<double>(std::numeric_limits<int>::min()) ||
      value > static_cast<double>(std::numeric_limits<int>::max())) {
    return std::nullopt;
  }
  return static_cast<int>(value);
}

std::optional<bool> boolean(const QJsonObject& object, const char* key)
{
  const auto member = object.value(QString::fromLatin1(key));
  return member.isBool() ? std::optional{member.toBool()} : std::nullopt;
}

template<typename Enum>
std::optional<Enum> enumeration(const QJsonObject& object, const char* key)
{
  const auto value = integer(object, key);
  return value && *value >= 0 ? std::optional{static_cast<Enum>(*value)}
                              : std::nullopt;
}

std::optional<CoreSystemSettings> readSettings(const QJsonObject& object)
{
  const auto hardware = enumeration<CoreSystemHardware>(object, "hardware");
  const auto region = enumeration<CoreSystemRegion>(object, "region");
  const auto standard = enumeration<CoreVideoStandard>(object, "videoStandard");
  const auto clock = enumeration<CoreMasterClock>(object, "masterClock");
  const auto lockups = boolean(object, "emulateIllegalAccessLockups");
  const auto addressErrors = boolean(object, "enableAddressErrors");
  if (!hardware || !region || !standard || !clock || !lockups ||
      !addressErrors) {
    return std::nullopt;
  }
  return CoreSystemSettings{
    .hardware = *hardware,
    .region = *region,
    .videoStandard = *standard,
    .masterClock = *clock,
    .emulateIllegalAccessLockups = *lockups,
    .enableAddressErrors = *addressErrors,
  };
}

SystemSettingsLoadResult invalidResult(std::string message)
{
  return {.status = invalid(std::move(message)), .settings = {}};
}

} // namespace

SystemSettingsStore::SystemSettingsStore(std::filesystem::path path)
  : path_(std::move(path))
{
}

const std::filesystem::path& SystemSettingsStore::path() const noexcept
{
  return path_;
}

SystemSettingsLoadResult SystemSettingsStore::load() const
{
  const auto loaded = readFileBounded(path_, maximumFileBytes);
  if (!loaded.status) {
    return {.status = loaded.status, .settings = {}};
  }
  if (!loaded.exists) {
    return {.status = {}, .settings = {}};
  }
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(
    QByteArray{reinterpret_cast<const char*>(loaded.data.data()),
      static_cast<qsizetype>(loaded.data.size())}, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return invalidResult("The system settings file is not valid JSON.");
  }
  const auto root = document.object();
  const auto schema = integer(root, "schemaVersion");
  if (!schema) {
    return invalidResult("The system settings schema version is missing.");
  }
  if (*schema == 0) {
    const auto migrated = readSettings(root);
    if (!migrated || !validateCoreSystemSettings(*migrated)) {
      return invalidResult("The legacy system settings are invalid.");
    }
    return {.status = {}, .settings = *migrated, .migrated = true};
  }
  if (*schema != static_cast<int>(schemaVersion)) {
    return invalidResult("The system settings schema version is not supported.");
  }
  const auto member = root.value(QStringLiteral("system"));
  const auto settings = member.isObject() ? readSettings(member.toObject())
                                          : std::nullopt;
  if (!settings || !validateCoreSystemSettings(*settings)) {
    return invalidResult("The system settings values are invalid.");
  }
  return {.status = {}, .settings = *settings};
}

PersistenceStatus SystemSettingsStore::save(
  const CoreSystemSettings& settings) const
{
  if (!validateCoreSystemSettings(settings)) {
    return invalid("Invalid system settings cannot be saved.");
  }
  const QJsonObject system{
    {QStringLiteral("hardware"), static_cast<int>(settings.hardware)},
    {QStringLiteral("region"), static_cast<int>(settings.region)},
    {QStringLiteral("videoStandard"),
      static_cast<int>(settings.videoStandard)},
    {QStringLiteral("masterClock"), static_cast<int>(settings.masterClock)},
    {QStringLiteral("emulateIllegalAccessLockups"),
      settings.emulateIllegalAccessLockups},
    {QStringLiteral("enableAddressErrors"), settings.enableAddressErrors},
  };
  const auto data = QJsonDocument{QJsonObject{
    {QStringLiteral("schemaVersion"), static_cast<int>(schemaVersion)},
    {QStringLiteral("system"), system},
  }}.toJson(QJsonDocument::Indented);
  return writeFileAtomically(path_,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(data.constData()),
      static_cast<std::size_t>(data.size())}, maximumFileBytes);
}

} // namespace genplusgx::settings
