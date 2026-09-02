#include "genplusgx/cloud/cloud_sync.h"

#include "genplusgx/bounded_queue.h"

#include <QCryptographicHash>
#include <QDateTime>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

namespace genplusgx::cloud {
namespace {

Status failure(Error error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

bool selected(const std::string& key, const Settings& settings)
{
  return (settings.syncSaves && key.starts_with("saves/")) ||
    (settings.syncStates && key.starts_with("states/"));
}

std::string objectPath(const Settings& settings, const std::string& sha256)
{
  return settings.remoteDirectory + "/objects/" + sha256;
}

std::string manifestPath(const Settings& settings)
{
  return settings.remoteDirectory + "/manifest.json";
}

std::string dataSha256(std::span<const std::uint8_t> data)
{
  QCryptographicHash hash{QCryptographicHash::Sha256};
  hash.addData(QByteArrayView{reinterpret_cast<const char*>(data.data()),
    static_cast<qsizetype>(data.size())});
  return hash.result().toHex().toStdString();
}

struct LoadedObject final {
  Status status;
  std::vector<std::uint8_t> data;
};

LoadedObject loadRemoteObject(RemoteStore& remote, const Settings& settings,
  const FileRecord& record)
{
  auto loaded = remote.read(objectPath(settings, record.sha256),
    maximumBytesForKey(record.key));
  if (!loaded.status) {
    return {.status = std::move(loaded.status), .data = {}};
  }
  if (!loaded.exists) {
    return {.status = failure(Error::invalidData,
      "The remote manifest references a missing content object."), .data = {}};
  }
  if (loaded.data.size() != record.size ||
      dataSha256(loaded.data) != record.sha256) {
    return {.status = failure(Error::invalidData,
      "A remote content object failed its size or SHA-256 check."), .data = {}};
  }
  return {.status = {}, .data = std::move(loaded.data)};
}

Status ensureRemoteObject(RemoteStore& remote, const Settings& settings,
  const FileRecord& record, std::span<const std::uint8_t> data)
{
  auto written = remote.write(objectPath(settings, record.sha256), data,
    WriteCondition::createOnly);
  if (!written.status) {
    return written.status;
  }
  if (!written.preconditionFailed) {
    return {};
  }
  const auto existing = loadRemoteObject(remote, settings, record);
  return existing.status;
}

std::filesystem::path conflictPath(const ApplicationPaths& paths,
  const std::string& key, const std::string& hash,
  std::chrono::system_clock::time_point timestamp)
{
  const auto local = localPathForKey(paths, key);
  if (!local) {
    return {};
  }
  const auto relative = local->lexically_relative(
    key.starts_with("saves/") ? paths.savesDirectory()
                              : paths.statesDirectory());
  if (relative.empty() || relative.is_absolute() ||
      relative.native().starts_with(std::filesystem::path{".."}.native())) {
    return {};
  }
  auto directory = paths.cloudDirectory() / "conflicts" /
    (key.starts_with("saves/") ? "saves" : "states") /
    relative.parent_path();
  const auto time = std::chrono::duration_cast<std::chrono::milliseconds>(
    timestamp.time_since_epoch()).count();
  const auto stamp = QDateTime::fromMSecsSinceEpoch(time).toUTC()
    .toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")).toStdString();
  const auto stem = relative.stem().string();
  const auto extension = relative.extension().string();
  for (std::size_t attempt = 0U; attempt < 10'000U; ++attempt) {
    const auto suffix = attempt == 0U ? std::string{}
                                      : '-' + std::to_string(attempt);
    const auto candidate = directory /
      (stem + ".remote-" + stamp + '-' + hash.substr(0U, 8U) + suffix + extension);
    std::error_code error;
    if (!std::filesystem::exists(candidate, error) && !error) {
      return candidate;
    }
  }
  return {};
}

void countItem(Summary& summary, ItemResult item, std::uint64_t bytes)
{
  switch (item.action) {
    case Action::unchanged:
      ++summary.unchanged;
      break;
    case Action::upload:
      ++summary.uploaded;
      summary.uploadedBytes += bytes;
      break;
    case Action::download:
      ++summary.downloaded;
      summary.downloadedBytes += bytes;
      break;
    case Action::conflict:
      ++summary.conflicts;
      summary.downloadedBytes += bytes;
      break;
  }
  summary.items.push_back(std::move(item));
}

struct Command final {
  std::uint64_t operationId{0U};
  ApplicationPaths paths;
  Settings settings;
  std::string password;
};

} // namespace

SyncResult synchronize(const ApplicationPaths& paths, const Settings& settings,
  RemoteStore& remote, const Cancellation& cancellation,
  std::chrono::system_clock::time_point timestamp)
{
  const auto validation = validateSettings(settings);
  if (!validation || !settings.enabled) {
    return {.status = validation ? failure(Error::invalidSettings,
      "Cloud synchronization is disabled.") : validation, .summary = {}};
  }
  const auto cancelled = [&cancellation] {
    return cancellation && cancellation();
  };
  if (cancelled()) {
    return {.status = failure(Error::cancelled,
      "Cloud synchronization was cancelled."), .summary = {}};
  }
  auto local = scanLocalFiles(paths, settings);
  if (!local.status) {
    return {.status = std::move(local.status), .summary = {}};
  }
  LocalManifestStore baselineStore{
    paths.configDirectory() /
      ("cloud-sync-baseline-" + baselineId(settings) + ".json")};
  auto baseline = baselineStore.load();
  if (!baseline.status) {
    return {.status = std::move(baseline.status), .summary = {}};
  }
  auto status = remote.ensureCollection(settings.remoteDirectory);
  if (!status) {
    return {.status = std::move(status), .summary = {}};
  }
  status = remote.ensureCollection(settings.remoteDirectory + "/objects");
  if (!status) {
    return {.status = std::move(status), .summary = {}};
  }
  auto remoteManifestRead = remote.read(manifestPath(settings), maximumManifestBytes);
  if (!remoteManifestRead.status) {
    return {.status = std::move(remoteManifestRead.status), .summary = {}};
  }
  RemoteManifest remoteManifest;
  if (remoteManifestRead.exists) {
    auto decoded = decodeRemoteManifest(remoteManifestRead.data);
    if (!decoded.status) {
      return {.status = std::move(decoded.status), .summary = {}};
    }
    remoteManifest = std::move(decoded.manifest);
  }

  std::set<std::string> keys;
  for (const auto& [key, record] : local.files) {
    static_cast<void>(record);
    keys.insert(key);
  }
  for (const auto& [key, record] : remoteManifest.files) {
    static_cast<void>(record);
    if (selected(key, settings)) {
      keys.insert(key);
    }
  }

  struct Planned final {
    std::string key;
    Action action{Action::unchanged};
  };
  std::vector<Planned> plan;
  plan.reserve(keys.size());
  for (const auto& key : keys) {
    const auto localIt = local.files.find(key);
    const auto remoteIt = remoteManifest.files.find(key);
    const auto baselineIt = baseline.manifest.files.find(key);
    plan.push_back({key, chooseAction(
      localIt == local.files.end() ? nullptr : &localIt->second,
      remoteIt == remoteManifest.files.end() ? nullptr : &remoteIt->second,
      baselineIt == baseline.manifest.files.end() ? nullptr : &baselineIt->second)});
  }

  auto updatedRemote = remoteManifest;
  Summary summary;
  summary.items.reserve(plan.size());
  for (const auto& item : plan) {
    if (cancelled()) {
      return {.status = failure(Error::cancelled,
        "Cloud synchronization was cancelled."), .summary = std::move(summary)};
    }
    if (item.action != Action::upload) {
      continue;
    }
    const auto localIt = local.files.find(item.key);
    if (localIt == local.files.end()) {
      return {.status = failure(Error::invalidData,
        "The upload plan lost its local source."), .summary = std::move(summary)};
    }
    const auto path = localPathForKey(paths, item.key);
    if (!path) {
      return {.status = failure(Error::invalidPath,
        "The upload plan contains an invalid local path."),
        .summary = std::move(summary)};
    }
    auto loaded = readFileBounded(*path, maximumBytesForKey(item.key));
    if (!loaded.status || !loaded.exists || loaded.data.size() != localIt->second.size ||
        dataSha256(loaded.data) != localIt->second.sha256) {
      return {.status = failure(Error::ioError,
        "A local file changed while cloud synchronization was reading it."),
        .summary = std::move(summary)};
    }
    status = ensureRemoteObject(remote, settings, localIt->second, loaded.data);
    if (!status) {
      return {.status = std::move(status), .summary = std::move(summary)};
    }
    updatedRemote.files[item.key] = localIt->second;
  }

  if (!remoteManifestRead.exists || !(updatedRemote == remoteManifest)) {
    const auto encoded = encodeRemoteManifest(updatedRemote);
    if (encoded.empty()) {
      return {.status = failure(Error::invalidManifest,
        "The updated remote manifest could not be encoded."),
        .summary = std::move(summary)};
    }
    if (remoteManifestRead.exists && remoteManifestRead.etag.empty()) {
      return {.status = failure(Error::remoteChanged,
        "The WebDAV server did not provide an ETag required for safe updates."),
        .summary = std::move(summary)};
    }
    auto written = remote.write(manifestPath(settings), encoded,
      remoteManifestRead.exists ? WriteCondition::match : WriteCondition::createOnly,
      remoteManifestRead.etag);
    if (!written.status) {
      return {.status = std::move(written.status), .summary = std::move(summary)};
    }
    if (written.preconditionFailed) {
      return {.status = failure(Error::remoteChanged,
        "Another client changed the remote manifest; synchronize again."),
        .summary = std::move(summary)};
    }
    remoteManifest = updatedRemote;
  }

  for (const auto& item : plan) {
    if (cancelled()) {
      return {.status = failure(Error::cancelled,
        "Cloud synchronization was cancelled."), .summary = std::move(summary)};
    }
    if (item.action == Action::upload) {
      countItem(summary, {item.key, item.action, {}},
        local.files.at(item.key).size);
      continue;
    }
    if (item.action == Action::unchanged) {
      countItem(summary, {item.key, item.action, {}}, 0U);
      continue;
    }
    const auto remoteIt = remoteManifest.files.find(item.key);
    if (remoteIt == remoteManifest.files.end()) {
      return {.status = failure(Error::invalidData,
        "The download plan lost its remote source."), .summary = std::move(summary)};
    }
    auto object = loadRemoteObject(remote, settings, remoteIt->second);
    if (!object.status) {
      return {.status = std::move(object.status), .summary = std::move(summary)};
    }
    auto destination = localPathForKey(paths, item.key);
    if (!destination) {
      return {.status = failure(Error::invalidPath,
        "The download plan contains an invalid local path."),
        .summary = std::move(summary)};
    }
    std::filesystem::path conflict;
    if (item.action == Action::conflict) {
      conflict = conflictPath(paths, item.key, remoteIt->second.sha256, timestamp);
      destination = conflict.empty() ? std::nullopt
                                     : std::optional<std::filesystem::path>{conflict};
    }
    if (!destination) {
      return {.status = failure(Error::ioError,
        "A collision-safe cloud conflict path was unavailable."),
        .summary = std::move(summary)};
    }
    const auto written = writeFileAtomically(*destination, object.data,
      maximumBytesForKey(item.key));
    if (!written) {
      return {.status = failure(Error::ioError, written.message),
        .summary = std::move(summary)};
    }
    if (item.action == Action::download) {
      local.files[item.key] = remoteIt->second;
    }
    countItem(summary, {item.key, item.action, std::move(conflict)},
      remoteIt->second.size);
  }

  auto nextBaseline = baseline.manifest;
  for (auto it = nextBaseline.files.begin(); it != nextBaseline.files.end();) {
    if (selected(it->first, settings) && !keys.contains(it->first)) {
      it = nextBaseline.files.erase(it);
    } else {
      ++it;
    }
  }
  for (const auto& key : keys) {
    const auto localIt = local.files.find(key);
    const auto remoteIt = remoteManifest.files.find(key);
    nextBaseline.files[key] = Baseline{
      .key = key,
      .localSha256 = localIt == local.files.end() ? std::string{}
                                                  : localIt->second.sha256,
      .remoteSha256 = remoteIt == remoteManifest.files.end() ? std::string{}
                                                              : remoteIt->second.sha256,
    };
  }
  status = baselineStore.save(nextBaseline);
  if (!status) {
    return {.status = std::move(status), .summary = std::move(summary)};
  }
  return {.status = {}, .summary = std::move(summary)};
}

class SyncService::Private final {
public:
  Private(RemoteFactory factory, std::size_t commandCapacity,
    std::size_t eventCapacity)
      : factory_(std::move(factory)), commands_(commandCapacity),
        events_(eventCapacity)
  {
    if (!factory_) {
      factory_ = [](const Settings& settings, std::string password,
                   Cancellation cancellation) {
        return std::make_unique<WebDavRemoteStore>(settings,
          std::move(password), std::move(cancellation));
      };
    }
  }

