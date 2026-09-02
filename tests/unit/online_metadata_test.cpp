#include "genplusgx/library/online_metadata_client.h"
#include "genplusgx/library/online_metadata_service.h"
#include "genplusgx/library/online_metadata_settings.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace genplusgx::library;

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::vector<std::uint8_t> json(const QJsonObject& object)
{
  const auto bytes = QJsonDocument{object}.toJson(QJsonDocument::Compact);
  return {reinterpret_cast<const std::uint8_t*>(bytes.constData()),
    reinterpret_cast<const std::uint8_t*>(bytes.constData() + bytes.size())};
}

std::string digest(std::span<const std::uint8_t> data)
{
  QCryptographicHash hash{QCryptographicHash::Sha256};
  hash.addData(QByteArrayView{reinterpret_cast<const char*>(data.data()),
    static_cast<qsizetype>(data.size())});
  return hash.result().toHex().toStdString();
}

std::vector<std::uint8_t> png()
{
  QImage image{8, 12, QImage::Format_ARGB32};
  image.fill(QColor{25, 75, 150});
  QByteArray data;
  QBuffer buffer{&data};
  if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
    return {};
  }
  return {reinterpret_cast<const std::uint8_t*>(data.constData()),
    reinterpret_cast<const std::uint8_t*>(data.constData() + data.size())};
}

OnlineHttpResult response(
  int statusCode,
  std::string contentType,
  std::vector<std::uint8_t> data)
{
  return {.status = {}, .statusCode = statusCode,
    .contentType = std::move(contentType), .data = std::move(data)};
}

struct TransportState final {
  std::mutex mutex;
  std::map<std::string, OnlineHttpResult> responses;
  std::vector<std::string> requests;
};

class FakeTransport final : public OnlineHttpTransport {
public:
  explicit FakeTransport(std::shared_ptr<TransportState> state)
    : state_(std::move(state))
  {
  }

  OnlineHttpResult get(const std::string& url, std::size_t maximumBytes) override
  {
    std::scoped_lock lock{state_->mutex};
    state_->requests.push_back(url);
    const auto found = state_->responses.find(url);
    if (found == state_->responses.end()) {
      return {.status = {OnlineMetadataError::transportFailed,
        "Synthetic transport failure."}, .statusCode = 0,
        .contentType = {}, .data = {}};
    }
    auto result = found->second;
    if (result.data.size() > maximumBytes) {
      return {.status = {OnlineMetadataError::dataTooLarge,
        "Synthetic response exceeds limit."}, .statusCode = result.statusCode,
        .contentType = {}, .data = {}};
    }
    return result;
  }

private:
  std::shared_ptr<TransportState> state_;
};

struct BlockingTransportState final {
  std::mutex mutex;
  std::condition_variable condition;
  bool entered{false};
  bool released{false};
};

class BlockingTransport final : public OnlineHttpTransport {
public:
  BlockingTransport(std::shared_ptr<TransportState> responses,
    std::shared_ptr<BlockingTransportState> blocking)
    : responses_(std::move(responses)), blocking_(std::move(blocking))
  {
  }

  OnlineHttpResult get(const std::string& url, std::size_t maximumBytes) override
  {
    {
      std::unique_lock lock{blocking_->mutex};
      blocking_->entered = true;
      blocking_->condition.notify_all();
      blocking_->condition.wait(lock, [this] { return blocking_->released; });
    }
    FakeTransport transport{responses_};
    return transport.get(url, maximumBytes);
  }

private:
  std::shared_ptr<TransportState> responses_;
  std::shared_ptr<BlockingTransportState> blocking_;
};

