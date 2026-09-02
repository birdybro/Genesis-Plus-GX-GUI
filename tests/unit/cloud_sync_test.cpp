#include "genplusgx/cloud/cloud_credentials.h"
#include "genplusgx/cloud/cloud_manifest.h"
#include "genplusgx/cloud/cloud_settings.h"
#include "genplusgx/cloud/cloud_sync.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace genplusgx;
using namespace genplusgx::cloud;

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::filesystem::path fsPath(const QString& value)
{
#if defined(Q_OS_WIN)
  return std::filesystem::path{value.toStdWString()};
#else
  return std::filesystem::path{value.toStdString()};
#endif
}

std::vector<std::uint8_t> bytes(std::string value)
{
  return {value.begin(), value.end()};
}

std::string hash(std::span<const std::uint8_t> data)
{
  return QCryptographicHash::hash(QByteArrayView{
    reinterpret_cast<const char*>(data.data()), static_cast<qsizetype>(data.size())},
    QCryptographicHash::Sha256).toHex().toStdString();
}

bool write(const std::filesystem::path& path, const std::vector<std::uint8_t>& data)
{
  return writeFileAtomically(path, data, maximumTransferBytes).ok();
}

std::vector<std::uint8_t> read(const std::filesystem::path& path)
{
  auto loaded = readFileBounded(path, maximumTransferBytes);
  return loaded.status && loaded.exists ? std::move(loaded.data)
                                        : std::vector<std::uint8_t>{};
}

struct RemoteState final {
  std::map<std::string, std::vector<std::uint8_t>> files;
  std::map<std::string, std::string> etags;
  std::uint64_t revision{0U};
  bool failNextManifestPrecondition{false};
};

class FakeRemote final : public RemoteStore {
public:
  explicit FakeRemote(std::shared_ptr<RemoteState> state)
      : state_(std::move(state))
  {
  }

  Status ensureCollection(const std::string& relativePath) override
  {
    return relativePath.empty()
      ? Status{Error::invalidPath, "empty collection"} : Status{};
  }

  RemoteReadResult read(
    const std::string& relativePath, std::size_t maximumBytes) override
  {
    const auto found = state_->files.find(relativePath);
    if (found == state_->files.end()) {
      return {.status = {}, .exists = false, .data = {}, .etag = {}};
    }
    if (found->second.size() > maximumBytes) {
      return {.status = {Error::dataTooLarge, "fake bound"}, .exists = true,
        .data = {}, .etag = {}};
    }
    return {.status = {}, .exists = true, .data = found->second,
      .etag = state_->etags[relativePath]};
  }

  RemoteWriteResult write(const std::string& relativePath,
    std::span<const std::uint8_t> data, WriteCondition condition,
    const std::string& etag) override
  {
    const bool exists = state_->files.contains(relativePath);
    if ((condition == WriteCondition::createOnly && exists) ||
        (condition == WriteCondition::match &&
          (!exists || state_->etags[relativePath] != etag)) ||
        (relativePath.ends_with("manifest.json") &&
          std::exchange(state_->failNextManifestPrecondition, false))) {
      return {.status = {}, .preconditionFailed = true, .etag = {}};
    }
    state_->files[relativePath] = {data.begin(), data.end()};
    state_->etags[relativePath] = '"' + std::to_string(++state_->revision) + '"';
    return {.status = {}, .preconditionFailed = false,
      .etag = state_->etags[relativePath]};
  }

private:
  std::shared_ptr<RemoteState> state_;
};

Settings enabledSettings()
{
  Settings settings;
  settings.enabled = true;
  settings.endpoint = "https://cloud.example.test/webdav";
  settings.username = "tester";
  return settings;
}

std::filesystem::path savePath(const ApplicationPaths& paths)
{
  return paths.savesDirectory() /
    "synthetic-0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" /
    "cartridge.srm";
}

