#include "genplusgx/settings/screenshot_settings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <span>
#include <utility>

namespace genplusgx::settings {
namespace {

QString pathToQString(const std::filesystem::path& path)
{
#if defined(Q_OS_WIN)
  return QString::fromStdWString(path.wstring());
#else
  return QString::fromUtf8(path.string());
#endif
}

std::filesystem::path pathFromQString(const QString& path)
{
#if defined(Q_OS_WIN)
  return std::filesystem::path{path.toStdWString()};
#else
  return std::filesystem::path{path.toUtf8().constData()};
#endif
}

PersistenceStatus invalid(std::string message)
{
  return {
    .error = PersistenceError::invalidData,
    .message = std::move(message),
  };
}

} // namespace

bool validateScreenshotSettings(const ScreenshotSettings& settings) noexcept
{
  return !settings.directory.empty() && settings.directory.is_absolute() &&
         settings.directory.native().size() <= 4'096U;
}

ScreenshotSettingsStore::ScreenshotSettingsStore(
  std::filesystem::path path, std::filesystem::path defaultDirectory)
    : path_(std::move(path)), defaultDirectory_(std::move(defaultDirectory))
{
}

const std::filesystem::path& ScreenshotSettingsStore::path() const noexcept
{
  return path_;
}

const std::filesystem::path& ScreenshotSettingsStore::defaultDirectory() const noexcept
{
  return defaultDirectory_;
}

ScreenshotSettingsLoadResult ScreenshotSettingsStore::load() const
{
  const ScreenshotSettings defaults{.directory = defaultDirectory_};
  if (!validateScreenshotSettings(defaults)) {
    return {
      .status = invalid("The default screenshot directory is invalid."),
      .settings = defaults,
    };
  }
  const auto loaded = readFileBounded(path_, maximumFileBytes);
  if (!loaded.status) {
    return {.status = loaded.status, .settings = defaults};
  }
  if (!loaded.exists) {
    return {.status = {}, .settings = defaults};
  }
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(
    QByteArray{reinterpret_cast<const char*>(loaded.data.data()),
      static_cast<qsizetype>(loaded.data.size())},
    &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return {
      .status = invalid("The screenshot settings file is not valid JSON."),
      .settings = defaults,
    };
  }
  const auto root = document.object();
  const auto schema = root.value(QStringLiteral("schemaVersion"));
  if (!schema.isDouble()) {
    return {
      .status = invalid("The screenshot settings schema version is missing."),
      .settings = defaults,
    };
  }
  QString directory;
  bool migrated = false;
  if (schema.toInt(-1) == 0) {
    const auto legacy = root.value(QStringLiteral("screenshotDirectory"));
    if (!legacy.isString()) {
      return {
        .status = invalid("The legacy screenshot settings are invalid."),
        .settings = defaults,
      };
    }
    directory = legacy.toString();
    migrated = true;
  } else if (schema.toInt(-1) == static_cast<int>(schemaVersion)) {
    const auto screenshots = root.value(QStringLiteral("screenshots"));
    if (!screenshots.isObject() ||
        !screenshots.toObject().value(QStringLiteral("directory")).isString()) {
      return {
        .status = invalid("The screenshot settings values are invalid."),
        .settings = defaults,
      };
    }
    directory = screenshots.toObject().value(QStringLiteral("directory")).toString();
  } else {
    return {
      .status = invalid("The screenshot settings schema version is not supported."),
      .settings = defaults,
    };
  }
  ScreenshotSettings settings{.directory = pathFromQString(directory)};
  if (!validateScreenshotSettings(settings)) {
    return {
      .status = invalid("The screenshot directory is invalid."),
      .settings = defaults,
    };
  }
  return {.status = {}, .settings = std::move(settings), .migrated = migrated};
}

PersistenceStatus ScreenshotSettingsStore::save(
  const ScreenshotSettings& settings) const
{
  if (!validateScreenshotSettings(settings)) {
    return invalid("Invalid screenshot settings cannot be saved.");
  }
  const auto data = QJsonDocument{
    QJsonObject{
      {QStringLiteral("schemaVersion"), static_cast<int>(schemaVersion)},
      {QStringLiteral("screenshots"),
        QJsonObject{
          {QStringLiteral("directory"), pathToQString(settings.directory)},
        }},
    }}.toJson(QJsonDocument::Indented);
  return writeFileAtomically(path_,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(data.constData()),
      static_cast<std::size_t>(data.size())},
    maximumFileBytes);
}

} // namespace genplusgx::settings
