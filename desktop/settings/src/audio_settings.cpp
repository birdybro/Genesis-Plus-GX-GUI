#include "genplusgx/settings/audio_settings.h"

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

template<typename Enum>
std::optional<Enum> enumeration(const QJsonObject& object, const char* key)
{
  const auto value = integer(object, key);
  return value && *value >= 0 ? std::optional{static_cast<Enum>(*value)}
                              : std::nullopt;
}

std::optional<bool> boolean(const QJsonObject& object, const char* key)
{
  const auto member = object.value(QString::fromLatin1(key));
  return member.isBool() ? std::optional{member.toBool()} : std::nullopt;
}

AudioSettingsLoadResult invalidResult(std::string message)
{
  return {
    .status = invalid(std::move(message)),
    .settings = defaultAudioSettings(),
  };
}

std::optional<AudioSettings> readCurrent(const QJsonObject& object)
{
  const auto volume = integer(object, "masterVolumePercent");
  const auto muted = boolean(object, "muted");
  const auto latency = integer(object, "latencyMilliseconds");
  const auto device = object.value(QStringLiteral("outputDeviceName"));
  const auto coreMember = object.value(QStringLiteral("core"));
  if (!volume || !muted || !latency || !device.isString() ||
      !coreMember.isObject()) {
    return std::nullopt;
  }
  const auto core = coreMember.toObject();
  const auto output = enumeration<CoreSoundOutput>(core, "output");
  const auto filter = enumeration<CoreAudioFilter>(core, "filter");
  const auto ym2612 = enumeration<CoreYm2612Core>(core, "ym2612Core");
  const auto ym2413Mode = enumeration<CoreYm2413Mode>(core, "ym2413Mode");
  const auto ym2413Core = enumeration<CoreYm2413Core>(core, "ym2413Core");
  const auto psg = integer(core, "psgLevelPercent");
  const auto fm = integer(core, "fmLevelPercent");
  const auto cdda = integer(core, "cddaLevelPercent");
  const auto pcm = integer(core, "pcmLevelPercent");
  const auto lowPass = integer(core, "lowPassPercent");
  const auto eqLow = integer(core, "equalizerLowPercent");
  const auto eqMid = integer(core, "equalizerMidPercent");
  const auto eqHigh = integer(core, "equalizerHighPercent");
  const auto hqFm = boolean(core, "highQualityFm");
  const auto hqPsg = boolean(core, "highQualityPsg");
  if (!output || !filter || !ym2612 || !ym2413Mode || !ym2413Core ||
      !psg || !fm || !cdda || !pcm || !lowPass || !eqLow || !eqMid ||
      !eqHigh || !hqFm || !hqPsg) {
    return std::nullopt;
  }
  return AudioSettings{
    .masterVolumePercent = *volume,
    .muted = *muted,
    .latencyMilliseconds = *latency,
    .outputDeviceName = device.toString().toStdString(),
    .core = {
      .output = *output,
      .filter = *filter,
      .ym2612Core = *ym2612,
      .ym2413Mode = *ym2413Mode,
      .ym2413Core = *ym2413Core,
      .psgLevelPercent = *psg,
      .fmLevelPercent = *fm,
      .cddaLevelPercent = *cdda,
      .pcmLevelPercent = *pcm,
      .lowPassPercent = *lowPass,
      .equalizerLowPercent = *eqLow,
      .equalizerMidPercent = *eqMid,
      .equalizerHighPercent = *eqHigh,
      .highQualityFm = *hqFm,
      .highQualityPsg = *hqPsg,
    },
  };
}