bool settingsAndManifestValidation()
{
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "Cloud settings temporary directory failed")) {
    return false;
  }
  auto settings = enabledSettings();
  if (!check(validateSettings(settings).ok(), "Valid HTTPS settings rejected") ||
      !check(!validateSettings(Settings{.enabled = true, .endpoint =
        "http://cloud.example.test", .username = "tester"}),
        "Plain HTTP settings accepted") ||
      !check(!validateSettings(Settings{.enabled = true, .endpoint =
        "https://user:secret@cloud.example.test", .username = "tester"}),
        "Credentials embedded in URL accepted") ||
      !check(!validateSettings(Settings{.enabled = true, .endpoint =
        "https://cloud.example.test", .username = "tester:secret"}),
        "A Basic-auth username containing ':' was accepted") ||
      !check(!validateSettings(Settings{.enabled = true, .syncSaves = false,
        .syncStates = false, .endpoint = "https://cloud.example.test",
        .username = "tester"}), "Empty selection accepted")) {
    return false;
  }
  SettingsStore store{fsPath(temporary.path()) / "cloud.json"};
  if (!check(store.save(settings).ok(), "Cloud settings save failed")) {
    return false;
  }
  const auto loaded = store.load();
  const auto raw = read(store.path());
  const std::string text{raw.begin(), raw.end()};
  if (!check(loaded.status.ok() && loaded.settings == settings,
        "Cloud settings round trip failed") ||
      !check(text.find("password") == std::string::npos &&
        text.find("secret") == std::string::npos,
        "Cloud settings persisted a secret field")) {
    return false;
  }

  const std::string key =
    "saves/synthetic-0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef/cartridge.srm";
  const auto content = bytes("save-data");
  RemoteManifest manifest{{{key, {key, hash(content), content.size()}}}};
  const auto encoded = encodeRemoteManifest(manifest);
  const auto decoded = decodeRemoteManifest(encoded);
  if (!check(decoded.status.ok() && decoded.manifest == manifest,
        "Remote manifest round trip failed") ||
      !check(!decodeRemoteManifest(bytes("not-json")).status,
        "Malformed remote manifest accepted") ||
      !check(decodeRemoteManifest(std::vector<std::uint8_t>(
        maximumManifestBytes + 1U, static_cast<std::uint8_t>(' '))).status.error ==
          Error::dataTooLarge,
        "Oversized remote manifest accepted") ||
      !check(!validFileKey("states/../../bios.bin"),
        "Traversal cloud key accepted") ||
      !check(!validFileKey("saves/game/firmware.bin"),
        "Unselected file type accepted")) {
    return false;
  }
  for (std::uint32_t seed = 0U; seed < 256U; ++seed) {
    auto fuzz = encoded;
    if (!fuzz.empty()) {
      fuzz[(seed * 97U) % fuzz.size()] ^= static_cast<std::uint8_t>(seed + 1U);
    }
    const auto parsed = decodeRemoteManifest(fuzz);
    if (parsed.status && encodeRemoteManifest(parsed.manifest).empty()) {
      return check(false, "Accepted fuzzed manifest could not be re-encoded");
    }
  }
  return check(CredentialStore::keyForAccount(settings.endpoint, settings.username)
      != CredentialStore::keyForAccount(settings.endpoint, "other"),
    "Credential account keys collide");
}

bool plannerMatrix()
{
  const FileRecord local{"saves/game/cartridge.srm", std::string(64U, 'a'), 1U};
  const FileRecord remote{"saves/game/cartridge.srm", std::string(64U, 'b'), 1U};
  const Baseline baseline{local.key, local.sha256, remote.sha256};
  auto changedLocal = local;
  changedLocal.sha256 = std::string(64U, 'c');
  auto changedRemote = remote;
  changedRemote.sha256 = std::string(64U, 'd');
  return check(chooseAction(&local, nullptr, nullptr) == Action::upload,
           "New local file was not uploaded") &&
    check(chooseAction(nullptr, &remote, nullptr) == Action::download,
      "New remote file was not downloaded") &&
    check(chooseAction(&local, &remote, nullptr) == Action::conflict,
      "First-sync difference was not a conflict") &&
    check(chooseAction(&changedLocal, &remote, &baseline) == Action::upload,
      "Local-only edit was not uploaded") &&
    check(chooseAction(&local, &changedRemote, &baseline) == Action::download,
      "Remote-only edit was not downloaded") &&
    check(chooseAction(&changedLocal, &changedRemote, &baseline) == Action::conflict,
      "Two-sided edit was not a conflict") &&
    check(chooseAction(nullptr, &remote, &baseline) == Action::download,
      "Local deletion was destructively propagated") &&
    check(chooseAction(&local, nullptr, &baseline) == Action::upload,
      "Remote deletion was destructively propagated");
}