QJsonObject licensedManifest(
  const std::string& gameHash,
  const std::string& artworkHash,
  std::string license = "CC-BY-4.0",
  std::string artworkMime = "image/png")
{
  return {
    {QStringLiteral("schemaVersion"), 1},
    {QStringLiteral("provider"), QJsonObject{
      {QStringLiteral("name"), QStringLiteral("Fixture Metadata")},
      {QStringLiteral("homepage"), QStringLiteral("https://provider.example.test")},
      {QStringLiteral("creator"), QStringLiteral("Fixture contributors")},
      {QStringLiteral("license_spdx"), QString::fromStdString(license)},
      {QStringLiteral("license_url"),
        QStringLiteral("https://creativecommons.org/licenses/by/4.0/")},
    }},
    {QStringLiteral("game"), QJsonObject{
      {QStringLiteral("sha256"), QString::fromStdString(gameHash)},
      {QStringLiteral("title"), QStringLiteral("Fixture Adventure")},
      {QStringLiteral("alternate_title"), QStringLiteral("Fixture Quest")},
      {QStringLiteral("description"), QStringLiteral("A deterministic fixture.")},
      {QStringLiteral("release_date"), QStringLiteral("1994-01-02")},
      {QStringLiteral("developer"), QStringLiteral("Fixture Studio")},
      {QStringLiteral("publisher"), QStringLiteral("Fixture Publisher")},
      {QStringLiteral("genres"), QJsonArray{
        QStringLiteral("Role-playing"), QStringLiteral("Adventure")}},
      {QStringLiteral("source_url"),
        QStringLiteral("https://provider.example.test/games/fixture")},
      {QStringLiteral("artwork"), QJsonObject{
        {QStringLiteral("url"),
          QStringLiteral("https://assets.example.test/fixture.png")},
        {QStringLiteral("sha256"), QString::fromStdString(artworkHash)},
        {QStringLiteral("mime_type"), QString::fromStdString(artworkMime)},
        {QStringLiteral("creator"), QStringLiteral("Fixture Artist")},
        {QStringLiteral("license_spdx"), QStringLiteral("CC0-1.0")},
        {QStringLiteral("license_url"),
          QStringLiteral("https://creativecommons.org/publicdomain/zero/1.0/")},
        {QStringLiteral("source_url"),
          QStringLiteral("https://assets.example.test/source")},
      }},
    }},
  };
}

QJsonObject retronianIndex(const std::string& gameHash)
{
  return {{QStringLiteral("by_sha256"), QJsonObject{
    {QString::fromStdString(gameHash), QJsonArray{
      QJsonObject{{QStringLiteral("game_id"),
        QStringLiteral("fixture-adventure")}}}},
  }}};
}

QJsonObject retronianGame(const std::string& gameHash)
{
  return {
    {QStringLiteral("id"), QStringLiteral("fixture-adventure")},
    {QStringLiteral("titles"), QJsonArray{
      QJsonObject{{QStringLiteral("text"), QStringLiteral("Fixture Adventure")},
        {QStringLiteral("lang"), QStringLiteral("en")},
        {QStringLiteral("region"), QStringLiteral("us")}},
      QJsonObject{{QStringLiteral("text"), QStringLiteral("フィクスチャ")},
        {QStringLiteral("lang"), QStringLiteral("ja")},
        {QStringLiteral("region"), QStringLiteral("jp")}},
    }},
    {QStringLiteral("roms"), QJsonArray{
      QJsonObject{{QStringLiteral("sha256"), QString::fromStdString(gameHash)}}}},
    {QStringLiteral("first_release_date"), QStringLiteral("1994-01-02")},
    {QStringLiteral("developers"), QJsonArray{QStringLiteral("fixture-studio")}},
    {QStringLiteral("publishers"), QJsonArray{QStringLiteral("fixture-publisher")}},
    {QStringLiteral("genres"), QJsonArray{QStringLiteral("role-playing")}},
    {QStringLiteral("descriptions"), QJsonArray{
      QJsonObject{{QStringLiteral("text"), QStringLiteral("English fixture.")},
        {QStringLiteral("lang"), QStringLiteral("en")}},
      QJsonObject{{QStringLiteral("text"), QStringLiteral("日本語")},
        {QStringLiteral("lang"), QStringLiteral("ja")}},
    }},
    {QStringLiteral("media"), QJsonArray{
      QJsonObject{{QStringLiteral("kind"), QStringLiteral("boxart")},
        {QStringLiteral("url"),
          QStringLiteral("https://unlicensed.example.test/box.png")},
        {QStringLiteral("source"), QStringLiteral("unlicensed")}},
    }},
  };
}

} // namespace