  Status start()
  {
    std::scoped_lock lock{mutex_};
    if (thread_.joinable()) {
      return failure(Error::busy, "The cloud synchronization service is running.");
    }
    commands_.clear();
    events_.clear();
    accepting_ = true;
    stopRequested_.store(false);
    shutdownStatus_ = {};
    try {
      thread_ = std::thread{&Private::threadMain, this};
    } catch (const std::system_error& error) {
      accepting_ = false;
      return failure(Error::threadFailure,
        "The cloud worker could not start: " + std::string{error.what()});
    }
    return {};
  }

  Status request(std::uint64_t operationId, ApplicationPaths paths,
    Settings settings, std::string password)
  {
    const auto validation = validateSettings(settings);
    if (operationId == 0U || !settings.enabled || !validation || password.empty() ||
        password.size() > 1'024U || paths.root().empty() ||
        !paths.root().is_absolute()) {
      std::fill(password.begin(), password.end(), '\0');
      return failure(Error::invalidSettings,
        validation ? "The cloud synchronization request is incomplete."
                   : validation.message);
    }
    std::scoped_lock lock{mutex_};
    if (!accepting_ || stopRequested_.load()) {
      std::fill(password.begin(), password.end(), '\0');
      return failure(Error::notRunning,
        "The cloud synchronization service is not accepting requests.");
    }
    if (!commands_.tryPush({operationId, std::move(paths), std::move(settings),
          std::move(password)})) {
      std::fill(password.begin(), password.end(), '\0');
      return failure(Error::queueFull,
        "The bounded cloud synchronization queue is full.");
    }
    wake_.notify_one();
    return {};
  }

