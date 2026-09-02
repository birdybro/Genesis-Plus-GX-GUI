#include "genplusgx/library/online_metadata_client.h"

#include "genplusgx/persistence.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QEventLoop>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <map>
#include <ranges>
#include <set>
#include <system_error>
#include <utility>

namespace genplusgx::library {
namespace {

constexpr int transferTimeoutMilliseconds = 15'000;
constexpr auto recordFreshness = std::chrono::hours{24 * 7};
constexpr auto indexFreshness = std::chrono::hours{24};

OnlineMetadataStatus failure(OnlineMetadataError error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

OnlineMetadataDecodeResult decodeFailure(OnlineMetadataStatus status)
{
  return {.status = std::move(status), .record = {}};
}

OnlineMetadataLookupResult lookupFailure(OnlineMetadataStatus status)
{
  return {.status = std::move(status), .record = {}, .artworkPath = {},
    .fromCache = false, .staleCache = false};
}

OnlineHttpResult httpFailure(OnlineMetadataStatus status, int statusCode = 0)
{
  return {.status = std::move(status), .statusCode = statusCode,
    .contentType = {}, .data = {}};
}

bool lowercaseHex(const std::string& text, std::size_t length)
{
  return text.size() == length && std::ranges::all_of(text, [](unsigned char byte) {
    return std::isdigit(byte) != 0 || (byte >= 'a' && byte <= 'f');
  });
}

std::string sha256(std::span<const std::uint8_t> data)
{
  QCryptographicHash hash{QCryptographicHash::Sha256};
  hash.addData(QByteArrayView{reinterpret_cast<const char*>(data.data()),
    static_cast<qsizetype>(data.size())});
  return hash.result().toHex().toStdString();
}

std::string providerCacheKey(const OnlineMetadataSettings& settings)
{
  const auto identity = std::to_string(static_cast<unsigned int>(settings.provider)) +
    "\n" + settings.endpoint;
  return sha256({reinterpret_cast<const std::uint8_t*>(identity.data()),
    identity.size()}).substr(0U, 16U);
}

std::filesystem::path providerCacheDirectory(
  const std::filesystem::path& cacheDirectory,
  const OnlineMetadataSettings& settings)
{
  return cacheDirectory / "online-metadata" / providerCacheKey(settings);
}

bool fresh(const std::filesystem::path& path, std::chrono::hours maximumAge)
{
  std::error_code error;
  const auto modified = std::filesystem::last_write_time(path, error);
  if (error) {
    return false;
  }
  const auto now = std::filesystem::file_time_type::clock::now();
  return modified <= now && now - modified <= maximumAge;
}

std::optional<std::vector<std::uint8_t>> readCache(
  const std::filesystem::path& path,
  std::size_t maximumBytes)
{
  const auto loaded = readFileBounded(path, maximumBytes);
  return loaded.status && loaded.exists
    ? std::optional<std::vector<std::uint8_t>>{loaded.data} : std::nullopt;
}

void pruneCache(const std::filesystem::path& root, std::uint64_t maximumBytes)
{
  struct Entry final {
    std::filesystem::path path;
    std::uint64_t size{0U};
    std::filesystem::file_time_type modified;
  };
  std::vector<Entry> entries;
  std::uint64_t total = 0U;
  std::error_code error;
  if (!std::filesystem::is_directory(root, error) || error) {
    return;
  }
  for (std::filesystem::recursive_directory_iterator iterator{
         root, std::filesystem::directory_options::skip_permission_denied, error}, end;
       iterator != end && !error; iterator.increment(error)) {
    if (!iterator->is_regular_file(error) || error) {
      error.clear();
      continue;
    }
    const auto size = iterator->file_size(error);
    if (error) {
      error.clear();
      continue;
    }
    const auto modified = iterator->last_write_time(error);
    if (error) {
      error.clear();
      continue;
    }
    entries.push_back({iterator->path(), size, modified});
    total += size;
  }
  std::ranges::sort(entries, {}, &Entry::modified);
  for (const auto& entry : entries) {
    if (total <= maximumBytes) {
      break;
    }
    std::filesystem::remove(entry.path, error);
    if (!error) {
      total -= std::min(total, entry.size);
    }
    error.clear();
  }
}

QUrl appendPath(QUrl base, const QStringList& segments)
{
  auto path = base.path();
  if (!path.endsWith('/')) {
    path += '/';
  }
  for (qsizetype index = 0; index < segments.size(); ++index) {
    path += QString::fromUtf8(QUrl::toPercentEncoding(segments[index]));
    if (index + 1 < segments.size()) {
      path += '/';
    }
  }
  base.setPath(path, QUrl::DecodedMode);
  return base;
}

std::string systemSlug(GameSystem system)
{
  switch (system) {
    case GameSystem::sg1000:
      return "sg1000";
    case GameSystem::masterSystem:
      return "master-system";
    case GameSystem::gameGear:
      return "game-gear";
    case GameSystem::genesis:
      return "genesis";
    case GameSystem::segaCd:
      return "sega-cd";
    case GameSystem::unknown:
      return {};
  }
  return {};
}

std::optional<std::string> boundedString(
  const QJsonObject& object,
  const char* key,
  std::size_t maximum,
  bool required = false)
{
  const auto value = object.value(QString::fromLatin1(key));
  if (value.isUndefined() && !required) {
    return std::string{};
  }
  if (!value.isString()) {
    return std::nullopt;
  }
  auto text = value.toString().toStdString();
  if ((required && text.empty()) || text.size() > maximum ||
      std::ranges::any_of(text, [](unsigned char byte) { return byte == 0U; })) {
    return std::nullopt;
  }
  return text;
}

std::string displaySlug(std::string text)
{
  bool wordStart = true;
  for (auto& character : text) {
    if (character == '-' || character == '_') {
      character = ' ';
      wordStart = true;
    } else if (wordStart) {
      character = static_cast<char>(std::toupper(
        static_cast<unsigned char>(character)));
      wordStart = false;
    }
  }
  return text;
}

std::string joinedSlugs(const QJsonValue& value)
{
  if (!value.isArray() || value.toArray().size() > 32) {
    return {};
  }
  std::string result;
  for (const auto& item : value.toArray()) {
    if (!item.isString()) {
      return {};
    }
    auto current = item.toString().toStdString();
    if (current.empty() || current.size() > 128U) {
      return {};
    }
    if (!result.empty()) {
      result += ", ";
    }
    result += displaySlug(std::move(current));
  }
  return result;
}

std::optional<OnlineArtworkOffer> licensedArtwork(
  const QJsonValue& mediaValue,
  const std::string& preferredRegion)
{
  if (!mediaValue.isArray() || mediaValue.toArray().size() > 256) {
    return std::nullopt;
  }
  std::optional<OnlineArtworkOffer> fallback;
  for (const auto& value : mediaValue.toArray()) {
    if (!value.isObject()) {
      continue;
    }
    const auto object = value.toObject();
    if (object.value(QStringLiteral("kind")).toString() !=
        QStringLiteral("boxart")) {
      continue;
    }
    const auto license = object.value(QStringLiteral("license"));
    if (!license.isObject()) {
      continue;
    }
    const auto licenseObject = license.toObject();
    const auto url = boundedString(object, "url", 2'048U, true);
    const auto digest = boundedString(object, "sha256", 64U);
    const auto mime = boundedString(object, "mime_type", 64U, true);
    const auto creator = boundedString(object, "creator", 512U, true);
    const auto source = boundedString(object, "source_url", 2'048U, true);
    const auto spdx = boundedString(licenseObject, "spdx", 64U, true);
    const auto licenseUrl = boundedString(licenseObject, "url", 2'048U, true);
    if (!url || !digest || !mime || !creator || !source || !spdx ||
        !licenseUrl) {
      continue;
    }
    OnlineArtworkOffer offer{
      .url = *url,
      .sha256 = *digest,
      .mimeType = *mime,
      .attribution = {
        .creator = *creator,
        .licenseSpdx = *spdx,
        .licenseUrl = *licenseUrl,
        .sourceUrl = *source,
      },
    };
    OnlineMetadataRecord probe{
      .lookupSha256 = std::string(64U, '0'),
      .providerName = "probe",
      .providerHomepage = "https://example.invalid",
      .preferredTitle = "probe",
      .alternateTitle = {},
      .description = {},
      .releaseDate = {},
      .developer = {},
      .publisher = {},
      .genres = {},
      .attribution = {"probe", "CC0-1.0",
        "https://creativecommons.org/publicdomain/zero/1.0/",
        "https://example.invalid/source"},
      .artwork = offer,
    };
    if (!validateOnlineMetadataRecord(probe)) {
      continue;
    }
    const auto region = object.value(QStringLiteral("region")).toString()
      .toStdString();
    if (!preferredRegion.empty() && region == preferredRegion) {
      return offer;
    }
    if (!fallback) {
      fallback = std::move(offer);
    }
  }
  return fallback;
}

std::optional<QJsonObject> jsonObject(
  std::span<const std::uint8_t> data,
  std::size_t maximum)
{
  if (data.empty() || data.size() > maximum) {
    return std::nullopt;
  }
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(QByteArray{
    reinterpret_cast<const char*>(data.data()), static_cast<qsizetype>(data.size())},
    &error);
  return error.error == QJsonParseError::NoError && document.isObject()
    ? std::optional<QJsonObject>{document.object()} : std::nullopt;
}

std::optional<std::string> retronianTitle(
  const QJsonArray& titles,
  const std::string& language,
  const std::string& region,
  const std::string& excluded = {})
{
  const auto choose = [&](bool requireRegion, const std::string& wantedLanguage)
      -> std::optional<std::string> {
    for (const auto& value : titles) {
      if (!value.isObject()) {
        continue;
      }
      const auto object = value.toObject();
      const auto text = boundedString(object, "text", 512U, true);
      if (!text || *text == excluded ||
          object.value(QStringLiteral("lang")).toString().toStdString() !=
            wantedLanguage ||
          (requireRegion && object.value(QStringLiteral("region")).toString()
              .toStdString() != region)) {
        continue;
      }
      return text;
    }
    return std::nullopt;
  };
  if (!region.empty()) {
    if (auto exact = choose(true, language)) {
      return exact;
    }
  }
  if (auto preferred = choose(false, language)) {
    return preferred;
  }
  if (language != "en") {
    if (auto english = choose(false, "en")) {
      return english;
    }
  }
  for (const auto& value : titles) {
    if (value.isObject()) {
      const auto text = boundedString(value.toObject(), "text", 512U, true);
      if (text && *text != excluded) {
        return text;
      }
    }
  }
  return std::nullopt;
}

std::string retronianDescription(
  const QJsonValue& descriptionsValue,
  const std::string& language)
{
  if (!descriptionsValue.isArray() || descriptionsValue.toArray().size() > 64) {
    return {};
  }
  const auto descriptions = descriptionsValue.toArray();
  for (const auto& wanted : {language, std::string{"en"}}) {
    for (const auto& value : descriptions) {
      if (!value.isObject()) {
        continue;
      }
      const auto object = value.toObject();
      if (object.value(QStringLiteral("lang")).toString().toStdString() == wanted) {
        if (const auto text = boundedString(object, "text", 16U * 1024U, true)) {
          return *text;
        }
      }
    }
  }
  return {};
}

std::string extensionForMime(const std::string& mime)
{
  if (mime == "image/png") {
    return ".png";
  }
  if (mime == "image/jpeg") {
    return ".jpg";
  }
  return {};
}

OnlineMetadataStatus validateArtworkBytes(
  const OnlineArtworkOffer& offer,
  const OnlineHttpResult& response)
{
  auto contentType = response.contentType;
  if (const auto separator = contentType.find(';'); separator != std::string::npos) {
    contentType.erase(separator);
  }
  if (contentType != offer.mimeType) {
    return failure(OnlineMetadataError::invalidResponse,
      "The artwork response content type does not match its licensed manifest.");
  }
  if (!offer.sha256.empty() && sha256(response.data) != offer.sha256) {
    return failure(OnlineMetadataError::invalidResponse,
      "Downloaded artwork failed its SHA-256 integrity check.");
  }
  QByteArray bytes{reinterpret_cast<const char*>(response.data.data()),
    static_cast<qsizetype>(response.data.size())};
  QBuffer buffer{&bytes};
  if (!buffer.open(QIODevice::ReadOnly)) {
    return failure(OnlineMetadataError::invalidResponse,
      "Downloaded artwork could not be inspected.");
  }
  QImageReader reader{&buffer};
  reader.setDecideFormatFromContent(true);
  const auto size = reader.size();
  if (!size.isValid() || size.width() > 8'192 || size.height() > 8'192 ||
      static_cast<std::uint64_t>(size.width()) *
        static_cast<std::uint64_t>(size.height()) > 16U * 1024U * 1024U ||
      !reader.canRead()) {
    return failure(OnlineMetadataError::invalidResponse,
      "Downloaded artwork is invalid or exceeds the 16-megapixel limit.");
  }
  const auto detectedFormat = reader.format().toLower();
  const bool formatMatches = offer.mimeType == "image/png"
    ? detectedFormat == QByteArrayLiteral("png")
    : detectedFormat == QByteArrayLiteral("jpeg") ||
        detectedFormat == QByteArrayLiteral("jpg");
  if (!formatMatches) {
    return failure(OnlineMetadataError::invalidResponse,
      "Downloaded artwork bytes do not match the declared image format.");
  }
  const auto decoded = reader.read();
  if (decoded.isNull()) {
    return failure(OnlineMetadataError::invalidResponse,
      "Downloaded artwork could not be decoded completely.");
  }
  return {};
}

OnlineMetadataLookupResult cachedResult(
  const std::filesystem::path& recordPath,
  const std::filesystem::path& artworkDirectory,
  const std::string& hash,
  bool stale)
{
  const auto bytes = readCache(recordPath, maximumOnlineMetadataBytes);
  if (!bytes) {
    return lookupFailure(failure(OnlineMetadataError::cacheFailed,
      "The cached metadata record could not be read."));
  }
  auto decoded = decodeOnlineMetadataRecord(*bytes);
  if (!decoded.status || decoded.record.lookupSha256 != hash) {
    return lookupFailure(failure(OnlineMetadataError::cacheFailed,
      "The cached metadata record is invalid or belongs to another game."));
  }
  std::filesystem::path artworkPath;
  if (decoded.record.artwork) {
    const auto candidate = artworkDirectory /
      (hash + extensionForMime(decoded.record.artwork->mimeType));
    const auto artwork = readCache(candidate, maximumOnlineArtworkBytes);
    const OnlineHttpResult cachedArtwork{
      .status = {},
      .statusCode = 200,
      .contentType = decoded.record.artwork->mimeType,
      .data = artwork.value_or(std::vector<std::uint8_t>{}),
    };
    if (artwork && validateArtworkBytes(
          *decoded.record.artwork, cachedArtwork)) {
      artworkPath = candidate;
    }
  }
  return {.status = {}, .record = std::move(decoded.record),
    .artworkPath = std::move(artworkPath), .fromCache = true,
    .staleCache = stale};
}

} // namespace

class QtOnlineHttpTransport::Private final {
public:
  Private(OnlineMetadataCancellation cancellation,
    const std::vector<std::string>& trustedAuthorities)
    : cancellation_(std::move(cancellation))
  {
    sslConfiguration_ = QSslConfiguration::defaultConfiguration();
    auto authorities = sslConfiguration_.caCertificates();
    for (const auto& der : trustedAuthorities) {
      authorities.append(QSslCertificate::fromData(
        QByteArray::fromStdString(der), QSsl::Der));
    }
    sslConfiguration_.setCaCertificates(authorities);
  }

  OnlineHttpResult get(const std::string& url, std::size_t maximumBytes)
  {
    const QUrl parsed{QString::fromStdString(url), QUrl::StrictMode};
    if (!parsed.isValid() || parsed.scheme() != QStringLiteral("https") ||
        parsed.host().isEmpty() || !parsed.userInfo().isEmpty() ||
        parsed.hasFragment() || maximumBytes == 0U ||
        maximumBytes > maximumOnlineIndexBytes) {
      return httpFailure(failure(OnlineMetadataError::invalidRequest,
        "The online metadata request URL or byte limit is invalid."));
    }
    QNetworkRequest request{parsed};
    request.setTransferTimeout(transferTimeoutMilliseconds);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
      QNetworkRequest::ManualRedirectPolicy);
    request.setSslConfiguration(sslConfiguration_);
    request.setRawHeader("Accept", "application/json, image/png, image/jpeg");
    request.setRawHeader("User-Agent", "Genesis-Plus-GX-GUI/OnlineMetadata");
    auto* reply = manager_.get(request);
    QEventLoop loop;
    QTimer cancellationTimer;
    cancellationTimer.setInterval(25);
    QObject::connect(&cancellationTimer, &QTimer::timeout, reply, [this, reply] {
      if (cancellation_ && cancellation_()) {
        reply->abort();
      }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    bool overflow = false;
    QObject::connect(reply, &QNetworkReply::metaDataChanged, reply,
      [reply, maximumBytes, &overflow] {
      const auto length = reply->header(QNetworkRequest::ContentLengthHeader);
      if (length.isValid() && length.toULongLong() > maximumBytes) {
        overflow = true;
        reply->abort();
      }
    });
    QObject::connect(reply, &QIODevice::readyRead, reply,
      [reply, maximumBytes, &overflow] {
      const auto available = reply->bytesAvailable();
      if (available < 0 || static_cast<std::uint64_t>(available) > maximumBytes) {
        overflow = true;
        reply->abort();
      }
    });
    cancellationTimer.start();
    loop.exec();
    cancellationTimer.stop();
    const auto statusValue = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const auto statusCode = statusValue.isValid() ? statusValue.toInt() : 0;
    const auto contentType = reply->header(QNetworkRequest::ContentTypeHeader)
      .toString().toStdString();
    const auto networkError = reply->error();
    const auto body = overflow ? QByteArray{} : reply->readAll();
    reply->deleteLater();
    if (overflow || body.size() > static_cast<qsizetype>(maximumBytes)) {
      return httpFailure(failure(OnlineMetadataError::dataTooLarge,
        "The online provider response exceeded its fixed byte limit."),
        statusCode);
    }
    if (cancellation_ && cancellation_()) {
      return httpFailure(failure(OnlineMetadataError::cancelled,
        "Online metadata lookup was cancelled."), statusCode);
    }
    if (networkError != QNetworkReply::NoError && statusCode == 0) {
      return httpFailure(failure(OnlineMetadataError::transportFailed,
        networkError == QNetworkReply::SslHandshakeFailedError
          ? "TLS certificate validation failed for the metadata provider."
          : "The HTTPS metadata request failed (network error " +
              std::to_string(static_cast<int>(networkError)) + ")."));
    }
    return {.status = {}, .statusCode = statusCode,
      .contentType = contentType,
      .data = {reinterpret_cast<const std::uint8_t*>(body.constData()),
        reinterpret_cast<const std::uint8_t*>(body.constData() + body.size())}};
  }

private:
  OnlineMetadataCancellation cancellation_;
  QSslConfiguration sslConfiguration_;
  QNetworkAccessManager manager_;
};

QtOnlineHttpTransport::QtOnlineHttpTransport(
  OnlineMetadataCancellation cancellation,
  std::vector<std::string> additionalTrustedCaDer)
  : private_(std::make_unique<Private>(
      std::move(cancellation), additionalTrustedCaDer))
{
}

QtOnlineHttpTransport::~QtOnlineHttpTransport() = default;

OnlineHttpResult QtOnlineHttpTransport::get(
  const std::string& url,
  std::size_t maximumBytes)
{
  return private_->get(url, maximumBytes);
}

OnlineMetadataStatus identifyRetronianGame(
  std::span<const std::uint8_t> indexData,
  const std::string& hash,
  std::string& gameId)
{
  gameId.clear();
  if (!lowercaseHex(hash, 64U)) {
    return failure(OnlineMetadataError::invalidRequest,
      "Retronian lookup requires a lowercase SHA-256 hash.");
  }
  const auto root = jsonObject(indexData, maximumOnlineIndexBytes);
  if (!root) {
    return failure(OnlineMetadataError::invalidResponse,
      "The Retronian ROM index is not valid bounded JSON.");
  }
  const auto hashes = root->value(QStringLiteral("by_sha256"));
  if (!hashes.isObject()) {
    return failure(OnlineMetadataError::invalidResponse,
      "The Retronian ROM index lacks its SHA-256 map.");
  }
  const auto matchesValue = hashes.toObject().value(QString::fromStdString(hash));
  if (matchesValue.isUndefined()) {
    return failure(OnlineMetadataError::notFound,
      "Retronian GameDB has no exact SHA-256 match for this game.");
  }
  const auto matches = matchesValue.isArray()
    ? matchesValue.toArray() : QJsonArray{};
  if (matches.empty() || matches.size() > 16) {
    return failure(OnlineMetadataError::invalidResponse,
      "The Retronian ROM index returned an invalid match set.");
  }
  std::set<std::string> ids;
  for (const auto& value : matches) {
    if (!value.isObject()) {
      return failure(OnlineMetadataError::invalidResponse,
        "The Retronian ROM index contains an invalid match.");
    }
    const auto id = boundedString(value.toObject(), "game_id", 128U, true);
    if (!id || !std::ranges::all_of(*id, [](unsigned char byte) {
          return std::islower(byte) != 0 || std::isdigit(byte) != 0 || byte == '-';
        })) {
      return failure(OnlineMetadataError::invalidResponse,
        "The Retronian ROM index contains an unsafe game identifier.");
    }
    ids.insert(*id);
  }
  if (ids.size() != 1U) {
    return failure(OnlineMetadataError::invalidResponse,
      "The Retronian ROM index returned ambiguous game identifiers.");
  }
  gameId = *ids.begin();
  return {};
}

OnlineMetadataDecodeResult decodeRetronianGameResponse(
  std::span<const std::uint8_t> data,
  const std::string& expectedSha256,
  const std::string& preferredLanguage,
  const std::string& preferredRegion)
{
  const auto root = jsonObject(data, maximumOnlineMetadataBytes);
  if (!root) {
    return decodeFailure(failure(OnlineMetadataError::invalidResponse,
      "The Retronian game response is not valid bounded JSON."));
  }
  const auto titlesValue = root->value(QStringLiteral("titles"));
  const auto romsValue = root->value(QStringLiteral("roms"));
  if (!titlesValue.isArray() || titlesValue.toArray().empty() ||
      titlesValue.toArray().size() > 128 || !romsValue.isArray() ||
      romsValue.toArray().size() > 256) {
    return decodeFailure(failure(OnlineMetadataError::invalidResponse,
      "The Retronian game response has invalid title or ROM records."));
  }
  bool hashConfirmed = false;
  for (const auto& value : romsValue.toArray()) {
    if (value.isObject() &&
        value.toObject().value(QStringLiteral("sha256")).toString()
          .toStdString() == expectedSha256) {
      hashConfirmed = true;
      break;
    }
  }
  if (!hashConfirmed) {
    return decodeFailure(failure(OnlineMetadataError::invalidResponse,
      "The Retronian game response does not contain the requested SHA-256."));
  }
  const auto preferred = retronianTitle(
    titlesValue.toArray(), preferredLanguage, preferredRegion);
  if (!preferred) {
    return decodeFailure(failure(OnlineMetadataError::invalidResponse,
      "The Retronian game response has no usable title."));
  }
  const auto alternate = retronianTitle(
    titlesValue.toArray(), preferredLanguage == "ja" ? "en" : "ja", {},
    *preferred);
  auto genres = joinedSlugs(root->value(QStringLiteral("genres")));
  std::vector<std::string> genreList;
  std::size_t begin = 0U;
  while (begin < genres.size()) {
    const auto end = genres.find(", ", begin);
    genreList.push_back(genres.substr(begin,
      end == std::string::npos ? std::string::npos : end - begin));
    if (end == std::string::npos) {
      break;
    }
    begin = end + 2U;
  }
  const auto releaseValue = root->value(QStringLiteral("first_release_date"));
  const auto releaseDate = releaseValue.isNull() || releaseValue.isUndefined()
    ? std::optional<std::string>{std::string{}}
    : boundedString(*root, "first_release_date", 32U);
  if (!releaseDate) {
    return decodeFailure(failure(OnlineMetadataError::invalidResponse,
      "The Retronian release date is malformed."));
  }
  OnlineMetadataRecord record{
    .lookupSha256 = expectedSha256,
    .providerName = "Retronian GameDB",
    .providerHomepage = "https://gamedb.retronian.com",
    .preferredTitle = *preferred,
    .alternateTitle = alternate.value_or(std::string{}),
    .description = retronianDescription(
      root->value(QStringLiteral("descriptions")), preferredLanguage),
    .releaseDate = *releaseDate,
    .developer = joinedSlugs(root->value(QStringLiteral("developers"))),
    .publisher = joinedSlugs(root->value(QStringLiteral("publishers"))),
    .genres = std::move(genreList),
    .attribution = {
      .creator = "Retronian GameDB contributors",
      .licenseSpdx = "CC-BY-SA-4.0",
      .licenseUrl = "https://creativecommons.org/licenses/by-sa/4.0/",
      .sourceUrl = "https://gamedb.retronian.com/",
    },
    .artwork = licensedArtwork(
      root->value(QStringLiteral("media")), preferredRegion),
  };
  const auto validation = validateOnlineMetadataRecord(record);
  return validation
    ? OnlineMetadataDecodeResult{.status = {}, .record = std::move(record)}
    : decodeFailure(validation);
}

OnlineMetadataDecodeResult decodeLicensedManifestResponse(
  std::span<const std::uint8_t> data,
  const std::string& expectedSha256)
{
  const auto root = jsonObject(data, maximumOnlineMetadataBytes);
  if (!root || root->value(QStringLiteral("schemaVersion")).toInt(-1) != 1) {
    return decodeFailure(failure(OnlineMetadataError::invalidResponse,
      "The licensed metadata manifest schema is invalid or unsupported."));
  }
  const auto providerValue = root->value(QStringLiteral("provider"));
  const auto gameValue = root->value(QStringLiteral("game"));
  if (!providerValue.isObject() || !gameValue.isObject()) {
    return decodeFailure(failure(OnlineMetadataError::invalidResponse,
      "The licensed metadata manifest is incomplete."));
  }
  const auto provider = providerValue.toObject();
  const auto game = gameValue.toObject();
  const auto name = boundedString(provider, "name", 128U, true);
  const auto homepage = boundedString(provider, "homepage", 2'048U, true);
  const auto title = boundedString(game, "title", 512U, true);
  const auto hash = boundedString(game, "sha256", 64U, true);
  const auto creator = boundedString(provider, "creator", 512U, true);
  const auto licenseSpdx = boundedString(provider, "license_spdx", 64U, true);
  const auto licenseUrl = boundedString(provider, "license_url", 2'048U, true);
  const auto sourceUrl = boundedString(game, "source_url", 2'048U, true);
  if (!name || !homepage || !title || !hash || !creator || !licenseSpdx ||
      !licenseUrl || !sourceUrl || *hash != expectedSha256) {
    return decodeFailure(failure(OnlineMetadataError::invalidResponse,
      "The licensed metadata manifest identity or attribution is invalid."));
  }
  const auto optional = [&game](const char* key, std::size_t maximum) {
    return boundedString(game, key, maximum);
  };
  const auto alternate = optional("alternate_title", 512U);
  const auto description = optional("description", 16U * 1024U);
  const auto releaseDate = optional("release_date", 32U);
  const auto developer = optional("developer", 512U);
  const auto publisher = optional("publisher", 512U);
  if (!alternate || !description || !releaseDate || !developer || !publisher) {
    return decodeFailure(failure(OnlineMetadataError::invalidResponse,
      "The licensed metadata manifest contains malformed optional text."));
  }
  std::vector<std::string> genres;
  const auto genresValue = game.value(QStringLiteral("genres"));
  if (!genresValue.isUndefined()) {
    if (!genresValue.isArray() || genresValue.toArray().size() > 32) {
      return decodeFailure(failure(OnlineMetadataError::invalidResponse,
        "The licensed metadata genre list is invalid."));
    }
    for (const auto& value : genresValue.toArray()) {
      if (!value.isString() || value.toString().isEmpty() ||
          value.toString().toUtf8().size() > 128) {
        return decodeFailure(failure(OnlineMetadataError::invalidResponse,
          "The licensed metadata genre list is invalid."));
      }
      genres.push_back(value.toString().toStdString());
    }
  }
  std::optional<OnlineArtworkOffer> artwork;
  const auto artworkValue = game.value(QStringLiteral("artwork"));
  if (!artworkValue.isUndefined()) {
    if (!artworkValue.isObject()) {
      return decodeFailure(failure(OnlineMetadataError::invalidResponse,
        "The licensed artwork manifest is invalid."));
    }
    const auto object = artworkValue.toObject();
    const auto url = boundedString(object, "url", 2'048U, true);
    const auto digest = boundedString(object, "sha256", 64U);
    const auto mime = boundedString(object, "mime_type", 64U, true);
    const auto artCreator = boundedString(object, "creator", 512U, true);
    const auto artSpdx = boundedString(object, "license_spdx", 64U, true);
    const auto artLicenseUrl = boundedString(object, "license_url", 2'048U, true);
    const auto artSource = boundedString(object, "source_url", 2'048U, true);
    if (!url || !digest || !mime || !artCreator || !artSpdx || !artLicenseUrl ||
        !artSource) {
      return decodeFailure(failure(OnlineMetadataError::invalidResponse,
        "The licensed artwork manifest is incomplete."));
    }
    artwork = OnlineArtworkOffer{.url = *url, .sha256 = *digest,
      .mimeType = *mime,
      .attribution = {.creator = *artCreator, .licenseSpdx = *artSpdx,
        .licenseUrl = *artLicenseUrl, .sourceUrl = *artSource}};
  }
  OnlineMetadataRecord record{
    .lookupSha256 = *hash,
    .providerName = *name,
    .providerHomepage = *homepage,
    .preferredTitle = *title,
    .alternateTitle = *alternate,
    .description = *description,
    .releaseDate = *releaseDate,
    .developer = *developer,
    .publisher = *publisher,
    .genres = std::move(genres),
    .attribution = {.creator = *creator, .licenseSpdx = *licenseSpdx,
      .licenseUrl = *licenseUrl, .sourceUrl = *sourceUrl},
    .artwork = std::move(artwork),
  };
  const auto validation = validateOnlineMetadataRecord(record);
  return validation
    ? OnlineMetadataDecodeResult{.status = {}, .record = std::move(record)}
    : decodeFailure(validation);
}

OnlineMetadataLookupResult lookupOnlineMetadata(
  const OnlineMetadataSettings& settings,
  const GameMetadata& game,
  const std::filesystem::path& cacheDirectory,
  OnlineHttpTransport& transport,
  const OnlineMetadataCancellation& cancellation)
{
  const auto settingsValidation = validateOnlineMetadataSettings(settings);
  if (!settingsValidation) {
    return lookupFailure(settingsValidation);
  }
  if (!settings.enabled) {
    return lookupFailure(failure(OnlineMetadataError::disabled,
      "Online metadata lookup is disabled."));
  }
  if (!lowercaseHex(game.sha256, 64U) || cacheDirectory.empty()) {
    return lookupFailure(failure(OnlineMetadataError::invalidRequest,
      "Online metadata lookup requires a game SHA-256 and cache directory."));
  }
  const auto slug = systemSlug(game.system);
  if (slug.empty() || (settings.provider == OnlineMetadataProvider::retronian &&
      game.system != GameSystem::genesis)) {
    return lookupFailure(failure(OnlineMetadataError::unsupportedSystem,
      settings.provider == OnlineMetadataProvider::retronian
        ? "Retronian GameDB currently supports Genesis / Mega Drive lookup only."
        : "The game system is not supported by the metadata protocol."));
  }
  const auto providerRoot = providerCacheDirectory(cacheDirectory, settings);
  const auto records = providerRoot / "records";
  const auto artworkDirectory = providerRoot / "artwork";
  const auto recordPath = records / (game.sha256 + ".json");
  std::error_code error;
  if (std::filesystem::is_regular_file(recordPath, error) && !error &&
      fresh(recordPath, recordFreshness)) {
    const auto cached = cachedResult(
      recordPath, artworkDirectory, game.sha256, false);
    if (cached.status) {
      return cached;
    }
  }
  const bool staleAvailable = std::filesystem::is_regular_file(recordPath, error) &&
    !error;
  if (cancellation && cancellation()) {
    return lookupFailure(failure(OnlineMetadataError::cancelled,
      "Online metadata lookup was cancelled."));
  }

  OnlineMetadataDecodeResult decoded;
  if (settings.provider == OnlineMetadataProvider::retronian) {
    const auto indexPath = providerRoot / "retronian-md-index.json";
    const auto indexUrl = appendPath(QUrl{QString::fromStdString(settings.endpoint)},
      {QStringLiteral("api"), QStringLiteral("v1"),
        QStringLiteral("rom-index"), QStringLiteral("md.json")});
    std::optional<std::vector<std::uint8_t>> index;
    bool indexFromCache = false;
    bool cacheDownloadedIndex = false;
    if (fresh(indexPath, indexFreshness)) {
      index = readCache(indexPath, maximumOnlineIndexBytes);
      indexFromCache = index.has_value();
    }
    const auto downloadIndex = [&]() {
      return transport.get(indexUrl.toString(QUrl::FullyEncoded).toStdString(),
        maximumOnlineIndexBytes);
    };
    const auto useDownloadedIndex = [&](OnlineHttpResult response)
        -> OnlineMetadataStatus {
      if (!response.status || response.statusCode != 200) {
        return response.status ? failure(OnlineMetadataError::transportFailed,
          "The metadata provider rejected its ROM index request (HTTP " +
            std::to_string(response.statusCode) + ").") : response.status;
      }
      index = std::move(response.data);
      indexFromCache = false;
      cacheDownloadedIndex = true;
      return {};
    };
    if (!index) {
      const auto downloaded = useDownloadedIndex(downloadIndex());
      if (!downloaded) {
        if (staleAvailable) {
          return cachedResult(recordPath, artworkDirectory, game.sha256, true);
        }
        return lookupFailure(downloaded);
      }
    }
    std::string gameId;
    auto identified = identifyRetronianGame(*index, game.sha256, gameId);
    if (!identified && indexFromCache &&
        identified.error == OnlineMetadataError::invalidResponse) {
      const auto downloaded = useDownloadedIndex(downloadIndex());
      if (!downloaded) {
        if (staleAvailable) {
          return cachedResult(recordPath, artworkDirectory, game.sha256, true);
        }
        return lookupFailure(downloaded);
      }
      identified = identifyRetronianGame(*index, game.sha256, gameId);
    }
    if (!identified) {
      return lookupFailure(identified);
    }
    if (cacheDownloadedIndex) {
      const auto cached = writeFileAtomically(indexPath, *index,
        maximumOnlineIndexBytes);
      if (!cached) {
        return lookupFailure(failure(OnlineMetadataError::cacheFailed,
          "The Retronian ROM index could not be cached: " + cached.message));
      }
    }
    const auto gameUrl = appendPath(QUrl{QString::fromStdString(settings.endpoint)},
      {QStringLiteral("api"), QStringLiteral("v1"), QStringLiteral("games"),
        QStringLiteral("md"), QString::fromStdString(gameId) +
          QStringLiteral(".json")});
    auto response = transport.get(gameUrl.toString(QUrl::FullyEncoded).toStdString(),
      maximumOnlineMetadataBytes);
    if (!response.status || response.statusCode != 200) {
      if (staleAvailable) {
        return cachedResult(recordPath, artworkDirectory, game.sha256, true);
      }
      return lookupFailure(response.status ? failure(OnlineMetadataError::transportFailed,
        "The metadata provider rejected its game request (HTTP " +
          std::to_string(response.statusCode) + ").") : response.status);
    }
    decoded = decodeRetronianGameResponse(response.data, game.sha256,
      settings.preferredLanguage, settings.preferredRegion);
    if (decoded.status) {
      decoded.record.attribution.sourceUrl =
        gameUrl.toString(QUrl::FullyEncoded).toStdString();
    }
  } else {
    const auto url = appendPath(QUrl{QString::fromStdString(settings.endpoint)},
      {QStringLiteral("v1"), QStringLiteral("games"), QString::fromStdString(slug),
        QString::fromStdString(game.sha256) + QStringLiteral(".json")});
    auto response = transport.get(url.toString(QUrl::FullyEncoded).toStdString(),
      maximumOnlineMetadataBytes);
    if (!response.status || response.statusCode != 200) {
      if (staleAvailable) {
        return cachedResult(recordPath, artworkDirectory, game.sha256, true);
      }
      if (response.statusCode == 404) {
        return lookupFailure(failure(OnlineMetadataError::notFound,
          "The metadata provider has no exact match for this game."));
      }
      return lookupFailure(response.status ? failure(OnlineMetadataError::transportFailed,
        "The metadata provider rejected the lookup (HTTP " +
          std::to_string(response.statusCode) + ").") : response.status);
    }
    decoded = decodeLicensedManifestResponse(response.data, game.sha256);
  }
  if (!decoded.status) {
    return lookupFailure(decoded.status);
  }

  std::filesystem::path artworkPath;
  if (settings.downloadArtwork && decoded.record.artwork) {
    if (cancellation && cancellation()) {
      return lookupFailure(failure(OnlineMetadataError::cancelled,
        "Online metadata lookup was cancelled."));
    }
    auto artwork = transport.get(decoded.record.artwork->url,
      maximumOnlineArtworkBytes);
    if (!artwork.status || artwork.statusCode != 200) {
      return lookupFailure(artwork.status ? failure(OnlineMetadataError::transportFailed,
        "The licensed artwork download failed (HTTP " +
          std::to_string(artwork.statusCode) + ").") : artwork.status);
    }
    const auto validated = validateArtworkBytes(*decoded.record.artwork, artwork);
    if (!validated) {
      return lookupFailure(validated);
    }
    artworkPath = artworkDirectory /
      (game.sha256 + extensionForMime(decoded.record.artwork->mimeType));
    const auto stored = writeFileAtomically(artworkPath, artwork.data,
      maximumOnlineArtworkBytes);
    if (!stored) {
      return lookupFailure(failure(OnlineMetadataError::cacheFailed,
        "Licensed artwork could not be cached: " + stored.message));
    }
  }
  const auto encoded = encodeOnlineMetadataRecord(decoded.record);
  const auto stored = writeFileAtomically(recordPath, encoded,
    maximumOnlineMetadataBytes);
  if (!stored) {
    return lookupFailure(failure(OnlineMetadataError::cacheFailed,
      "Online metadata could not be cached: " + stored.message));
  }
  pruneCache(cacheDirectory / "online-metadata",
    static_cast<std::uint64_t>(settings.cacheMegabytes) * 1024U * 1024U);
  return {.status = {}, .record = std::move(decoded.record),
    .artworkPath = std::move(artworkPath)};
}

} // namespace genplusgx::library
