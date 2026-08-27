#include "genplusgx/settings/video_settings.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <optional>
#include <initializer_list>
#include <string_view>
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

template<typename Value>
std::optional<Value> enumFromString(
  const QJsonObject& object,
  const QString& key,
  std::initializer_list<std::pair<std::string_view, Value>> values)
{
  const auto member = object.value(key);
  if (!member.isString()) {
    return std::nullopt;
  }
  const auto text = member.toString();
  for (const auto& [name, value] : values) {
    if (text == QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()))) {
      return value;
    }
  }
  return std::nullopt;
}

QString aspectName(video::AspectMode mode)
{
  switch (mode) {
    case video::AspectMode::native: return QStringLiteral("native");
    case video::AspectMode::fourThree: return QStringLiteral("four-three");
    case video::AspectMode::stretch: return QStringLiteral("stretch");
  }
  return {};
}

QString scalingName(video::ScaleMode mode)
{
  switch (mode) {
    case video::ScaleMode::fit: return QStringLiteral("fit");
    case video::ScaleMode::integer: return QStringLiteral("integer");
  }
  return {};
}

QString presentationFilterName(video::VideoFilter filter)
{
  switch (filter) {
    case video::VideoFilter::nearest: return QStringLiteral("nearest");
    case video::VideoFilter::bilinear: return QStringLiteral("bilinear");
  }
  return {};
}

