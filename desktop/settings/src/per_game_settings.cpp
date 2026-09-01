#include "genplusgx/settings/per_game_settings.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <system_error>
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

PerGameSettingsLoadResult invalidResult(std::string message)
{
  return {
    .status = invalid(std::move(message)),
    .exists = true,
    .settings = {},
  };
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

template <typename Enum>
std::optional<Enum> enumeration(const QJsonObject& object, const char* key)
{
  const auto value = integer(object, key);
  return value && *value >= 0 ? std::optional{static_cast<Enum>(*value)} : std::nullopt;
}

std::optional<bool> boolean(const QJsonObject& object, const char* key)
{
  const auto value = object.value(QString::fromLatin1(key));
  return value.isBool() ? std::optional{value.toBool()} : std::nullopt;
}

QString fromPath(const std::filesystem::path& path)
{
#ifdef _WIN32
  return QString::fromStdWString(path.wstring());
#else
  const auto bytes = path.u8string();
  return QString::fromUtf8(reinterpret_cast<const char*>(bytes.data()),
    static_cast<qsizetype>(bytes.size()));
#endif
}

std::filesystem::path toPath(const QString& path)
{
#ifdef _WIN32
  return std::filesystem::path{path.toStdWString()};
#else
  const auto bytes = path.toUtf8();
  return std::filesystem::path{bytes.constData()};
#endif
}

std::optional<video::ShaderConfiguration> readShader(const QJsonObject& object)
{
  const auto mode = enumeration<video::ShaderMode>(object, "mode");
  const auto path = object.value(QStringLiteral("presetPath"));
  const auto parameterArray = object.value(QStringLiteral("parameters"));
  if (!mode || !path.isString() || !parameterArray.isArray()) {
    return std::nullopt;
  }
  video::ShaderConfiguration shader{
    .mode = *mode,
    .presetPath = toPath(path.toString()),
    .parameters = {},
  };
  for (const auto& parameterValue : parameterArray.toArray()) {
    if (!parameterValue.isObject()) {
      return std::nullopt;
    }
    const auto parameter = parameterValue.toObject();
    const auto name = parameter.value(QStringLiteral("name"));
    const auto value = parameter.value(QStringLiteral("value"));
    if (!name.isString() || !value.isDouble()) {
      return std::nullopt;
    }
    shader.parameters.push_back({
      .name = name.toString().toStdString(),
      .value = static_cast<float>(value.toDouble()),
    });
  }
  return video::validateShaderConfiguration(shader) ? std::optional{shader}
                                                     : std::nullopt;
}

QJsonObject writeShader(const video::ShaderConfiguration& shader)
{
  QJsonArray parameters;
  for (const auto& parameter : shader.parameters) {
    parameters.push_back(QJsonObject{
      {QStringLiteral("name"), QString::fromStdString(parameter.name)},
      {QStringLiteral("value"), static_cast<double>(parameter.value)},
    });
  }
  return {
    {QStringLiteral("mode"), static_cast<int>(shader.mode)},
    {QStringLiteral("presetPath"), fromPath(shader.presetPath)},
    {QStringLiteral("parameters"), parameters},
  };
}

std::optional<VideoSettings> readVideo(const QJsonObject& object)
{
  const auto aspect = enumeration<video::AspectMode>(object, "aspect");
  const auto scaling = enumeration<video::ScaleMode>(object, "scaling");
  const auto presentation =
    enumeration<video::VideoFilter>(object, "presentationFilter");
  const auto overscan = enumeration<CoreOverscanMode>(object, "overscan");
  const auto ntsc = enumeration<CoreNtscFilter>(object, "ntscFilter");
  const auto interlaced =
    enumeration<CoreInterlacedRenderMode>(object, "interlacedRender");
  const auto gameGear = boolean(object, "gameGearExtendedScreen");
  if (!aspect || !scaling || !presentation || !overscan || !ntsc || !interlaced ||
      !gameGear) {
    return std::nullopt;
  }
  auto shader = video::ShaderConfiguration{};
  if (object.contains(QStringLiteral("shader"))) {
    const auto shaderValue = object.value(QStringLiteral("shader"));
    const auto parsed = shaderValue.isObject()
      ? readShader(shaderValue.toObject()) : std::nullopt;
    if (!parsed) {
      return std::nullopt;
    }
    shader = *parsed;
  }
  auto presentationConfiguration = video::PresentationConfiguration{};
  const bool hasSync = object.contains(QStringLiteral("presentationSync"));
  const bool hasBuffering =
    object.contains(QStringLiteral("presentationBuffering"));
  if (hasSync != hasBuffering) {
    return std::nullopt;
  }
  if (hasSync) {
    const auto sync =
      enumeration<video::PresentationSyncMode>(object, "presentationSync");
    const auto buffering = enumeration<video::PresentationBufferingMode>(
      object, "presentationBuffering");
    if (!sync || !buffering) {
      return std::nullopt;
    }
    presentationConfiguration = {
      .sync = *sync,
      .buffering = *buffering,
    };
  }
  VideoSettings value{
    .aspect = *aspect,
    .scaling = *scaling,
    .presentationFilter = *presentation,
    .presentation = presentationConfiguration,
    .shader = std::move(shader),
    .core =
      {
        .overscan = *overscan,
        .ntscFilter = *ntsc,
        .interlacedRender = *interlaced,
        .gameGearExtendedScreen = *gameGear,
      },
  };
  return validateVideoSettings(value) ? std::optional{value} : std::nullopt;
}

QJsonObject writeVideo(const VideoSettings& value)
{
  return {
    {QStringLiteral("aspect"), static_cast<int>(value.aspect)},
    {QStringLiteral("scaling"), static_cast<int>(value.scaling)},
    {QStringLiteral("presentationFilter"), static_cast<int>(value.presentationFilter)},
    {QStringLiteral("presentationSync"),
      static_cast<int>(value.presentation.sync)},
    {QStringLiteral("presentationBuffering"),
      static_cast<int>(value.presentation.buffering)},
    {QStringLiteral("shader"), writeShader(value.shader)},
    {QStringLiteral("overscan"), static_cast<int>(value.core.overscan)},
    {QStringLiteral("ntscFilter"), static_cast<int>(value.core.ntscFilter)},
    {QStringLiteral("interlacedRender"), static_cast<int>(value.core.interlacedRender)},
    {QStringLiteral("gameGearExtendedScreen"), value.core.gameGearExtendedScreen},
  };
}

std::optional<AudioSettings> readAudio(const QJsonObject& object)
{
  const auto volume = integer(object, "masterVolumePercent");
  const auto muted = boolean(object, "muted");
  const auto latency = integer(object, "latencyMilliseconds");
  const auto device = object.value(QStringLiteral("outputDeviceName"));
  const auto coreMember = object.value(QStringLiteral("core"));
  if (!volume || !muted || !latency || !device.isString() || !coreMember.isObject()) {
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
  if (!output || !filter || !ym2612 || !ym2413Mode || !ym2413Core || !psg || !fm ||
      !cdda || !pcm || !lowPass || !eqLow || !eqMid || !eqHigh || !hqFm || !hqPsg) {
    return std::nullopt;
  }
  AudioSettings value{
    .masterVolumePercent = *volume,
    .muted = *muted,
    .latencyMilliseconds = *latency,
    .outputDeviceName = device.toString().toStdString(),
    .core =
      {
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
  return validateAudioSettings(value) ? std::optional{value} : std::nullopt;
}

QJsonObject writeAudioCore(const CoreAudioSettings& value)
{
  return {
    {QStringLiteral("output"), static_cast<int>(value.output)},
    {QStringLiteral("filter"), static_cast<int>(value.filter)},
    {QStringLiteral("ym2612Core"), static_cast<int>(value.ym2612Core)},
    {QStringLiteral("ym2413Mode"), static_cast<int>(value.ym2413Mode)},
    {QStringLiteral("ym2413Core"), static_cast<int>(value.ym2413Core)},
    {QStringLiteral("psgLevelPercent"), value.psgLevelPercent},
    {QStringLiteral("fmLevelPercent"), value.fmLevelPercent},
    {QStringLiteral("cddaLevelPercent"), value.cddaLevelPercent},
    {QStringLiteral("pcmLevelPercent"), value.pcmLevelPercent},
    {QStringLiteral("lowPassPercent"), value.lowPassPercent},
    {QStringLiteral("equalizerLowPercent"), value.equalizerLowPercent},
    {QStringLiteral("equalizerMidPercent"), value.equalizerMidPercent},
    {QStringLiteral("equalizerHighPercent"), value.equalizerHighPercent},
    {QStringLiteral("highQualityFm"), value.highQualityFm},
    {QStringLiteral("highQualityPsg"), value.highQualityPsg},
  };
}

QJsonObject writeAudio(const AudioSettings& value)
{
  return {
    {QStringLiteral("masterVolumePercent"), value.masterVolumePercent},
    {QStringLiteral("muted"), value.muted},
    {QStringLiteral("latencyMilliseconds"), value.latencyMilliseconds},
    {QStringLiteral("outputDeviceName"),
      QString::fromStdString(value.outputDeviceName)},
    {QStringLiteral("core"), writeAudioCore(value.core)},
  };
}

std::optional<CoreSystemSettings> readSystem(const QJsonObject& object)
{
  const auto hardware = enumeration<CoreSystemHardware>(object, "hardware");
  const auto region = enumeration<CoreSystemRegion>(object, "region");
  const auto standard = enumeration<CoreVideoStandard>(object, "videoStandard");
  const auto clock = enumeration<CoreMasterClock>(object, "masterClock");
  const auto lockups = boolean(object, "emulateIllegalAccessLockups");
  const auto addressErrors = boolean(object, "enableAddressErrors");
  if (!hardware || !region || !standard || !clock || !lockups || !addressErrors) {
    return std::nullopt;
  }
  CoreSystemSettings value{
    .hardware = *hardware,
    .region = *region,
    .videoStandard = *standard,
    .masterClock = *clock,
    .emulateIllegalAccessLockups = *lockups,
    .enableAddressErrors = *addressErrors,
  };
  return validateCoreSystemSettings(value) ? std::optional{value} : std::nullopt;
}

QJsonObject writeSystem(const CoreSystemSettings& value)
{
  return {
    {QStringLiteral("hardware"), static_cast<int>(value.hardware)},
    {QStringLiteral("region"), static_cast<int>(value.region)},
    {QStringLiteral("videoStandard"), static_cast<int>(value.videoStandard)},
    {QStringLiteral("masterClock"), static_cast<int>(value.masterClock)},
    {QStringLiteral("emulateIllegalAccessLockups"), value.emulateIllegalAccessLockups},
    {QStringLiteral("enableAddressErrors"), value.enableAddressErrors},
  };
}

QString pathString(const std::filesystem::path& path)
{
#if defined(_WIN32)
  return QString::fromStdWString(path.native());
#else
  return QString::fromUtf8(path.native());
#endif
}

std::filesystem::path filesystemPath(const QString& path)
{
#if defined(_WIN32)
  return std::filesystem::path{path.toStdWString()};
#else
  return std::filesystem::path{path.toUtf8().toStdString()};
#endif
}

std::optional<platform::BiosConfiguration> readBios(const QJsonObject& object)
{
  platform::BiosConfiguration value;
  for (const auto& descriptor : platform::biosDescriptors()) {
    const auto member = object.value(QString::fromLatin1(
      descriptor.key.data(), static_cast<qsizetype>(descriptor.key.size())));
    if (!member.isString()) {
      return std::nullopt;
    }
    value.setPath(descriptor.slot, filesystemPath(member.toString()));
  }
  return value;
}

QJsonObject writeBios(const platform::BiosConfiguration& value)
{
  QJsonObject result;
  for (const auto& descriptor : platform::biosDescriptors()) {
    result.insert(QString::fromLatin1(descriptor.key.data(),
                    static_cast<qsizetype>(descriptor.key.size())),
      pathString(value.path(descriptor.slot)));
  }
  return result;
}

bool validInputProfile(std::string_view value) noexcept
{
  if (value.empty() || value.size() > PerGameSettingsStore::maximumInputProfileBytes) {
    return false;
  }
  return std::ranges::none_of(value,
    [](unsigned char character) { return character < 0x20U || character == 0x7fU; });
}

bool validBios(const platform::BiosConfiguration& value) noexcept
{
  return std::ranges::all_of(
    platform::biosDescriptors(), [&value](const auto& descriptor) {
      const auto& path = value.path(descriptor.slot);
      return path.empty() ||
             (path.is_absolute() &&
               path.native().size() <= PerGameSettingsStore::maximumBiosPathBytes);
    });
}

} // namespace

bool PerGameSettings::empty() const noexcept
{
  return !video && !audio && !system && !inputProfile && !bios;
}

bool validatePerGameSettings(const PerGameSettings& settings) noexcept
{
  return (!settings.video || validateVideoSettings(*settings.video)) &&
         (!settings.audio || validateAudioSettings(*settings.audio)) &&
         (!settings.system || validateCoreSystemSettings(*settings.system)) &&
         (!settings.inputProfile || validInputProfile(*settings.inputProfile)) &&
         (!settings.bios || validBios(*settings.bios));
}

EffectiveGameSettings resolvePerGameSettings(
  const GlobalGameSettings& global, const PerGameSettings& overrides) noexcept
{
  EffectiveGameSettings effective{
    .video = overrides.video.value_or(global.video),
    .audio = overrides.audio.value_or(global.audio),
    .system = overrides.system.value_or(global.system),
    .inputProfile = overrides.inputProfile.value_or(global.inputProfile),
    .bios = overrides.bios.value_or(global.bios),
  };
  // Device selection and ring capacity are process-wide host resources.
  effective.audio.latencyMilliseconds = global.audio.latencyMilliseconds;
  effective.audio.outputDeviceName = global.audio.outputDeviceName;
  return effective;
}

AudioSettingsLayerUpdate planAudioSettingsLayerUpdate(
  const AudioSettings& global,
  const std::optional<AudioSettings>& currentPerGame,
  const AudioSettings& requested) noexcept
{
  if (!currentPerGame) {
    return {.global = requested, .perGame = std::nullopt};
  }
  auto updatedGlobal = global;
  updatedGlobal.latencyMilliseconds = requested.latencyMilliseconds;
  updatedGlobal.outputDeviceName = requested.outputDeviceName;
  auto updatedPerGame = requested;
  updatedPerGame.latencyMilliseconds = currentPerGame->latencyMilliseconds;
  updatedPerGame.outputDeviceName = currentPerGame->outputDeviceName;
  return {
    .global = std::move(updatedGlobal),
    .perGame = std::move(updatedPerGame),
  };
}

PerGameSettingsStore::PerGameSettingsStore(std::filesystem::path root)
    : root_(std::move(root))
{
}

const std::filesystem::path& PerGameSettingsStore::root() const noexcept
{
  return root_;
}

std::filesystem::path PerGameSettingsStore::pathFor(const GameIdentity& identity) const
{
  if (!identity.valid() || root_.empty() || !root_.is_absolute()) {
    return {};
  }
  return root_ / (identity.directoryName() + ".json");
}

PerGameSettingsLoadResult PerGameSettingsStore::load(const GameIdentity& identity) const
{
  const auto path = pathFor(identity);
  if (path.empty()) {
    return {
      .status =
        {
          .error = PersistenceError::invalidGameIdentity,
          .message = "Per-game settings require a valid identity and absolute root.",
        },
      .exists = false,
      .settings = {},
    };
  }
  const auto loaded = readFileBounded(path, maximumFileBytes);
  if (!loaded.status) {
    return {.status = loaded.status, .exists = false, .settings = {}};
  }
  if (!loaded.exists) {
    return {.status = {}, .exists = false, .settings = {}};
  }
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(
    QByteArray{reinterpret_cast<const char*>(loaded.data.data()),
      static_cast<qsizetype>(loaded.data.size())},
    &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return invalidResult("The per-game settings file is not valid JSON.");
  }
  const auto root = document.object();
  const auto schema = integer(root, "schemaVersion");
  if (!schema || *schema != static_cast<int>(schemaVersion)) {
    return invalidResult(
      "The per-game settings schema version is missing or unsupported.");
  }
  const auto gameMember = root.value(QStringLiteral("game"));
  const auto overridesMember = root.value(QStringLiteral("overrides"));
  if (!gameMember.isObject() || !overridesMember.isObject()) {
    return invalidResult("The per-game settings identity or overrides are missing.");
  }
  const auto game = gameMember.toObject();
  const auto hash = game.value(QStringLiteral("sha256"));
  const auto slug = game.value(QStringLiteral("titleSlug"));
  if (!hash.isString() || !slug.isString() ||
      hash.toString().toStdString() != identity.sha256 ||
      slug.toString().toStdString() != identity.titleSlug) {
    return invalidResult("The per-game settings belong to a different game.");
  }

  const auto object = overridesMember.toObject();
  PerGameSettings settings;
  if (object.contains(QStringLiteral("video"))) {
    const auto member = object.value(QStringLiteral("video"));
    if (!member.isObject() || !(settings.video = readVideo(member.toObject()))) {
      return invalidResult("The per-game video override is invalid.");
    }
  }
  if (object.contains(QStringLiteral("audio"))) {
    const auto member = object.value(QStringLiteral("audio"));
    if (!member.isObject() || !(settings.audio = readAudio(member.toObject()))) {
      return invalidResult("The per-game audio override is invalid.");
    }
  }
  if (object.contains(QStringLiteral("system"))) {
    const auto member = object.value(QStringLiteral("system"));
    if (!member.isObject() || !(settings.system = readSystem(member.toObject()))) {
      return invalidResult("The per-game system override is invalid.");
    }
  }
  if (object.contains(QStringLiteral("inputProfile"))) {
    const auto member = object.value(QStringLiteral("inputProfile"));
    if (!member.isString()) {
      return invalidResult("The per-game input profile override is invalid.");
    }
    settings.inputProfile = member.toString().toStdString();
  }
  if (object.contains(QStringLiteral("bios"))) {
    const auto member = object.value(QStringLiteral("bios"));
    if (!member.isObject() || !(settings.bios = readBios(member.toObject()))) {
      return invalidResult("The per-game BIOS override is invalid.");
    }
  }
  if (settings.empty() || !validatePerGameSettings(settings)) {
    return invalidResult("The per-game settings values are invalid or empty.");
  }
  return {.status = {}, .exists = true, .settings = std::move(settings)};
}

PersistenceStatus PerGameSettingsStore::save(
  const GameIdentity& identity, const PerGameSettings& settings) const
{
  const auto path = pathFor(identity);
  if (path.empty()) {
    return {
      .error = PersistenceError::invalidGameIdentity,
      .message = "Per-game settings require a valid identity and absolute root.",
    };
  }
  if (!validatePerGameSettings(settings)) {
    return invalid("Invalid per-game settings cannot be saved.");
  }
  if (settings.empty()) {
    std::error_code error;
    static_cast<void>(std::filesystem::remove(path, error));
    if (error) {
      return {
        .error = PersistenceError::fileCommitFailed,
        .message =
          "The empty per-game override file could not be removed: " + error.message(),
      };
    }
    return {};
  }

  QJsonObject overrides;
  if (settings.video) {
    overrides.insert(QStringLiteral("video"), writeVideo(*settings.video));
  }
  if (settings.audio) {
    overrides.insert(QStringLiteral("audio"), writeAudio(*settings.audio));
  }
  if (settings.system) {
    overrides.insert(QStringLiteral("system"), writeSystem(*settings.system));
  }
  if (settings.inputProfile) {
    overrides.insert(
      QStringLiteral("inputProfile"), QString::fromStdString(*settings.inputProfile));
  }
  if (settings.bios) {
    overrides.insert(QStringLiteral("bios"), writeBios(*settings.bios));
  }
  const auto data = QJsonDocument{
    QJsonObject{
      {QStringLiteral("schemaVersion"), static_cast<int>(schemaVersion)},
      {QStringLiteral("game"),
        QJsonObject{
          {QStringLiteral("sha256"), QString::fromStdString(identity.sha256)},
          {QStringLiteral("titleSlug"), QString::fromStdString(identity.titleSlug)},
        }},
      {QStringLiteral("overrides"), overrides},
    }}.toJson(QJsonDocument::Indented);
  return writeFileAtomically(path,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(data.constData()),
      static_cast<std::size_t>(data.size())},
    maximumFileBytes);
}

} // namespace genplusgx::settings