  std::optional<Event> pollEvent()
  {
    std::scoped_lock lock{mutex_};
    return events_.pop();
  }

  std::optional<Event> waitForEvent(std::chrono::milliseconds timeout)
  {
    std::unique_lock lock{mutex_};
    eventReady_.wait_for(lock, timeout, [this] { return !events_.empty(); });
    return events_.pop();
  }

  Status stop()
  {
    {
      std::scoped_lock lock{mutex_};
      if (!thread_.joinable()) {
        accepting_ = false;
        return shutdownStatus_;
      }
      accepting_ = false;
      stopRequested_.store(true);
      while (auto command = commands_.pop()) {
        std::fill(command->password.begin(), command->password.end(), '\0');
      }
      wake_.notify_all();
      eventReady_.notify_all();
    }
    thread_.join();
    std::scoped_lock lock{mutex_};
    return shutdownStatus_;
  }

private:
  void threadMain()
  {
    publish({.type = EventType::serviceStarted, .operationId = 0U,
      .result = {}});
    while (!stopRequested_.load()) {
      std::optional<Command> command;
      {
        std::unique_lock lock{mutex_};
        wake_.wait(lock, [this] {
          return stopRequested_.load() || !commands_.empty();
        });
        if (stopRequested_.load()) {
          break;
        }
        command = commands_.pop();
      }
      if (!command) {
        continue;
      }
      auto password = std::move(command->password);
      auto remote = factory_(command->settings, password,
        [this] { return stopRequested_.load(); });
      std::fill(password.begin(), password.end(), '\0');
      SyncResult result;
      if (!remote) {
        result.status = failure(Error::threadFailure,
          "The cloud remote transport could not be created.");
      } else {
        result = synchronize(command->paths, command->settings, *remote,
          [this] { return stopRequested_.load(); });
      }
      publish({
        .type = result.status ? EventType::syncCompleted : EventType::syncFailed,
        .operationId = command->operationId,
        .result = std::move(result),
      });
    }
    finish({});
  }

