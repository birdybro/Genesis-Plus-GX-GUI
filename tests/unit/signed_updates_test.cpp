#include "genplusgx/updates/update_client.h"
#include "genplusgx/updates/update_manifest.h"
#include "genplusgx/updates/update_service.h"
#include "genplusgx/updates/update_settings.h"

#include <monocypher-ed25519.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace genplusgx::updates;

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

bool productionKeyTests(const QString& publicKeyPath)
{
  QFile file{publicKeyPath};
  if (!check(file.open(QIODevice::ReadOnly),
        "production update public key could not be read")) {
    return false;
  }
  auto pem = file.readAll();
  pem.replace("-----BEGIN PUBLIC KEY-----", "");
  pem.replace("-----END PUBLIC KEY-----", "");
  pem.replace("\r", "");
  pem.replace("\n", "");
  const auto der = QByteArray::fromBase64(pem);
  const auto prefix = QByteArray::fromHex("302a300506032b6570032100");
  bool ok = check(der.size() == 44 && der.startsWith(prefix),
    "production update public key is not canonical Ed25519 SubjectPublicKeyInfo");
  if (!ok) {
    return false;
  }
  const auto raw = der.last(32);
  const auto trust = productionTrust();
  ok &= check(raw.toHex().toStdString() == trust.publicKeyHex,
    "production updater and published public key differ");
  const auto keyId = QCryptographicHash::hash(raw, QCryptographicHash::Sha256)
    .toHex().left(16).toStdString();
  ok &= check(keyId == trust.keyId,
    "production update key identity does not match its public key digest");
  return ok;
}

struct KeyPair final {
  std::array<std::uint8_t, 64> secret{};
  std::array<std::uint8_t, 32> publicKey{};
};

KeyPair testKey()
{
  // RFC 8032 section 7.1 test vector 1. This is public test material only.
  auto seedBytes = QByteArray::fromHex(
    "9d61b19deffd5a60ba844af492ec2cc4"
    "4449c5697b326919703bac031cae7f60");
  std::array<std::uint8_t, 32> seed{};
  std::copy(seedBytes.begin(), seedBytes.end(),
    reinterpret_cast<char*>(seed.data()));
  KeyPair result;
  crypto_ed25519_key_pair(result.secret.data(), result.publicKey.data(), seed.data());
  return result;
}

Trust testTrust(const KeyPair& key)
{
  const QByteArray publicBytes{reinterpret_cast<const char*>(key.publicKey.data()),
    static_cast<qsizetype>(key.publicKey.size())};
  return {
    .publicKeyHex = publicBytes.toHex().toStdString(),
    .keyId = "fixture-key",
    .manifestUrl = "https://updates.example.test/manifest.json",
    .signatureUrl = "https://updates.example.test/manifest.json.sig",
    .repositoryUrl = "https://updates.example.test/project",
    .allowedHosts = {"updates.example.test"},
  };
}

std::vector<std::uint8_t> manifest(const std::string& version = "9.8.7")
{
  const auto name = "Genesis-Plus-GX-GUI-" + version + "-linux-x86_64.tar.gz";
  const QJsonObject object{
    {QStringLiteral("assets"), QJsonArray{QJsonObject{
      {QStringLiteral("architecture"), QStringLiteral("x86_64")},
      {QStringLiteral("fileName"), QString::fromStdString(name)},
      {QStringLiteral("format"), QStringLiteral("tar.gz")},
      {QStringLiteral("platform"), QStringLiteral("linux")},
      {QStringLiteral("sha256"), QString(64, u'a')},
      {QStringLiteral("size"), 12345},
      {QStringLiteral("url"), QString::fromStdString(
        "https://updates.example.test/project/releases/download/v" + version +
          "/" + name)},
    }}},
    {QStringLiteral("keyId"), QStringLiteral("fixture-key")},
    {QStringLiteral("publishedAt"), QStringLiteral("2026-09-02T18:00:00.000Z")},
    {QStringLiteral("releasePage"), QString::fromStdString(
      "https://updates.example.test/project/releases/tag/v" + version)},
    {QStringLiteral("schemaVersion"), 1},
    {QStringLiteral("version"), QString::fromStdString(version)},
  };
  auto bytes = QJsonDocument{object}.toJson(QJsonDocument::Compact);
  bytes.append('\n');
  return {bytes.begin(), bytes.end()};
}