int main(int argc, char* argv[])
{
  QCoreApplication application{argc, argv};
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "Temporary directory was unavailable")) {
    return 1;
  }
  const std::filesystem::path root{temporary.path().toStdString()};
  const std::string gameHash(64U, 'a');
  const auto artwork = png();
  if (!check(!artwork.empty(), "PNG fixture generation failed")) {
    return 2;
  }

  auto settings = defaultOnlineMetadataSettings();
  if (!check(!settings.enabled && !settings.automaticLookup &&
      settings.provider == OnlineMetadataProvider::retronian &&
      validateOnlineMetadataSettings(settings),
      "Online metadata defaults are not private and valid") ||
      !check(isApprovedContentLicense("CC-BY-SA-4.0") &&
        !isApprovedContentLicense("LicenseRef-Proprietary"),
        "Content license allowlist is incorrect")) {
    return 3;
  }
  auto invalid = settings;
  invalid.automaticLookup = true;
  if (!check(!validateOnlineMetadataSettings(invalid),
      "Automatic lookup was allowed while disabled")) {
    return 4;
  }
  invalid = settings;
  invalid.endpoint = "http://insecure.example.test";
  if (!check(!validateOnlineMetadataSettings(invalid),
      "Insecure provider endpoint was accepted")) {
    return 5;
  }

  OnlineMetadataSettingsStore store{root / "config/online-metadata.json"};
  settings.enabled = true;
  settings.provider = OnlineMetadataProvider::licensedManifest;
  settings.endpoint = "https://metadata.example.test/api";
  settings.preferredRegion = "us";
  settings.downloadArtwork = true;
  if (!check(store.save(settings), "Online metadata settings were not saved") ||
      !check(store.load().status && store.load().settings == settings,
        "Online metadata settings did not round-trip")) {
    return 6;
  }

  const auto manifestBytes = json(licensedManifest(
    gameHash, digest(artwork)));
  const auto decoded = decodeLicensedManifestResponse(manifestBytes, gameHash);
  auto mismatchedLicense = decoded.record;
  mismatchedLicense.attribution.licenseSpdx = "CC0-1.0";
  auto fragmentedArtworkUrl = decoded.record;
  fragmentedArtworkUrl.artwork->url += "#ignored-fragment";
  if (!check(decoded.status && decoded.record.preferredTitle ==
        "Fixture Adventure" && decoded.record.artwork &&
        decoded.record.artwork->attribution.licenseSpdx == "CC0-1.0",
      "Licensed manifest did not decode") ||
      !check(!decodeLicensedManifestResponse(
        json(licensedManifest(gameHash, digest(artwork), "Proprietary")),
        gameHash).status,
        "Unapproved metadata license was accepted") ||
      !check(!decodeLicensedManifestResponse(manifestBytes,
        std::string(64U, 'b')).status,
        "Wrong-game online metadata was accepted") ||
      !check(!validateOnlineMetadataRecord(mismatchedLicense),
        "A mismatched SPDX identifier and license URL were accepted") ||
      !check(!validateOnlineMetadataRecord(fragmentedArtworkUrl),
        "An artwork request URL with a fragment was accepted")) {
    return 7;
  }
  const auto canonical = encodeOnlineMetadataRecord(decoded.record);
  if (!check(!canonical.empty() &&
      decodeOnlineMetadataRecord(canonical).record == decoded.record,
      "Canonical online metadata serialization did not round-trip")) {
    return 8;
  }
  const std::vector<std::vector<std::uint8_t>> malformedCorpus{
    {}, {'n', 'u', 'l', 'l'}, {'[', ']', '\0'}, {'{', '"', 'x', '"', ':', '1'},
    std::vector<std::uint8_t>(maximumOnlineMetadataBytes + 1U, 'x')};
  for (const auto& malformed : malformedCorpus) {
    if (!check(!decodeOnlineMetadataRecord(malformed).status &&
        !decodeLicensedManifestResponse(malformed, gameHash).status,
        "Malformed online metadata corpus entry was accepted")) {
      return 9;
    }
  }

  std::string retronianId;
  const auto indexBytes = json(retronianIndex(gameHash));
  if (!check(identifyRetronianGame(indexBytes, gameHash, retronianId) &&
      retronianId == "fixture-adventure",
      "Retronian hash identification failed")) {
    return 10;
  }
  const auto retronian = decodeRetronianGameResponse(
    json(retronianGame(gameHash)), gameHash, "ja", "jp");
  auto undatedRetronian = retronianGame(gameHash);
  undatedRetronian.insert(QStringLiteral("first_release_date"), QJsonValue::Null);
  auto licensedRetronian = retronianGame(gameHash);
  licensedRetronian.insert(QStringLiteral("media"), QJsonArray{
    QJsonObject{
      {QStringLiteral("kind"), QStringLiteral("boxart")},
      {QStringLiteral("url"),
        QStringLiteral("https://licensed.example.test/box.png")},
      {QStringLiteral("sha256"), QString::fromStdString(digest(artwork))},
      {QStringLiteral("mime_type"), QStringLiteral("image/png")},
      {QStringLiteral("creator"), QStringLiteral("Fixture Artist")},
      {QStringLiteral("source_url"),
        QStringLiteral("https://licensed.example.test/source")},
      {QStringLiteral("region"), QStringLiteral("jp")},
      {QStringLiteral("license"), QJsonObject{
        {QStringLiteral("spdx"), QStringLiteral("CC0-1.0")},
        {QStringLiteral("url"), QStringLiteral(
          "https://creativecommons.org/publicdomain/zero/1.0/")},
      }},
    },
  });
  const auto retronianWithLicensedArtwork = decodeRetronianGameResponse(
    json(licensedRetronian), gameHash, "ja", "jp");
  if (!check(retronian.status && retronian.record.preferredTitle ==
        "フィクスチャ" && retronian.record.alternateTitle ==
        "Fixture Adventure" && retronian.record.description == "日本語" &&
        !retronian.record.artwork,
      "Retronian metadata selection or unlicensed-art rejection failed") ||
      !check(retronianWithLicensedArtwork.status &&
        retronianWithLicensedArtwork.record.artwork &&
        retronianWithLicensedArtwork.record.artwork->attribution.licenseSpdx ==
          "CC0-1.0" &&
        retronianWithLicensedArtwork.record.artwork->url ==
          "https://licensed.example.test/box.png",
        "Properly licensed Retronian artwork was rejected") ||
      !check(decodeRetronianGameResponse(json(undatedRetronian), gameHash,
        "en", "us").status,
        "A valid Retronian record with no known release date was rejected")) {
    return 11;
  }

  auto retronianSettings = defaultOnlineMetadataSettings();
  retronianSettings.enabled = true;
  retronianSettings.preferredLanguage = "ja";
  retronianSettings.preferredRegion = "jp";
  auto retronianTransportState = std::make_shared<TransportState>();
  const std::string retronianIndexUrl =
    "https://gamedb.retronian.com/api/v1/rom-index/md.json";
  const std::string retronianGameUrl =
    "https://gamedb.retronian.com/api/v1/games/md/fixture-adventure.json";
  retronianTransportState->responses.emplace(retronianIndexUrl,
    response(200, "application/json", indexBytes));
  retronianTransportState->responses.emplace(retronianGameUrl,
    response(200, "application/json", json(retronianGame(gameHash))));
  GameMetadata retronianMetadata;
  retronianMetadata.system = GameSystem::genesis;
  retronianMetadata.sha256 = gameHash;
  FakeTransport retronianTransport{retronianTransportState};
  const auto retronianLookup = lookupOnlineMetadata(retronianSettings,
    retronianMetadata, root / "retronian-cache", retronianTransport);
  if (!check(retronianLookup.status && !retronianLookup.fromCache &&
      retronianLookup.record.preferredTitle == "フィクスチャ" &&
      retronianLookup.record.attribution.sourceUrl == retronianGameUrl,
      "Retronian full lookup did not preserve exact-match attribution")) {
    return 12;
  }
  std::filesystem::path cachedIndex;
  std::filesystem::path cachedRecord;
  for (const auto& entry : std::filesystem::recursive_directory_iterator{
         root / "retronian-cache"}) {
    if (entry.path().filename() == "retronian-md-index.json") {
      cachedIndex = entry.path();
    } else if (entry.path().filename() == gameHash + ".json") {
      cachedRecord = entry.path();
    }
  }
  {
    std::ofstream invalidIndex{cachedIndex, std::ios::binary | std::ios::trunc};
    invalidIndex << "{broken";
  }
  std::error_code removeError;
  const bool removedRecord = std::filesystem::remove(cachedRecord, removeError);
  {
    std::scoped_lock lock{retronianTransportState->mutex};
    retronianTransportState->requests.clear();
  }
  const auto recoveredIndexLookup = lookupOnlineMetadata(retronianSettings,
    retronianMetadata, root / "retronian-cache", retronianTransport);
  {
    std::scoped_lock lock{retronianTransportState->mutex};
    if (!check(!cachedIndex.empty() && !cachedRecord.empty() &&
        removedRecord && !removeError && recoveredIndexLookup.status &&
        retronianTransportState->requests ==
          std::vector<std::string>{retronianIndexUrl, retronianGameUrl},
        "A corrupt cached Retronian index was not refreshed safely")) {
      return 13;
    }
  }

  auto transportState = std::make_shared<TransportState>();
  const auto lookupUrl = "https://metadata.example.test/api/v1/games/genesis/" +
    gameHash + ".json";
  const std::string artworkUrl = "https://assets.example.test/fixture.png";
  transportState->responses.emplace(lookupUrl,
    response(200, "application/json", manifestBytes));
  transportState->responses.emplace(artworkUrl,
    response(200, "image/png", artwork));
  GameMetadata game;
  game.system = GameSystem::genesis;
  game.sha256 = gameHash;
  FakeTransport transport{transportState};
  const auto lookedUp = lookupOnlineMetadata(
    settings, game, root / "cache", transport);
  if (!check(lookedUp.status && !lookedUp.fromCache &&
      lookedUp.record.preferredTitle == "Fixture Adventure" &&
      std::filesystem::is_regular_file(lookedUp.artworkPath),
      "Licensed metadata/artwork lookup failed")) {
    return 14;
  }
  auto incompleteArtwork = artwork;
  incompleteArtwork.resize(incompleteArtwork.size() - 20U);
  QByteArray incompleteBytes{
    reinterpret_cast<const char*>(incompleteArtwork.data()),
    static_cast<qsizetype>(incompleteArtwork.size())};
  QBuffer incompleteBuffer{&incompleteBytes};
  incompleteBuffer.open(QIODevice::ReadOnly);
  QImageReader incompleteReader{&incompleteBuffer};
  incompleteReader.setDecideFormatFromContent(true);
  const auto incompleteSize = incompleteReader.size();
  const auto incompleteReadableHeader = incompleteReader.canRead();
  const auto incompleteImage = incompleteReader.read();
  if (!check(incompleteSize.isValid() && incompleteReadableHeader &&
      incompleteImage.isNull(),
      "The intentionally incomplete PNG did not exercise full decoding")) {
    return 25;
  }
  auto incompleteState = std::make_shared<TransportState>();
  incompleteState->responses.emplace(lookupUrl,
    response(200, "application/json", json(licensedManifest(
      gameHash, digest(incompleteArtwork)))));
  incompleteState->responses.emplace(artworkUrl,
    response(200, "image/png", incompleteArtwork));
  FakeTransport incompleteTransport{incompleteState};
  const auto incompleteLookup = lookupOnlineMetadata(settings, game,
    root / "incomplete-artwork-cache", incompleteTransport);
  if (!check(!incompleteLookup.status &&
      incompleteLookup.status.error == OnlineMetadataError::invalidResponse,
      "Artwork with a readable header but incomplete image data was accepted")) {
    return 26;
  }
  auto mismatchedFormatState = std::make_shared<TransportState>();
  mismatchedFormatState->responses.emplace(lookupUrl,
    response(200, "application/json", json(licensedManifest(
      gameHash, digest(artwork), "CC-BY-4.0", "image/jpeg"))));
  mismatchedFormatState->responses.emplace(artworkUrl,
    response(200, "image/jpeg", artwork));
  FakeTransport mismatchedFormatTransport{mismatchedFormatState};
  const auto mismatchedFormatLookup = lookupOnlineMetadata(settings, game,
    root / "mismatched-format-cache", mismatchedFormatTransport);
  if (!check(!mismatchedFormatLookup.status &&
      mismatchedFormatLookup.status.error == OnlineMetadataError::invalidResponse,
      "Artwork whose bytes disagree with its declared MIME type was accepted")) {
    return 27;
  }
  {
    std::scoped_lock lock{transportState->mutex};
    transportState->responses.clear();
    transportState->requests.clear();
  }
  const auto cached = lookupOnlineMetadata(settings, game, root / "cache", transport);
  {
    std::scoped_lock lock{transportState->mutex};
    if (!check(cached.status && cached.fromCache && !cached.staleCache &&
        transportState->requests.empty() &&
        std::filesystem::is_regular_file(cached.artworkPath),
        "Fresh validated cache did not avoid the network")) {
      return 15;
    }
  }
  {
    std::ofstream corruptArtwork{
      lookedUp.artworkPath, std::ios::binary | std::ios::trunc};
    corruptArtwork << "not an image";
    corruptArtwork.close();
    if (!check(static_cast<bool>(corruptArtwork),
        "Cached artwork corruption could not be staged")) {
      return 16;
    }
  }
  const auto cacheWithCorruptArtwork = lookupOnlineMetadata(
    settings, game, root / "cache", transport);
  if (!check(cacheWithCorruptArtwork.status &&
      cacheWithCorruptArtwork.fromCache &&
      cacheWithCorruptArtwork.artworkPath.empty(),
      "Corrupt cached artwork was trusted by a metadata cache hit")) {
    return 17;
  }

  auto serviceState = std::make_shared<TransportState>();
  serviceState->responses.emplace(lookupUrl,
    response(200, "application/json", manifestBytes));
  serviceState->responses.emplace(artworkUrl,
    response(200, "image/png", artwork));
  OnlineMetadataService service{1U, 4U,
    [serviceState](OnlineMetadataCancellation) {
      return std::make_unique<FakeTransport>(serviceState);
    }};
  if (!check(service.start(), "Online metadata service did not start") ||
      !check(service.waitForEvent(std::chrono::seconds{2}).has_value(),
        "Online metadata service did not publish startup")) {
    return 18;
  }
  const auto serviceCache = root / "service-cache";
  if (!check(service.request(1U, 42, settings, game, serviceCache),
      "Online metadata service rejected a valid request")) {
    return 19;
  }
  const auto event = service.waitForEvent(std::chrono::seconds{3});
  if (!check(event && event->type == OnlineMetadataEventType::lookupCompleted &&
      event->operationId == 1U && event->libraryGameId == 42 &&
      event->result.status,
      "Online metadata service did not complete its request") ||
      !check(service.stop(), "Online metadata service did not stop cleanly")) {
    return 20;
  }

  const auto boundedCache = root / "bounded-service-cache";
  auto blockingState = std::make_shared<BlockingTransportState>();
  auto boundedResponses = std::make_shared<TransportState>();
  boundedResponses->responses.emplace(lookupUrl,
    response(200, "application/json", manifestBytes));
  boundedResponses->responses.emplace(artworkUrl,
    response(200, "image/png", artwork));
  OnlineMetadataService boundedService{1U, 4U,
    [boundedResponses, blockingState](OnlineMetadataCancellation) {
      return std::make_unique<BlockingTransport>(
        boundedResponses, blockingState);
    }};
  if (!check(boundedService.start() &&
      boundedService.waitForEvent(std::chrono::seconds{2}).has_value() &&
      boundedService.request(101U, 101, settings, game, boundedCache),
      "Bounded online metadata service did not accept its first request")) {
    return 21;
  }
  {
    std::unique_lock lock{blockingState->mutex};
    if (!check(blockingState->condition.wait_for(lock,
          std::chrono::seconds{2}, [&] { return blockingState->entered; }),
        "Bounded service request did not enter its transport")) {
      return 22;
    }
  }
  const auto queued = boundedService.request(
    102U, 102, settings, game, boundedCache);
  const auto overflow = boundedService.request(
    103U, 103, settings, game, boundedCache);
  if (!check(queued && overflow.error == OnlineMetadataError::queueFull,
      "Bounded service command queue did not report saturation")) {
    return 23;
  }
  {
    std::scoped_lock lock{blockingState->mutex};
    blockingState->released = true;
    blockingState->condition.notify_all();
  }
  std::size_t completions = 0U;
  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::seconds{3};
  while (completions < 2U && std::chrono::steady_clock::now() < deadline) {
    const auto completed = boundedService.waitForEvent(
      std::chrono::milliseconds{100});
    if (completed && completed->type == OnlineMetadataEventType::lookupCompleted) {
      ++completions;
    }
  }
  if (!check(completions == 2U && boundedService.stop(),
      "Bounded service did not drain and join after saturation")) {
    return 24;
  }

  return 0;
}
