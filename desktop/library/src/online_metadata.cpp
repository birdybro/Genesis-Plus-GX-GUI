#include "genplusgx/library/online_metadata.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include <utility>

namespace genplusgx::library {
namespace {

OnlineMetadataStatus failure(OnlineMetadataError error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

bool boundedText(
  const std::string& text,
  std::size_t maximum,
  bool allowEmpty = true)
{
  return (allowEmpty || !text.empty()) && text.size() <= maximum &&
    std::ranges::none_of(text, [](unsigned char byte) {
      return byte == 0U || (byte < 0x20U && byte != '\n' && byte != '\t');
    });
}

bool lowercaseHex(const std::string& text, std::size_t length)
{
  return text.size() == length && std::ranges::all_of(text, [](unsigned char byte) {
    return std::isdigit(byte) != 0 || (byte >= 'a' && byte <= 'f');
  });
}

bool validHttpsUrl(const std::string& text)
{
  if (!boundedText(text, 2'048U, false)) {
    return false;
  }
  const QUrl url{QString::fromStdString(text), QUrl::StrictMode};
  return url.isValid() && url.scheme() == QStringLiteral("https") &&
    !url.host().isEmpty() && url.userInfo().isEmpty() && !url.hasFragment();
}

bool approvedLicenseReference(
  const std::string& spdx,
  const std::string& url)
{
  static constexpr std::array references{
    std::pair{std::string_view{"CC0-1.0"},
      std::string_view{"https://creativecommons.org/publicdomain/zero/1.0"}},
    std::pair{std::string_view{"PDM-1.0"},
      std::string_view{"https://creativecommons.org/publicdomain/mark/1.0"}},
    std::pair{std::string_view{"CC-BY-3.0"},
      std::string_view{"https://creativecommons.org/licenses/by/3.0"}},
    std::pair{std::string_view{"CC-BY-4.0"},
      std::string_view{"https://creativecommons.org/licenses/by/4.0"}},
    std::pair{std::string_view{"CC-BY-SA-3.0"},
      std::string_view{"https://creativecommons.org/licenses/by-sa/3.0"}},
    std::pair{std::string_view{"CC-BY-SA-4.0"},
      std::string_view{"https://creativecommons.org/licenses/by-sa/4.0"}},
    std::pair{std::string_view{"CC-BY-NC-SA-3.0"},
      std::string_view{"https://creativecommons.org/licenses/by-nc-sa/3.0"}},
    std::pair{std::string_view{"CC-BY-NC-SA-4.0"},
      std::string_view{"https://creativecommons.org/licenses/by-nc-sa/4.0"}},
  };
  auto normalized = url;
  while (normalized.ends_with('/')) {
    normalized.pop_back();
  }
  return std::ranges::any_of(references, [&](const auto& reference) {
    return reference.first == spdx && reference.second == normalized;
  });
}

QJsonObject encodeAttribution(const OnlineAttribution& attribution)
{
  return {
    {QStringLiteral("creator"), QString::fromStdString(attribution.creator)},
    {QStringLiteral("licenseSpdx"),
      QString::fromStdString(attribution.licenseSpdx)},
    {QStringLiteral("licenseUrl"),
      QString::fromStdString(attribution.licenseUrl)},
    {QStringLiteral("sourceUrl"),
      QString::fromStdString(attribution.sourceUrl)},
  };
}

std::optional<OnlineAttribution> decodeAttribution(const QJsonValue& value)
{
  if (!value.isObject()) {
    return std::nullopt;
  }
  const auto object = value.toObject();
  const auto creator = object.value(QStringLiteral("creator"));
  const auto license = object.value(QStringLiteral("licenseSpdx"));
  const auto licenseUrl = object.value(QStringLiteral("licenseUrl"));
  const auto sourceUrl = object.value(QStringLiteral("sourceUrl"));
  if (!creator.isString() || !license.isString() || !licenseUrl.isString() ||
      !sourceUrl.isString()) {
    return std::nullopt;
  }
  return OnlineAttribution{
    .creator = creator.toString().toStdString(),
    .licenseSpdx = license.toString().toStdString(),
    .licenseUrl = licenseUrl.toString().toStdString(),
    .sourceUrl = sourceUrl.toString().toStdString(),
  };
}

} // namespace

bool isApprovedContentLicense(const std::string& spdx) noexcept
{
  static constexpr std::array approved{
    std::string_view{"CC0-1.0"},
    std::string_view{"CC-BY-3.0"},
    std::string_view{"CC-BY-4.0"},
    std::string_view{"CC-BY-SA-3.0"},
    std::string_view{"CC-BY-SA-4.0"},
    std::string_view{"CC-BY-NC-SA-3.0"},
    std::string_view{"CC-BY-NC-SA-4.0"},
    std::string_view{"PDM-1.0"},
  };
  return std::ranges::find(approved, spdx) != approved.end();
}

OnlineMetadataStatus validateOnlineMetadataRecord(
  const OnlineMetadataRecord& record) noexcept
{
  if (!lowercaseHex(record.lookupSha256, 64U)) {
    return failure(OnlineMetadataError::invalidResponse,
      "Online metadata does not contain the requested lowercase SHA-256.");
  }
  if (!boundedText(record.providerName, 128U, false) ||
      !validHttpsUrl(record.providerHomepage) ||
      !boundedText(record.preferredTitle, 512U, false) ||
      !boundedText(record.alternateTitle, 512U) ||
      !boundedText(record.description, 16U * 1024U) ||
      !boundedText(record.releaseDate, 32U) ||
      !boundedText(record.developer, 512U) ||
      !boundedText(record.publisher, 512U) || record.genres.size() > 32U) {
    return failure(OnlineMetadataError::invalidResponse,
      "Online metadata contains missing, malformed, or oversized text.");
  }
  if (std::ranges::any_of(record.genres, [](const auto& genre) {
        return !boundedText(genre, 128U, false);
      })) {
    return failure(OnlineMetadataError::invalidResponse,
      "Online metadata contains an invalid genre.");
  }
  const auto& attribution = record.attribution;
  if (!boundedText(attribution.creator, 512U, false) ||
      !isApprovedContentLicense(attribution.licenseSpdx) ||
      !validHttpsUrl(attribution.licenseUrl) ||
      !approvedLicenseReference(
        attribution.licenseSpdx, attribution.licenseUrl) ||
      !validHttpsUrl(attribution.sourceUrl)) {
    return failure(OnlineMetadataError::invalidResponse,
      "Online metadata lacks approved license and attribution details.");
  }
  if (!record.artwork) {
    return {};
  }
  const auto& artwork = *record.artwork;
  if (!validHttpsUrl(artwork.url) ||
      (!artwork.sha256.empty() && !lowercaseHex(artwork.sha256, 64U)) ||
      (artwork.mimeType != "image/png" && artwork.mimeType != "image/jpeg") ||
      !boundedText(artwork.attribution.creator, 512U, false) ||
      !isApprovedContentLicense(artwork.attribution.licenseSpdx) ||
      !validHttpsUrl(artwork.attribution.licenseUrl) ||
      !approvedLicenseReference(artwork.attribution.licenseSpdx,
        artwork.attribution.licenseUrl) ||
      !validHttpsUrl(artwork.attribution.sourceUrl)) {
    return failure(OnlineMetadataError::invalidResponse,
      "Online artwork lacks a safe URL, supported format, or approved license details.");
  }
  return {};
}

std::vector<std::uint8_t> encodeOnlineMetadataRecord(
  const OnlineMetadataRecord& record)
{
  if (!validateOnlineMetadataRecord(record)) {
    return {};
  }
  QJsonArray genres;
  for (const auto& genre : record.genres) {
    genres.append(QString::fromStdString(genre));
  }
  QJsonObject root{
    {QStringLiteral("schemaVersion"), 1},
    {QStringLiteral("lookupSha256"), QString::fromStdString(record.lookupSha256)},
    {QStringLiteral("providerName"), QString::fromStdString(record.providerName)},
    {QStringLiteral("providerHomepage"),
      QString::fromStdString(record.providerHomepage)},
    {QStringLiteral("preferredTitle"),
      QString::fromStdString(record.preferredTitle)},
    {QStringLiteral("alternateTitle"),
      QString::fromStdString(record.alternateTitle)},
    {QStringLiteral("description"), QString::fromStdString(record.description)},
    {QStringLiteral("releaseDate"), QString::fromStdString(record.releaseDate)},
    {QStringLiteral("developer"), QString::fromStdString(record.developer)},
    {QStringLiteral("publisher"), QString::fromStdString(record.publisher)},
    {QStringLiteral("genres"), genres},
    {QStringLiteral("attribution"), encodeAttribution(record.attribution)},
  };
  if (record.artwork) {
    root.insert(QStringLiteral("artwork"), QJsonObject{
      {QStringLiteral("url"), QString::fromStdString(record.artwork->url)},
      {QStringLiteral("sha256"), QString::fromStdString(record.artwork->sha256)},
      {QStringLiteral("mimeType"), QString::fromStdString(record.artwork->mimeType)},
      {QStringLiteral("attribution"),
        encodeAttribution(record.artwork->attribution)},
    });
  }
  const auto data = QJsonDocument{root}.toJson(QJsonDocument::Compact);
  return {reinterpret_cast<const std::uint8_t*>(data.constData()),
    reinterpret_cast<const std::uint8_t*>(data.constData() + data.size())};
}

OnlineMetadataDecodeResult decodeOnlineMetadataRecord(
  std::span<const std::uint8_t> data)
{
  if (data.empty() || data.size() > maximumOnlineMetadataBytes) {
    return {.status = failure(OnlineMetadataError::dataTooLarge,
      "The online metadata record is empty or exceeds its byte limit."),
      .record = {}};
  }
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(QByteArray{
    reinterpret_cast<const char*>(data.data()), static_cast<qsizetype>(data.size())},
    &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return {.status = failure(OnlineMetadataError::invalidResponse,
      "The online metadata record is not valid JSON."), .record = {}};
  }
  const auto root = document.object();
  if (root.value(QStringLiteral("schemaVersion")).toInt(-1) != 1) {
    return {.status = failure(OnlineMetadataError::invalidResponse,
      "The online metadata schema is not supported."), .record = {}};
  }
  const auto stringValue = [&root](const char* key) -> std::optional<std::string> {
    const auto value = root.value(QString::fromLatin1(key));
    return value.isString()
      ? std::optional<std::string>{value.toString().toStdString()}
      : std::nullopt;
  };
  const auto hash = stringValue("lookupSha256");
  const auto provider = stringValue("providerName");
  const auto homepage = stringValue("providerHomepage");
  const auto title = stringValue("preferredTitle");
  const auto alternate = stringValue("alternateTitle");
  const auto description = stringValue("description");
  const auto releaseDate = stringValue("releaseDate");
  const auto developer = stringValue("developer");
  const auto publisher = stringValue("publisher");
  const auto attribution = decodeAttribution(
    root.value(QStringLiteral("attribution")));
  const auto genresValue = root.value(QStringLiteral("genres"));
  if (!hash || !provider || !homepage || !title || !alternate || !description ||
      !releaseDate || !developer || !publisher || !attribution ||
      !genresValue.isArray()) {
    return {.status = failure(OnlineMetadataError::invalidResponse,
      "The online metadata record is incomplete."), .record = {}};
  }
  OnlineMetadataRecord record{
    .lookupSha256 = *hash,
    .providerName = *provider,
    .providerHomepage = *homepage,
    .preferredTitle = *title,
    .alternateTitle = *alternate,
    .description = *description,
    .releaseDate = *releaseDate,
    .developer = *developer,
    .publisher = *publisher,
    .genres = {},
    .attribution = *attribution,
    .artwork = std::nullopt,
  };
  for (const auto& value : genresValue.toArray()) {
    if (!value.isString()) {
      return {.status = failure(OnlineMetadataError::invalidResponse,
        "The online metadata genre list is invalid."), .record = {}};
    }
    record.genres.push_back(value.toString().toStdString());
  }
  const auto artworkValue = root.value(QStringLiteral("artwork"));
  if (!artworkValue.isUndefined()) {
    if (!artworkValue.isObject()) {
      return {.status = failure(OnlineMetadataError::invalidResponse,
        "The online artwork record is invalid."), .record = {}};
    }
    const auto artwork = artworkValue.toObject();
    const auto url = artwork.value(QStringLiteral("url"));
    const auto artworkHash = artwork.value(QStringLiteral("sha256"));
    const auto mimeType = artwork.value(QStringLiteral("mimeType"));
    const auto artworkAttribution = decodeAttribution(
      artwork.value(QStringLiteral("attribution")));
    if (!url.isString() || !artworkHash.isString() || !mimeType.isString() ||
        !artworkAttribution) {
      return {.status = failure(OnlineMetadataError::invalidResponse,
        "The online artwork record is incomplete."), .record = {}};
    }
    record.artwork = OnlineArtworkOffer{
      .url = url.toString().toStdString(),
      .sha256 = artworkHash.toString().toStdString(),
      .mimeType = mimeType.toString().toStdString(),
      .attribution = *artworkAttribution,
    };
  }
  const auto validation = validateOnlineMetadataRecord(record);
  return validation
    ? OnlineMetadataDecodeResult{.status = {}, .record = std::move(record)}
    : OnlineMetadataDecodeResult{.status = validation, .record = {}};
}

} // namespace genplusgx::library
