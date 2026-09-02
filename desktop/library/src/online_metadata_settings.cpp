#include "genplusgx/library/online_metadata_settings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrl>

#include <algorithm>
#include <cctype>
#include <span>
#include <utility>

namespace genplusgx::library {
namespace {

PersistenceStatus invalid(std::string message)
{
  return {.error = PersistenceError::invalidData, .message = std::move(message)};
}

bool languageCode(const std::string& text)
{
  return text.size() == 2U && std::ranges::all_of(text, [](unsigned char byte) {
    return byte >= 'a' && byte <= 'z';
  });
}

bool regionCode(const std::string& text)
{
  return text.empty() || (text.size() == 2U &&
    std::ranges::all_of(text, [](unsigned char byte) {
      return byte >= 'a' && byte <= 'z';
    }));
}

std::optional<OnlineMetadataProvider> providerFromString(const QString& text)
{
  if (text == QStringLiteral("retronian")) {
    return OnlineMetadataProvider::retronian;
  }
  if (text == QStringLiteral("licensed-manifest")) {
    return OnlineMetadataProvider::licensedManifest;
  }
  return std::nullopt;
}

QString providerString(OnlineMetadataProvider provider)
{
  return provider == OnlineMetadataProvider::retronian
    ? QStringLiteral("retronian") : QStringLiteral("licensed-manifest");
}

} // namespace

OnlineMetadataSettings defaultOnlineMetadataSettings() noexcept { return {}; }

std::string_view onlineMetadataProviderName(
  OnlineMetadataProvider provider) noexcept
{
  switch (provider) {
    case OnlineMetadataProvider::retronian:
      return "Retronian GameDB";
    case OnlineMetadataProvider::licensedManifest:
      return "Licensed Manifest API v1";
  }
  return "Unknown";
}

OnlineMetadataStatus validateOnlineMetadataSettings(
  const OnlineMetadataSettings& settings) noexcept
{
  if (settings.automaticLookup && !settings.enabled) {
    return {OnlineMetadataError::invalidSettings,
      "Automatic lookup cannot be enabled while online metadata is disabled."};
  }
  if (settings.downloadArtwork && !settings.enabled) {
    return {OnlineMetadataError::invalidSettings,
      "Artwork download cannot be enabled while online metadata is disabled."};
  }
  if (!languageCode(settings.preferredLanguage) ||
      !regionCode(settings.preferredRegion)) {
    return {OnlineMetadataError::invalidSettings,
      "Language and region preferences must be lowercase two-letter codes."};
  }
  if (settings.cacheMegabytes < 16U || settings.cacheMegabytes > 2'048U) {
    return {OnlineMetadataError::invalidSettings,
      "The online metadata cache must be between 16 MiB and 2048 MiB."};
  }
  if (settings.endpoint.empty() || settings.endpoint.size() > 2'048U) {
    return {OnlineMetadataError::invalidSettings,
      "The metadata provider endpoint is missing or too long."};
  }
  const QUrl endpoint{QString::fromStdString(settings.endpoint), QUrl::StrictMode};
  if (!endpoint.isValid() || endpoint.scheme() != QStringLiteral("https") ||
      endpoint.host().isEmpty() || !endpoint.userInfo().isEmpty() ||
      endpoint.hasQuery() || endpoint.hasFragment()) {
    return {OnlineMetadataError::invalidSettings,
      "Use an HTTPS metadata endpoint without credentials, query, or fragment."};
  }
  if (settings.provider == OnlineMetadataProvider::retronian &&
      endpoint.path() != QString{} && endpoint.path() != QStringLiteral("/")) {
    return {OnlineMetadataError::invalidSettings,
      "The Retronian endpoint must be an HTTPS origin without a path."};
  }
  return {};
}

OnlineMetadataSettingsStore::OnlineMetadataSettingsStore(
  std::filesystem::path path)
  : path_(std::move(path))
{
}

const std::filesystem::path& OnlineMetadataSettingsStore::path() const noexcept
{
  return path_;
}

OnlineMetadataSettingsLoadResult OnlineMetadataSettingsStore::load() const
{
  const auto loaded = readFileBounded(path_, maximumFileBytes);
  if (!loaded.status) {
    return {.status = loaded.status, .settings = defaultOnlineMetadataSettings()};
  }
  if (!loaded.exists) {
    return {.status = {}, .settings = defaultOnlineMetadataSettings()};
  }
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(QByteArray{
    reinterpret_cast<const char*>(loaded.data.data()),
    static_cast<qsizetype>(loaded.data.size())}, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return {.status = invalid("The online metadata settings file is not valid JSON."),
      .settings = defaultOnlineMetadataSettings()};
  }
  const auto root = document.object();
  const auto values = root.value(QStringLiteral("onlineMetadata"));
  if (root.value(QStringLiteral("schemaVersion")).toInt(-1) !=
        static_cast<int>(schemaVersion) || !values.isObject()) {
    return {.status = invalid("The online metadata settings schema is not supported."),
      .settings = defaultOnlineMetadataSettings()};
  }
  const auto object = values.toObject();
  const auto enabled = object.value(QStringLiteral("enabled"));
  const auto automatic = object.value(QStringLiteral("automaticLookup"));
  const auto artwork = object.value(QStringLiteral("downloadArtwork"));
  const auto providerValue = object.value(QStringLiteral("provider"));
  const auto endpoint = object.value(QStringLiteral("endpoint"));
  const auto language = object.value(QStringLiteral("preferredLanguage"));
  const auto region = object.value(QStringLiteral("preferredRegion"));
  const auto cache = object.value(QStringLiteral("cacheMegabytes"));
  const auto provider = providerValue.isString()
    ? providerFromString(providerValue.toString()) : std::nullopt;
  if (!enabled.isBool() || !automatic.isBool() || !artwork.isBool() ||
      !provider || !endpoint.isString() || !language.isString() ||
      !region.isString() || !cache.isDouble()) {
    return {.status = invalid("The online metadata settings values are incomplete."),
      .settings = defaultOnlineMetadataSettings()};
  }
  OnlineMetadataSettings settings{
    .enabled = enabled.toBool(),
    .automaticLookup = automatic.toBool(),
    .downloadArtwork = artwork.toBool(),
    .provider = *provider,
    .endpoint = endpoint.toString().toStdString(),
    .preferredLanguage = language.toString().toStdString(),
    .preferredRegion = region.toString().toStdString(),
    .cacheMegabytes = static_cast<std::uint32_t>(cache.toInt(-1)),
  };
  const auto validation = validateOnlineMetadataSettings(settings);
  if (!validation) {
    return {.status = invalid(validation.message),
      .settings = defaultOnlineMetadataSettings()};
  }
  return {.status = {}, .settings = std::move(settings)};
}

PersistenceStatus OnlineMetadataSettingsStore::save(
  const OnlineMetadataSettings& settings) const
{
  const auto validation = validateOnlineMetadataSettings(settings);
  if (!validation) {
    return invalid(validation.message);
  }
  const auto data = QJsonDocument{QJsonObject{
    {QStringLiteral("schemaVersion"), static_cast<int>(schemaVersion)},
    {QStringLiteral("onlineMetadata"), QJsonObject{
      {QStringLiteral("enabled"), settings.enabled},
      {QStringLiteral("automaticLookup"), settings.automaticLookup},
      {QStringLiteral("downloadArtwork"), settings.downloadArtwork},
      {QStringLiteral("provider"), providerString(settings.provider)},
      {QStringLiteral("endpoint"), QString::fromStdString(settings.endpoint)},
      {QStringLiteral("preferredLanguage"),
        QString::fromStdString(settings.preferredLanguage)},
      {QStringLiteral("preferredRegion"),
        QString::fromStdString(settings.preferredRegion)},
      {QStringLiteral("cacheMegabytes"),
        static_cast<int>(settings.cacheMegabytes)},
    }},
  }}.toJson(QJsonDocument::Indented);
  return writeFileAtomically(path_, {
    reinterpret_cast<const std::uint8_t*>(data.constData()),
    static_cast<std::size_t>(data.size())}, maximumFileBytes);
}

} // namespace genplusgx::library
