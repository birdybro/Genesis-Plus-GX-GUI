#include "genplusgx/updates/update_manifest.h"

#include <monocypher-ed25519.h>

#include <QByteArray>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <set>
#include <utility>

namespace genplusgx::updates {
namespace {

Status failure(Error error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

ManifestResult manifestFailure(Error error, std::string message)
{
  return {.status = failure(error, std::move(message)), .manifest = {}};
}

bool lowercaseHex(const std::string& text, std::size_t length)
{
  return text.size() == length && std::ranges::all_of(text,
    [](const unsigned char value) {
      return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
    });
}

std::optional<std::string> strictString(
  const QJsonObject& object, const char* name, std::size_t maximum)
{
  const auto value = object.value(QString::fromLatin1(name));
  if (!value.isString()) {
    return std::nullopt;
  }
  auto result = value.toString().toStdString();
  if (result.empty() || result.size() > maximum ||
      result.find('\0') != std::string::npos) {
    return std::nullopt;
  }
  return result;
}

bool exactKeys(const QJsonObject& object, std::initializer_list<const char*> keys)
{
  if (object.size() != static_cast<qsizetype>(keys.size())) {
    return false;
  }
  return std::ranges::all_of(keys, [&object](const char* key) {
    return object.contains(QString::fromLatin1(key));
  });
}

std::optional<std::array<std::uint8_t, 32>> publicKey(const Trust& trust)
{
  const auto decoded = QByteArray::fromHex(QByteArray::fromStdString(
    trust.publicKeyHex));
  if (decoded.size() != 32 || decoded.toHex().toStdString() != trust.publicKeyHex) {
    return std::nullopt;
  }
  std::array<std::uint8_t, 32> result{};
  std::ranges::copy(decoded, reinterpret_cast<char*>(result.data()));
  return result;
}

std::optional<std::array<std::uint8_t, 64>> signature(
  std::span<const std::uint8_t> file)
{
  if (file.size() != 89U || file.back() != static_cast<std::uint8_t>('\n')) {
    return std::nullopt;
  }
  QByteArray encoded{reinterpret_cast<const char*>(file.data()),
    static_cast<qsizetype>(file.size())};
  encoded.chop(1);
  if (encoded.size() != 88 || encoded.contains('\r') || encoded.contains('\n') ||
      encoded.contains(' ') || encoded.contains('\t')) {
    return std::nullopt;
  }
  const auto decoded = QByteArray::fromBase64(encoded);
  if (decoded.size() != 64 || decoded.toBase64() != encoded) {
    return std::nullopt;
  }
  std::array<std::uint8_t, 64> result{};
  std::ranges::copy(decoded, reinterpret_cast<char*>(result.data()));
  return result;
}

bool validTimestamp(const std::string& text)
{
  const auto date = QDateTime::fromString(QString::fromStdString(text),
    Qt::ISODateWithMs);
  return date.isValid() && date.timeSpec() == Qt::UTC &&
    date.toString(Qt::ISODateWithMs).toStdString() == text;
}

bool safeFileName(const std::string& value)
{
  return !value.empty() && value.size() <= 255U && value != "." && value != ".." &&
    value.find('/') == std::string::npos && value.find('\\') == std::string::npos &&
    value.find('\0') == std::string::npos;
}

bool allowedFormat(const std::string& platform, const std::string& format)
{
  if (platform == "linux") {
    return format == "tar.gz";
  }
  if (platform == "windows") {
    return format == "zip";
  }
  if (platform == "macos") {
    return format == "zip" || format == "dmg";
  }
  return false;
}

} // namespace

Status validateTrustedUrl(const std::string& value, const Trust& trust,
  bool allowQuery)
{
  if (value.empty() || value.size() > 2'048U) {
    return failure(Error::invalidManifest, "An update URL is empty or too long.");
  }
  const QUrl url{QString::fromStdString(value), QUrl::StrictMode};
  const auto host = url.host(QUrl::FullyDecoded).toLower().toStdString();
  const auto port = url.port(-1);
  if (!url.isValid() || url.scheme() != QStringLiteral("https") ||
      url.userInfo().size() > 0 || url.hasFragment() ||
      (!allowQuery && url.hasQuery()) || url.path().isEmpty() ||
      (port != -1 && port != 443 && !trust.allowNonDefaultHttpsPorts) ||
      std::ranges::find(trust.allowedHosts, host) == trust.allowedHosts.end()) {
    return failure(Error::invalidManifest,
      "An update URL is not an approved HTTPS release URL.");
  }
  return {};
}

ManifestResult verifyAndParseManifest(
  std::span<const std::uint8_t> manifestBytes,
  std::span<const std::uint8_t> signatureFileBytes,
  const Trust& trust)
{
  if (manifestBytes.empty() || manifestBytes.size() > maximumManifestBytes) {
    return manifestFailure(Error::responseTooLarge,
      "The update manifest is empty or exceeds its size limit.");
  }
  const auto key = publicKey(trust);
  const auto signedBytes = signature(signatureFileBytes);
  if (!key || !signedBytes || crypto_ed25519_check(signedBytes->data(), key->data(),
        manifestBytes.data(), manifestBytes.size()) != 0) {
    return manifestFailure(Error::signatureInvalid,
      "The update manifest signature is invalid.");
  }

  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(QByteArray{
    reinterpret_cast<const char*>(manifestBytes.data()),
    static_cast<qsizetype>(manifestBytes.size())}, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return manifestFailure(Error::invalidManifest,
      "The signed update manifest is not valid JSON.");
  }
  auto canonical = document.toJson(QJsonDocument::Compact);
  canonical.append('\n');
  if (canonical.size() != static_cast<qsizetype>(manifestBytes.size()) ||
      !std::equal(canonical.begin(), canonical.end(), manifestBytes.begin())) {
    return manifestFailure(Error::invalidManifest,
      "The signed update manifest is not canonical JSON.");
  }
  const auto root = document.object();
  if (!exactKeys(root, {"assets", "keyId", "publishedAt", "releasePage",
        "schemaVersion", "version"}) ||
      !root.value(QStringLiteral("schemaVersion")).isDouble() ||
      root.value(QStringLiteral("schemaVersion")).toDouble() != 1.0 ||
      !root.value(QStringLiteral("assets")).isArray()) {
    return manifestFailure(Error::invalidManifest,
      "The signed update manifest schema is invalid.");
  }
  const auto versionText = strictString(root, "version", 32U);
  const auto keyId = strictString(root, "keyId", 64U);
  const auto publishedAt = strictString(root, "publishedAt", 64U);
  const auto releasePage = strictString(root, "releasePage", 2'048U);
  const auto version = versionText ? parseSemanticVersion(*versionText) : std::nullopt;
  if (!version || !keyId || *keyId != trust.keyId || !publishedAt ||
      !validTimestamp(*publishedAt) || !releasePage ||
      !validateTrustedUrl(*releasePage, trust)) {
    return manifestFailure(Error::invalidManifest,
      "The signed update manifest identity or release metadata is invalid.");
  }
  if (*releasePage != trust.repositoryUrl + "/releases/tag/v" +
      version->toString()) {
    return manifestFailure(Error::invalidManifest,
      "The signed release page does not belong to this project.");
  }

  const auto values = root.value(QStringLiteral("assets")).toArray();
  if (values.empty() || values.size() > 16) {
    return manifestFailure(Error::invalidManifest,
      "The signed update manifest has an invalid asset count.");
  }
  std::vector<Asset> assets;
  std::set<std::string> identities;
  assets.reserve(static_cast<std::size_t>(values.size()));
  for (const auto& value : values) {
    if (!value.isObject()) {
      return manifestFailure(Error::invalidManifest,
        "An update asset is not an object.");
    }
    const auto object = value.toObject();
    if (!exactKeys(object, {"architecture", "fileName", "format", "platform",
          "sha256", "size", "url"})) {
      return manifestFailure(Error::invalidManifest,
        "An update asset schema is invalid.");
    }
    const auto platform = strictString(object, "platform", 16U);
    const auto architecture = strictString(object, "architecture", 16U);
    const auto format = strictString(object, "format", 16U);
    const auto fileName = strictString(object, "fileName", 255U);
    const auto url = strictString(object, "url", 2'048U);
    const auto digest = strictString(object, "sha256", 64U);
    const auto sizeValue = object.value(QStringLiteral("size"));
    if (!platform || !architecture || !format || !fileName || !url || !digest ||
        !sizeValue.isDouble() || !std::isfinite(sizeValue.toDouble()) ||
        std::floor(sizeValue.toDouble()) != sizeValue.toDouble() ||
        sizeValue.toDouble() < 1.0 ||
        sizeValue.toDouble() > static_cast<double>(maximumPackageBytes) ||
        !(*platform == "linux" || *platform == "windows" || *platform == "macos") ||
        !(*architecture == "x86_64" || *architecture == "arm64") ||
        !allowedFormat(*platform, *format) || !safeFileName(*fileName) ||
        !lowercaseHex(*digest, 64U) || !validateTrustedUrl(*url, trust) ||
        *url != trust.repositoryUrl + "/releases/download/v" +
          version->toString() + "/" + *fileName) {
      return manifestFailure(Error::invalidManifest,
        "A signed update asset is invalid.");
    }
    const auto identity = *platform + '\n' + *architecture + '\n' + *format;
    if (!identities.insert(identity).second) {
      return manifestFailure(Error::invalidManifest,
        "The signed update manifest contains a duplicate asset.");
    }
    assets.push_back({.platform = *platform, .architecture = *architecture,
      .format = *format, .fileName = *fileName, .url = *url,
      .sha256 = *digest, .size = static_cast<std::uint64_t>(sizeValue.toDouble())});
  }
  return {.status = {}, .manifest = {.schemaVersion = 1U,
    .version = *version, .publishedAt = *publishedAt,
    .releasePage = *releasePage, .keyId = *keyId, .assets = std::move(assets)}};
}

std::optional<Asset> selectAsset(const Manifest& manifest,
  std::string_view platform, std::string_view architecture)
{
  const auto preferred = platform == "macos" ? "dmg" :
    (platform == "windows" ? "zip" : "tar.gz");
  const auto found = std::ranges::find_if(manifest.assets,
    [platform, architecture, preferred](const Asset& asset) {
      return asset.platform == platform && asset.architecture == architecture &&
        asset.format == preferred;
    });
  return found == manifest.assets.end() ? std::nullopt : std::optional{*found};
}

} // namespace genplusgx::updates