QJsonObject coreObject(const CoreAudioSettings& core)
{
  return {
    {QStringLiteral("output"), static_cast<int>(core.output)},
    {QStringLiteral("filter"), static_cast<int>(core.filter)},
    {QStringLiteral("ym2612Core"), static_cast<int>(core.ym2612Core)},
    {QStringLiteral("ym2413Mode"), static_cast<int>(core.ym2413Mode)},
    {QStringLiteral("ym2413Core"), static_cast<int>(core.ym2413Core)},
    {QStringLiteral("psgLevelPercent"), core.psgLevelPercent},
    {QStringLiteral("fmLevelPercent"), core.fmLevelPercent},
    {QStringLiteral("cddaLevelPercent"), core.cddaLevelPercent},
    {QStringLiteral("pcmLevelPercent"), core.pcmLevelPercent},
    {QStringLiteral("lowPassPercent"), core.lowPassPercent},
    {QStringLiteral("equalizerLowPercent"), core.equalizerLowPercent},
    {QStringLiteral("equalizerMidPercent"), core.equalizerMidPercent},
    {QStringLiteral("equalizerHighPercent"), core.equalizerHighPercent},
    {QStringLiteral("highQualityFm"), core.highQualityFm},
    {QStringLiteral("highQualityPsg"), core.highQualityPsg},
  };
}

} // namespace

AudioSettings defaultAudioSettings() noexcept
{
  return {};
}

bool validateAudioSettings(const AudioSettings& settings) noexcept
{
  return settings.masterVolumePercent >= 0 &&
    settings.masterVolumePercent <= 100 &&
    settings.latencyMilliseconds >= 10 && settings.latencyMilliseconds <= 500 &&
    settings.outputDeviceName.size() <= 1'024U &&
    validateCoreAudioSettings(settings.core);
}

AudioSettingsStore::AudioSettingsStore(std::filesystem::path path)
  : path_(std::move(path))
{
}

const std::filesystem::path& AudioSettingsStore::path() const noexcept
{
  return path_;
}

AudioSettingsLoadResult AudioSettingsStore::load() const
{
  const auto loaded = readFileBounded(path_, maximumFileBytes);
  if (!loaded.status) {
    return {.status = loaded.status, .settings = defaultAudioSettings()};
  }
  if (!loaded.exists) {
    return {.status = {}, .settings = defaultAudioSettings()};
  }
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(
    QByteArray{reinterpret_cast<const char*>(loaded.data.data()),
      static_cast<qsizetype>(loaded.data.size())}, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return invalidResult("The audio settings file is not valid JSON.");
  }
  const auto root = document.object();
  const auto schema = integer(root, "schemaVersion");
  if (!schema) {
    return invalidResult("The audio settings schema version is missing.");
  }
  if (*schema == 0) {
    auto migrated = defaultAudioSettings();
    const auto volume = integer(root, "volume");
    const auto muted = boolean(root, "mute");
    const auto latency = integer(root, "latency");
    if (!volume || !muted || !latency) {
      return invalidResult("The legacy audio settings are invalid.");
    }
    migrated.masterVolumePercent = *volume;
    migrated.muted = *muted;
    migrated.latencyMilliseconds = *latency;
    if (!validateAudioSettings(migrated)) {
      return invalidResult("The legacy audio settings values are invalid.");
    }
    return {.status = {}, .settings = migrated, .migrated = true};
  }
  if (*schema != static_cast<int>(schemaVersion)) {
    return invalidResult("The audio settings schema version is not supported.");
  }
  const auto member = root.value(QStringLiteral("audio"));
  const auto value = member.isObject() ? readCurrent(member.toObject())
                                       : std::nullopt;
  if (!value || !validateAudioSettings(*value)) {
    return invalidResult("The audio settings values are invalid.");
  }
  return {.status = {}, .settings = *value};
}

PersistenceStatus AudioSettingsStore::save(const AudioSettings& settings) const
{
  if (!validateAudioSettings(settings)) {
    return invalid("Invalid audio settings cannot be saved.");
  }
  const QJsonObject audio{
    {QStringLiteral("masterVolumePercent"), settings.masterVolumePercent},
    {QStringLiteral("muted"), settings.muted},
    {QStringLiteral("latencyMilliseconds"), settings.latencyMilliseconds},
    {QStringLiteral("outputDeviceName"),
      QString::fromStdString(settings.outputDeviceName)},
    {QStringLiteral("core"), coreObject(settings.core)},
  };
  const auto data = QJsonDocument{QJsonObject{
    {QStringLiteral("schemaVersion"), static_cast<int>(schemaVersion)},
    {QStringLiteral("audio"), audio},
  }}.toJson(QJsonDocument::Indented);
  return writeFileAtomically(path_,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(data.constData()),
      static_cast<std::size_t>(data.size())}, maximumFileBytes);
}

} // namespace genplusgx::settings