std::vector<std::uint8_t> canonicalBytes(const QJsonObject& object)
{
  auto bytes = QJsonDocument{object}.toJson(QJsonDocument::Compact);
  bytes.append('\n');
  return {bytes.begin(), bytes.end()};
}

std::vector<std::uint8_t> sign(std::span<const std::uint8_t> bytes,
  const KeyPair& key)
{
  std::array<std::uint8_t, 64> signature{};
  crypto_ed25519_sign(signature.data(), key.secret.data(), bytes.data(), bytes.size());
  const QByteArray raw{reinterpret_cast<const char*>(signature.data()),
    static_cast<qsizetype>(signature.size())};
  auto encoded = raw.toBase64();
  encoded.append('\n');
  return {encoded.begin(), encoded.end()};
}

class FakeTransport final : public HttpTransport {
public:
  std::map<std::string, HttpResult> responses;
  DownloadResult downloadResult;
  std::vector<std::string> requests;

  HttpResult get(const std::string& url, std::size_t maximumBytes,
    const Trust&) override
  {
    requests.push_back(url);
    const auto found = responses.find(url);
    if (found == responses.end() || found->second.data.size() > maximumBytes) {
      return {.status = {Error::network, "Synthetic response failure."},
        .statusCode = 0, .data = {}};
    }
    return found->second;
  }

  DownloadResult download(const Asset&, const std::filesystem::path&,
    const Trust&) override
  {
    return downloadResult;
  }
};

class BlockingTransport final : public HttpTransport {
public:
  BlockingTransport(Cancellation cancellation, std::atomic_bool& entered)
    : cancellation_(std::move(cancellation)), entered_(entered)
  {
  }