  void publish(Event event)
  {
    std::scoped_lock lock{mutex_};
    static_cast<void>(events_.dropOldestAndPush(std::move(event)));
    eventReady_.notify_all();
  }

  void finish(Status status)
  {
    {
      std::scoped_lock lock{mutex_};
      accepting_ = false;
      shutdownStatus_ = status;
    }
    publish({.type = EventType::serviceStopped, .operationId = 0U,
      .result = {.status = std::move(status), .summary = {}}});
  }

  RemoteFactory factory_;
  BoundedQueue<Command> commands_;
  BoundedQueue<Event> events_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable eventReady_;
  std::thread thread_;
  std::atomic_bool stopRequested_{false};
  Status shutdownStatus_;
  bool accepting_{false};
};

SyncService::SyncService(RemoteFactory factory, std::size_t commandCapacity,
  std::size_t eventCapacity)
    : private_(std::make_unique<Private>(std::move(factory), commandCapacity,
        eventCapacity))
{
}

SyncService::~SyncService() { static_cast<void>(private_->stop()); }

Status SyncService::start() { return private_->start(); }

Status SyncService::request(std::uint64_t operationId, ApplicationPaths paths,
  Settings settings, std::string password)
{
  return private_->request(operationId, std::move(paths), std::move(settings),
    std::move(password));
}

std::optional<Event> SyncService::pollEvent() { return private_->pollEvent(); }

std::optional<Event> SyncService::waitForEvent(std::chrono::milliseconds timeout)
{
  return private_->waitForEvent(timeout);
}

Status SyncService::stop() { return private_->stop(); }

} // namespace genplusgx::cloud