QString shaderModeName(video::ShaderMode mode)
{
  switch (mode) {
    case video::ShaderMode::disabled: return QStringLiteral("disabled");
    case video::ShaderMode::builtinCrt: return QStringLiteral("builtin-crt");
    case video::ShaderMode::libretroPreset:
      return QStringLiteral("libretro-preset");
  }
  return {};
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

std::optional<video::ShaderConfiguration> readShader(
  const QJsonObject& videoObject)
{
  const auto shaderObjectValue = videoObject.value(QStringLiteral("shader"));
  if (!shaderObjectValue.isObject()) {
    return std::nullopt;
  }
  const auto shaderObject = shaderObjectValue.toObject();
  const auto mode = enumFromString<video::ShaderMode>(shaderObject,
    QStringLiteral("mode"),
    {{"disabled", video::ShaderMode::disabled},
      {"builtin-crt", video::ShaderMode::builtinCrt},
      {"libretro-preset", video::ShaderMode::libretroPreset}});
  const auto pathValue = shaderObject.value(QStringLiteral("presetPath"));
  const auto parametersValue = shaderObject.value(QStringLiteral("parameters"));
  if (!mode || !pathValue.isString() || !parametersValue.isArray()) {
    return std::nullopt;
  }
  video::ShaderConfiguration shader{
    .mode = *mode,
    .presetPath = toPath(pathValue.toString()),
    .parameters = {},
  };
  const auto parameters = parametersValue.toArray();
  shader.parameters.reserve(static_cast<std::size_t>(parameters.size()));
  for (const auto& parameterValue : parameters) {
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
  return shader;
}

QString overscanName(CoreOverscanMode mode)
{
  switch (mode) {
    case CoreOverscanMode::disabled: return QStringLiteral("disabled");
    case CoreOverscanMode::vertical: return QStringLiteral("top-bottom");
    case CoreOverscanMode::horizontal: return QStringLiteral("left-right");
    case CoreOverscanMode::full: return QStringLiteral("full");
  }
  return {};
}

QString ntscName(CoreNtscFilter filter)
{
  switch (filter) {
    case CoreNtscFilter::disabled: return QStringLiteral("disabled");
    case CoreNtscFilter::monochrome: return QStringLiteral("monochrome");
    case CoreNtscFilter::composite: return QStringLiteral("composite");
    case CoreNtscFilter::sVideo: return QStringLiteral("s-video");
    case CoreNtscFilter::rgb: return QStringLiteral("rgb");
  }
  return {};
}

QString interlacedRenderName(CoreInterlacedRenderMode mode)
{
  switch (mode) {
    case CoreInterlacedRenderMode::singleField:
      return QStringLiteral("single-field");
    case CoreInterlacedRenderMode::doubleField:
      return QStringLiteral("double-field");
  }
  return {};
}

VideoSettingsLoadResult invalidResult(std::string message)
{
  return {
    .status = invalid(std::move(message)),
    .settings = defaultVideoSettings(),
    .migrated = false,
  };
}

std::optional<VideoSettings> readCurrent(
  const QJsonObject& videoObject,
  bool hasShader)
{
  const auto aspect = enumFromString<video::AspectMode>(videoObject,
    QStringLiteral("aspect"), {{"native", video::AspectMode::native},
      {"four-three", video::AspectMode::fourThree},
      {"stretch", video::AspectMode::stretch}});
  const auto scaling = enumFromString<video::ScaleMode>(videoObject,
    QStringLiteral("scaling"), {{"fit", video::ScaleMode::fit},
      {"integer", video::ScaleMode::integer}});
  const auto presentation = enumFromString<video::VideoFilter>(videoObject,
    QStringLiteral("presentationFilter"),
    {{"nearest", video::VideoFilter::nearest},
      {"bilinear", video::VideoFilter::bilinear}});
  const auto overscan = enumFromString<CoreOverscanMode>(videoObject,
    QStringLiteral("overscan"),
    {{"disabled", CoreOverscanMode::disabled},
      {"top-bottom", CoreOverscanMode::vertical},
      {"left-right", CoreOverscanMode::horizontal},
      {"full", CoreOverscanMode::full}});
  const auto ntsc = enumFromString<CoreNtscFilter>(videoObject,
    QStringLiteral("ntscFilter"),
    {{"disabled", CoreNtscFilter::disabled},
      {"monochrome", CoreNtscFilter::monochrome},
      {"composite", CoreNtscFilter::composite},
      {"s-video", CoreNtscFilter::sVideo},
      {"rgb", CoreNtscFilter::rgb}});
  const auto render = enumFromString<CoreInterlacedRenderMode>(videoObject,
    QStringLiteral("interlacedRender"),
    {{"single-field", CoreInterlacedRenderMode::singleField},
      {"double-field", CoreInterlacedRenderMode::doubleField}});
  const auto gameGear = videoObject.value(QStringLiteral("gameGearExtendedScreen"));
  if (!aspect || !scaling || !presentation || !overscan || !ntsc || !render ||
      !gameGear.isBool()) {
    return std::nullopt;
  }
  const auto shader = hasShader
    ? readShader(videoObject)
    : std::optional<video::ShaderConfiguration>{video::ShaderConfiguration{}};
  if (!shader) {
    return std::nullopt;
  }
  return VideoSettings{
    .aspect = *aspect,
    .scaling = *scaling,
    .presentationFilter = *presentation,
    .shader = *shader,
    .core = {
      .overscan = *overscan,
      .ntscFilter = *ntsc,
      .interlacedRender = *render,
      .gameGearExtendedScreen = gameGear.toBool(),
    },
  };
}

std::optional<VideoSettings> readLegacy(const QJsonObject& root)
{
  const auto integerScale = root.value(QStringLiteral("integerScale"));
  const auto forceFourThree = root.value(QStringLiteral("forceFourThree"));
  const auto bilinear = root.value(QStringLiteral("bilinear"));
  const auto overscan = root.value(QStringLiteral("overscan"));
  const auto ntscComposite = root.value(QStringLiteral("ntscComposite"));
  const auto gameGear = root.value(QStringLiteral("gameGearExtendedScreen"));
  const auto doubleField = root.value(QStringLiteral("doubleField"));
  if (!integerScale.isBool() || !forceFourThree.isBool() || !bilinear.isBool() ||
      !overscan.isDouble() || !ntscComposite.isBool() || !gameGear.isBool() ||
      !doubleField.isBool()) {
    return std::nullopt;
  }
  const auto overscanValue = overscan.toInt(-1);
  if (overscanValue < 0 || overscanValue > 3) {
    return std::nullopt;
  }
  return VideoSettings{
    .aspect = forceFourThree.toBool() ? video::AspectMode::fourThree
                                     : video::AspectMode::native,
    .scaling = integerScale.toBool() ? video::ScaleMode::integer
                                     : video::ScaleMode::fit,
    .presentationFilter = bilinear.toBool() ? video::VideoFilter::bilinear
                                            : video::VideoFilter::nearest,
    .shader = {},
    .core = {
      .overscan = static_cast<CoreOverscanMode>(overscanValue),
      .ntscFilter = ntscComposite.toBool() ? CoreNtscFilter::composite
                                           : CoreNtscFilter::disabled,
      .interlacedRender = doubleField.toBool()
        ? CoreInterlacedRenderMode::doubleField
        : CoreInterlacedRenderMode::singleField,
      .gameGearExtendedScreen = gameGear.toBool(),
    },
  };
}

} // namespace

VideoSettings defaultVideoSettings() noexcept
{
  return {};
}

bool validateVideoSettings(const VideoSettings& settings) noexcept
{
  return static_cast<unsigned>(settings.aspect) <=
      static_cast<unsigned>(video::AspectMode::stretch) &&
    static_cast<unsigned>(settings.scaling) <=
      static_cast<unsigned>(video::ScaleMode::integer) &&
    static_cast<unsigned>(settings.presentationFilter) <=
      static_cast<unsigned>(video::VideoFilter::bilinear) &&
    video::validateShaderConfiguration(settings.shader) &&
    validateCoreVideoSettings(settings.core);
}

VideoSettingsStore::VideoSettingsStore(std::filesystem::path path)
  : path_(std::move(path))
{
}

const std::filesystem::path& VideoSettingsStore::path() const noexcept
{
  return path_;
}

VideoSettingsLoadResult VideoSettingsStore::load() const
{
  const auto loaded = readFileBounded(path_, maximumFileBytes);
  if (!loaded.status) {
    return {.status = loaded.status, .settings = defaultVideoSettings()};
  }
  if (!loaded.exists) {
    return {.status = {}, .settings = defaultVideoSettings(), .migrated = false};
  }

  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(
    QByteArray{reinterpret_cast<const char*>(loaded.data.data()),
      static_cast<qsizetype>(loaded.data.size())}, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return invalidResult("The video settings file is not valid JSON.");
  }
  const auto root = document.object();
  const auto schema = root.value(QStringLiteral("schemaVersion"));
  if (!schema.isDouble()) {
    return invalidResult("The video settings schema version is missing.");
  }
  if (schema.toInt(-1) == 0) {
    const auto settings = readLegacy(root);
    if (!settings || !validateVideoSettings(*settings)) {
      return invalidResult("The legacy video settings are invalid.");
    }
    return {.status = {}, .settings = *settings, .migrated = true};
  }
  const auto schemaVersionValue = schema.toInt(-1);
  if (schemaVersionValue != 1 &&
      schemaVersionValue != static_cast<int>(schemaVersion)) {
    return invalidResult("The video settings schema version is not supported.");
  }
  const auto videoObject = root.value(QStringLiteral("video"));
  const auto settings = videoObject.isObject()
    ? readCurrent(videoObject.toObject(),
        schemaVersionValue == static_cast<int>(schemaVersion))
    : std::nullopt;
  if (!settings || !validateVideoSettings(*settings)) {
    return invalidResult("The video settings values are invalid.");
  }
  return {.status = {}, .settings = *settings,
    .migrated = schemaVersionValue != static_cast<int>(schemaVersion)};
}

PersistenceStatus VideoSettingsStore::save(const VideoSettings& settings) const
{
  if (!validateVideoSettings(settings)) {
    return invalid("Invalid video settings cannot be saved.");
  }
  QJsonArray shaderParameters;
  for (const auto& parameter : settings.shader.parameters) {
    shaderParameters.push_back(QJsonObject{
      {QStringLiteral("name"), QString::fromStdString(parameter.name)},
      {QStringLiteral("value"), static_cast<double>(parameter.value)},
    });
  }
  const QJsonObject shaderObject{
    {QStringLiteral("mode"), shaderModeName(settings.shader.mode)},
    {QStringLiteral("presetPath"), fromPath(settings.shader.presetPath)},
    {QStringLiteral("parameters"), shaderParameters},
  };
  const QJsonObject videoObject{
    {QStringLiteral("aspect"), aspectName(settings.aspect)},
    {QStringLiteral("scaling"), scalingName(settings.scaling)},
    {QStringLiteral("presentationFilter"),
      presentationFilterName(settings.presentationFilter)},
    {QStringLiteral("shader"), shaderObject},
    {QStringLiteral("overscan"), overscanName(settings.core.overscan)},
    {QStringLiteral("ntscFilter"), ntscName(settings.core.ntscFilter)},
    {QStringLiteral("gameGearExtendedScreen"),
      settings.core.gameGearExtendedScreen},
    {QStringLiteral("interlacedRender"),
      interlacedRenderName(settings.core.interlacedRender)},
  };
  const auto data = QJsonDocument{QJsonObject{
    {QStringLiteral("schemaVersion"), static_cast<int>(schemaVersion)},
    {QStringLiteral("video"), videoObject},
  }}.toJson(QJsonDocument::Indented);
  return writeFileAtomically(path_,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(data.constData()),
      static_cast<std::size_t>(data.size())}, maximumFileBytes);
}

} // namespace genplusgx::settings