  HttpResult get(const std::string&, std::size_t, const Trust&) override
  {
    entered_.store(true, std::memory_order_release);
    while (!cancellation_()) {
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return {.status = {Error::cancelled, "Synthetic cancellation."},
      .statusCode = 0, .data = {}};
  }

  DownloadResult download(const Asset&, const std::filesystem::path&,
    const Trust&) override
  {
    return {.status = {Error::cancelled, "Synthetic cancellation."},
      .path = {}, .asset = {}};
  }

private:
  Cancellation cancellation_;
  std::atomic_bool& entered_;
};

bool manifestTests()
{
  const auto key = testKey();
  const auto trust = testTrust(key);
  const auto bytes = manifest();
  const std::string pythonCanonical =
    "{\"assets\":[{\"architecture\":\"x86_64\",\"fileName\":"
    "\"Genesis-Plus-GX-GUI-9.8.7-linux-x86_64.tar.gz\",\"format\":"
    "\"tar.gz\",\"platform\":\"linux\",\"sha256\":\"" +
    std::string(64U, 'a') +
    "\",\"size\":12345,\"url\":\"https://updates.example.test/project/"
    "releases/download/v9.8.7/Genesis-Plus-GX-GUI-9.8.7-linux-x86_64.tar.gz\"}],"
    "\"keyId\":\"fixture-key\",\"publishedAt\":\"2026-09-02T18:00:00.000Z\","
    "\"releasePage\":\"https://updates.example.test/project/releases/tag/v9.8.7\","
    "\"schemaVersion\":1,\"version\":\"9.8.7\"}\n";
  const auto signature = sign(bytes, key);
  const auto verified = verifyAndParseManifest(bytes, signature, trust);
  bool ok = true;
  const QByteArray publicBytes{reinterpret_cast<const char*>(key.publicKey.data()),
    static_cast<qsizetype>(key.publicKey.size())};
  ok &= check(publicBytes.toHex() == QByteArray{
      "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"},
    "RFC 8032 Ed25519 public key mismatch");
  ok &= check(std::vector<std::uint8_t>(pythonCanonical.begin(),
      pythonCanonical.end()) == bytes,
    "Qt and release-tool canonical JSON representations differ");
  std::array<std::uint8_t, 64> emptySignature{};
  const std::array<std::uint8_t, 1> emptyMessage{};
  crypto_ed25519_sign(emptySignature.data(), key.secret.data(),
    emptyMessage.data(), 0U);
  const QByteArray emptySignatureBytes{
    reinterpret_cast<const char*>(emptySignature.data()),
    static_cast<qsizetype>(emptySignature.size())};
  ok &= check(emptySignatureBytes.toHex() == QByteArray{
      "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
      "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
    "RFC 8032 Ed25519 signature mismatch");
  ok &= check(verified.status.ok(), "valid signed manifest should verify");
  ok &= check(verified.manifest.version.toString() == "9.8.7",
    "verified manifest version mismatch");
  ok &= check(verified.manifest.assets.size() == 1U,
    "verified manifest asset count mismatch");
  const QByteArray manifestData{reinterpret_cast<const char*>(bytes.data()),
    static_cast<qsizetype>(bytes.size())};
  auto wrongReleasePage = QJsonDocument::fromJson(manifestData).object();
  wrongReleasePage[QStringLiteral("releasePage")] = QStringLiteral(
    "https://updates.example.test/project/releases/tag/v9.8.7/notes");
  auto wrongReleaseBytes = canonicalBytes(wrongReleasePage);
  ok &= check(verifyAndParseManifest(wrongReleaseBytes,
      sign(wrongReleaseBytes, key), trust).status.error == Error::invalidManifest,
    "signed release page with a non-exact version path was accepted");
  auto wrongAssetUrl = QJsonDocument::fromJson(manifestData).object();
  auto assets = wrongAssetUrl[QStringLiteral("assets")].toArray();
  auto asset = assets.at(0).toObject();
  asset[QStringLiteral("url")] = QStringLiteral(
    "https://updates.example.test/project/releases/download/v9.8.7/extra/"
    "Genesis-Plus-GX-GUI-9.8.7-linux-x86_64.tar.gz");
  assets[0] = asset;
  wrongAssetUrl[QStringLiteral("assets")] = assets;
  auto wrongAssetBytes = canonicalBytes(wrongAssetUrl);
  ok &= check(verifyAndParseManifest(wrongAssetBytes,
      sign(wrongAssetBytes, key), trust).status.error == Error::invalidManifest,
    "signed asset with a non-exact release path was accepted");
  auto tampered = bytes;
  tampered[10] ^= 1U;
  ok &= check(verifyAndParseManifest(tampered, signature, trust).status.error ==
      Error::signatureInvalid, "tampered manifest should fail signature first");
  auto corruptSignature = signature;
  corruptSignature[0] = corruptSignature[0] == 'A' ? 'B' : 'A';
  ok &= check(verifyAndParseManifest(bytes, corruptSignature, trust).status.error ==
      Error::signatureInvalid, "tampered signature should fail");
  auto missingNewline = signature;
  missingNewline.pop_back();
  ok &= check(verifyAndParseManifest(bytes, missingNewline, trust).status.error ==
      Error::signatureInvalid, "noncanonical signature file should fail");
  auto noncanonical = bytes;
  noncanonical.insert(noncanonical.begin(), ' ');
  ok &= check(verifyAndParseManifest(noncanonical, sign(noncanonical, key), trust)
      .status.error == Error::invalidManifest,
    "signed noncanonical JSON should fail schema admission");
  const std::vector<std::uint8_t> validButWrongSchema{'{','}', '\n'};
  ok &= check(verifyAndParseManifest(validButWrongSchema,
      sign(validButWrongSchema, key), trust).status.error == Error::invalidManifest,
    "signed malformed schema should fail after signature verification");
  auto oversized = std::vector<std::uint8_t>(maximumManifestBytes + 1U, 'x');
  ok &= check(verifyAndParseManifest(oversized, signature, trust).status.error ==
      Error::responseTooLarge, "oversized manifest should fail before parsing");
  ok &= check(validateTrustedUrl("http://updates.example.test/file", trust).error ==
      Error::invalidManifest, "plaintext update URL should fail");
  ok &= check(validateTrustedUrl("https://evil.example/file", trust).error ==
      Error::invalidManifest, "untrusted update host should fail");
  ok &= check(validateTrustedUrl("https://updates.example.test:8443/file", trust)
      .error == Error::invalidManifest,
    "a nondefault HTTPS port should fail unless the trust policy opts in");
  ok &= check(parseSemanticVersion("1.2.3").has_value(), "valid semver rejected");
  ok &= check(!parseSemanticVersion("01.2.3"), "noncanonical semver accepted");
  ok &= check(!parseSemanticVersion("1.2.3-rc1"), "prerelease semver accepted");
  return ok;
}

bool settingsTests(const std::filesystem::path& root)
{
  SettingsStore store{root / "update-settings.json"};
  bool ok = true;
  auto loaded = store.load();
  ok &= check(loaded.status.ok() && !loaded.settings.automaticChecks,
    "missing update settings should load secure defaults");
  Settings settings{.automaticChecks = true,
    .lastCheckUtc = "2026-09-02T18:00:00.000Z",
    .highestSeenVersion = "9.8.7"};
  ok &= check(store.save(settings).ok(), "update settings save failed");
  loaded = store.load();
  ok &= check(loaded.status.ok() && loaded.settings == settings,
    "update settings round trip failed");
  std::ofstream corrupt{store.path(), std::ios::binary | std::ios::trunc};
  corrupt << "{not-json";
  corrupt.close();
  ok &= check(!store.load().status.ok(), "corrupt update settings were accepted");
  ok &= check(!validateSettings({.automaticChecks = false,
      .lastCheckUtc = "yesterday", .highestSeenVersion = {}}),
    "invalid update timestamp accepted");
  const std::string now{"2026-09-03T18:00:00.000Z"};
  ok &= check(!automaticCheckDue(defaultSettings(), now),
    "secure defaults unexpectedly scheduled an automatic update check");
  ok &= check(automaticCheckDue({.automaticChecks = true,
      .lastCheckUtc = "2026-09-02T18:00:00.000Z", .highestSeenVersion = {}}, now),
    "a daily automatic update check was not scheduled");
  ok &= check(!automaticCheckDue({.automaticChecks = true,
      .lastCheckUtc = "2026-09-03T17:59:59.999Z", .highestSeenVersion = {}}, now),
    "automatic update checks were allowed more than once daily");
  ok &= check(!automaticCheckDue({.automaticChecks = true,
      .lastCheckUtc = "2026-09-04T18:00:00.000Z", .highestSeenVersion = {}}, now),
    "a future last-attempt timestamp caused an immediate retry");
  return ok;
}

bool clientTests()
{
  const auto key = testKey();
  const auto trust = testTrust(key);
  const auto bytes = manifest();
  FakeTransport transport;
  transport.responses.emplace(trust.manifestUrl,
    HttpResult{.status = {}, .statusCode = 200, .data = bytes});
  transport.responses.emplace(trust.signatureUrl,
    HttpResult{.status = {}, .statusCode = 200, .data = sign(bytes, key)});
  bool ok = true;
  auto result = checkForUpdate({}, "1.2.3", trust, transport);
  ok &= check(result.status.ok() && result.updateAvailable,
    "new signed update should be available");
  if (currentPlatform() == "linux" && currentArchitecture() == "x86_64") {
    ok &= check(result.asset.has_value(), "current platform asset not selected");
  }
  result = checkForUpdate({.automaticChecks = false, .lastCheckUtc = {},
      .highestSeenVersion = "10.0.0"}, "1.2.3", trust, transport);
  ok &= check(result.status.error == Error::rollbackDetected,
    "rollback below highest signed version was accepted");
  return ok;
}

bool serviceTests()
{
  const auto key = testKey();
  const auto trust = testTrust(key);
  const auto bytes = manifest();
  auto factory = [trust, bytes, key](Cancellation) {
    auto transport = std::make_unique<FakeTransport>();
    transport->responses.emplace(trust.manifestUrl,
      HttpResult{.status = {}, .statusCode = 200, .data = bytes});
    transport->responses.emplace(trust.signatureUrl,
      HttpResult{.status = {}, .statusCode = 200, .data = sign(bytes, key)});
    return transport;
  };
  Service service{2U, 8U, factory};
  bool ok = true;
  ok &= check(service.requestCheck(1U, {}, "1.0.0", trust).error ==
      Error::notRunning, "stopped update service accepted a request");
  ok &= check(service.start().ok(), "update service failed to start");
  ok &= check(service.start().error == Error::threadFailure,
    "running update service started twice");
  ok &= check(service.requestCheck(0U, {}, "1.0.0", trust).error ==
      Error::invalidRequest, "update service accepted a zero operation identity");
  ok &= check(service.requestCheck(42U, {}, "1.0.0", trust).ok(),
    "update service rejected valid check");
  bool completed = false;
  for (int attempt = 0; attempt < 4 && !completed; ++attempt) {
    const auto event = service.waitForEvent(std::chrono::seconds{1});
    if (event && event->type == EventType::checkCompleted) {
      completed = event->operationId == 42U && event->check.status.ok();
    }
  }
  ok &= check(completed, "update service did not complete signed check");
  ok &= check(service.stop().ok(), "update service failed to stop");
  ok &= check(service.stop().ok(), "repeated update service stop failed");
  ok &= check(service.start().ok(), "update service could not restart cleanly");
  ok &= check(service.stop().ok(), "restarted update service failed to stop");

  std::atomic_bool entered{false};
  auto blockingFactory = [&entered](Cancellation cancellation) {
    return std::make_unique<BlockingTransport>(std::move(cancellation), entered);
  };
  Service bounded{1U, 2U, blockingFactory};
  ok &= check(bounded.start().ok(), "bounded update service failed to start");
  ok &= check(bounded.requestCheck(10U, {}, "1.0.0", trust).ok(),
    "bounded update service rejected its active request");
  for (int attempt = 0; attempt < 1'000 &&
       !entered.load(std::memory_order_acquire); ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  ok &= check(entered.load(std::memory_order_acquire),
    "bounded update transport did not enter its request");
  ok &= check(bounded.requestCheck(11U, {}, "1.0.0", trust).ok(),
    "bounded update service rejected its one queued request");
  ok &= check(bounded.requestCheck(12U, {}, "1.0.0", trust).error ==
      Error::queueFull, "bounded update service exceeded its command capacity");
  ok &= check(bounded.stop().ok(),
    "bounded update service did not cancel and join cleanly");

  Service nullTransport{2U, 4U,
    [](Cancellation) -> std::unique_ptr<HttpTransport> { return {}; }};
  ok &= check(nullTransport.start().ok(), "null-transport service failed to start");
  ok &= check(nullTransport.requestCheck(20U, {}, "1.0.0", trust).ok(),
    "null-transport request was not queued");
  bool transportFailure = false;
  for (int attempt = 0; attempt < 4 && !transportFailure; ++attempt) {
    const auto event = nullTransport.waitForEvent(std::chrono::seconds{1});
    transportFailure = event && event->operationId == 20U &&
      event->type == EventType::checkFailed &&
      event->check.status.error == Error::threadFailure;
  }
  ok &= check(transportFailure,
    "null update transport did not produce a bounded failure event");
  ok &= check(nullTransport.stop().ok(), "null-transport service failed to stop");
  return ok;
}

} // namespace

int main(int argc, char** argv)
{
  QCoreApplication application{argc, argv};
  if (argc != 2) {
    std::cerr << "Expected the production update public-key path\n";
    return 2;
  }
  QTemporaryDir temporary;
  bool ok = check(temporary.isValid(), "temporary directory creation failed");
  ok &= productionKeyTests(QString::fromLocal8Bit(argv[1]));
  ok &= manifestTests();
  ok &= settingsTests(temporary.path().toStdString());
  ok &= clientTests();
  ok &= serviceTests();
  return ok ? 0 : 1;
}