bool synchronizationWorkflow()
{
  QTemporaryDir firstTemporary;
  QTemporaryDir secondTemporary;
  if (!firstTemporary.isValid() || !secondTemporary.isValid()) {
    return check(false, "Cloud workflow temporary directories failed");
  }
  ApplicationPaths first{fsPath(firstTemporary.path())};
  ApplicationPaths second{fsPath(secondTemporary.path())};
  if (!first.initialize() || !second.initialize()) {
    return check(false, "Cloud workflow application paths failed");
  }
  const auto settings = enabledSettings();
  const auto original = bytes("original-save");
  if (!write(savePath(first), original)) {
    return check(false, "Cloud workflow local save write failed");
  }
  auto state = std::make_shared<RemoteState>();
  FakeRemote firstRemote{state};
  auto result = synchronize(first, settings, firstRemote);
  if (!check(result.status.ok() && result.summary.uploaded == 1U,
        "Initial upload failed")) {
    return false;
  }
  FakeRemote secondRemote{state};
  result = synchronize(second, settings, secondRemote);
  if (!check(result.status.ok() && result.summary.downloaded == 1U &&
        read(savePath(second)) == original, "Initial download failed")) {
    return false;
  }

  const auto localEdit = bytes("first-client-edit");
  if (!write(savePath(first), localEdit)) {
    return false;
  }
  result = synchronize(first, settings, firstRemote);
  if (!check(result.status.ok() && result.summary.uploaded == 1U,
        "Changed save upload failed")) {
    return false;
  }
  result = synchronize(second, settings, secondRemote);
  if (!check(result.status.ok() && result.summary.downloaded == 1U &&
        read(savePath(second)) == localEdit, "Remote edit download failed")) {
    return false;
  }

  const auto secondEdit = bytes("second-client-edit");
  const auto firstEdit = bytes("new-first-client-edit");
  if (!write(savePath(second), secondEdit) || !write(savePath(first), firstEdit)) {
    return false;
  }
  result = synchronize(first, settings, firstRemote);
  if (!result.status || result.summary.uploaded != 1U) {
    return check(false, "Conflict setup upload failed");
  }
  const auto conflictTime = std::chrono::system_clock::time_point{
    std::chrono::milliseconds{1'700'000'000'000LL}};
  result = synchronize(second, settings, secondRemote, {}, conflictTime);
  if (!check(result.status.ok() && result.summary.conflicts == 1U &&
        read(savePath(second)) == secondEdit && result.summary.items.size() == 1U &&
        !result.summary.items[0].conflictPath.empty() &&
        read(result.summary.items[0].conflictPath) == firstEdit,
        "Two-sided edit did not preserve both versions")) {
    return false;
  }

  std::filesystem::remove(savePath(second));
  result = synchronize(second, settings, secondRemote);
  if (!check(result.status.ok() && result.summary.downloaded == 1U &&
        read(savePath(second)) == firstEdit,
        "Local deletion was not healed from remote")) {
    return false;
  }

  const auto manifestKey = settings.remoteDirectory + "/manifest.json";
  const auto remoteManifest = decodeRemoteManifest(state->files[manifestKey]);
  if (!remoteManifest.status || remoteManifest.manifest.files.empty()) {
    return check(false, "Remote manifest disappeared");
  }
  state->files[manifestKey] = encodeRemoteManifest(RemoteManifest{});
  state->etags[manifestKey] = '"' + std::to_string(++state->revision) + '"';
  result = synchronize(first, settings, firstRemote);
  if (!check(result.status.ok() && result.summary.uploaded == 1U,
        "Remote deletion was not healed from local")) {
    return false;
  }

  if (!write(savePath(first), bytes("conditional-edit"))) {
    return false;
  }
  state->failNextManifestPrecondition = true;
  result = synchronize(first, settings, firstRemote);
  return check(!result.status && result.status.error == Error::remoteChanged &&
      read(savePath(first)) == bytes("conditional-edit"),
    "ETag conflict did not fail safely");
}

bool scannerAndServiceBounds()
{
  QTemporaryDir temporary;
  if (!temporary.isValid()) {
    return false;
  }
  ApplicationPaths paths{fsPath(temporary.path())};
  if (!paths.initialize()) {
    return false;
  }
  if (!write(savePath(paths), bytes("selected")) ||
      !write(paths.savesDirectory() / "game" / "notes.txt", bytes("ignored")) ||
      !write(paths.root() / "bios.bin", bytes("never-uploaded"))) {
    return false;
  }
  const auto scan = scanLocalFiles(paths, enabledSettings());
  if (!check(scan.status.ok() && scan.files.size() == 1U,
        "Scanner selected a non-save/state file")) {
    return false;
  }

  auto remoteState = std::make_shared<RemoteState>();
  SyncService service{
    [remoteState](const Settings&, std::string password, Cancellation) {
      std::fill(password.begin(), password.end(), '\0');
      return std::make_unique<FakeRemote>(remoteState);
    }};
  if (!check(service.start().ok(), "Cloud service failed to start")) {
    return false;
  }
  auto event = service.waitForEvent(std::chrono::seconds{2});
  if (!check(event && event->type == EventType::serviceStarted,
        "Cloud service did not announce startup")) {
    return false;
  }
  if (!check(service.request(1U, paths, enabledSettings(), "temporary-secret").ok(),
        "Cloud service rejected valid request")) {
    return false;
  }
  event = service.waitForEvent(std::chrono::seconds{5});
  const bool completed = event && event->type == EventType::syncCompleted &&
    event->result.status.ok();
  const auto stopped = service.stop();
  return check(completed, "Cloud service did not complete its request") &&
    check(stopped.ok(), "Cloud service failed to stop cleanly");
}

class BlockingRemote final : public RemoteStore {
public:
  BlockingRemote(Cancellation cancellation, std::atomic_bool& entered)
      : cancellation_(std::move(cancellation)), entered_(entered)
  {
  }

  Status ensureCollection(const std::string&) override
  {
    entered_.store(true);
    while (!cancellation_()) {
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return {Error::cancelled, "cancelled test transfer"};
  }

  RemoteReadResult read(const std::string&, std::size_t) override { return {}; }
  RemoteWriteResult write(const std::string&, std::span<const std::uint8_t>,
    WriteCondition, const std::string&) override { return {}; }

private:
  Cancellation cancellation_;
  std::atomic_bool& entered_;
};

bool activeShutdownAndQueueBounds()
{
  QTemporaryDir temporary;
  if (!temporary.isValid()) {
    return false;
  }
  ApplicationPaths paths{fsPath(temporary.path())};
  if (!paths.initialize()) {
    return false;
  }
  std::atomic_bool entered{false};
  SyncService service{
    [&entered](const Settings&, std::string password, Cancellation cancellation) {
      std::fill(password.begin(), password.end(), '\0');
      return std::make_unique<BlockingRemote>(std::move(cancellation), entered);
    }, 1U, 8U};
  if (!service.start()) {
    return false;
  }
  static_cast<void>(service.waitForEvent(std::chrono::seconds{2}));
  if (!service.request(1U, paths, enabledSettings(), "active-secret")) {
    return false;
  }
  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::seconds{2};
  while (!entered.load() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  if (!check(entered.load(), "Blocking cloud transfer did not start") ||
      !check(service.request(2U, paths, enabledSettings(), "queued-secret").ok(),
        "The bounded cloud queue did not accept its single waiting request")) {
    return false;
  }
  const auto overflow = service.request(
    3U, paths, enabledSettings(), "rejected-secret");
  const auto started = std::chrono::steady_clock::now();
  const auto stopped = service.stop();
  return check(!overflow && overflow.error == Error::queueFull,
      "The bounded cloud queue accepted an overflow request") &&
    check(stopped.ok(), "Active cloud shutdown failed") &&
    check(std::chrono::steady_clock::now() - started < std::chrono::seconds{2},
      "Active cloud shutdown did not cancel promptly");
}

} // namespace

int main(int argc, char** argv)
{
  QCoreApplication application{argc, argv};
  return settingsAndManifestValidation() && plannerMatrix() &&
      synchronizationWorkflow() && scannerAndServiceBounds() &&
      activeShutdownAndQueueBounds()
    ? 0 : 1;
}
