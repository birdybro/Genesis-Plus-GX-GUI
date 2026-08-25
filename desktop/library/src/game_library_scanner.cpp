#include "genplusgx/library/game_library_scanner.h"

#include "genplusgx/bounded_queue.h"
#include "genplusgx/game_file.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace genplusgx::library {
namespace {

GameLibraryScannerStatus failure(
  GameLibraryScannerError error,
  std::string message)
{
  return {.error = error, .message = std::move(message)};
}

struct ScanCommand final {
  std::uint64_t operationId{0};
  std::int64_t directoryId{0};
};

std::int64_t fileTimeMilliseconds(
  const std::filesystem::directory_entry& entry)
{
  std::error_code error;
  const auto time = entry.last_write_time(error);
  return error ? 0 : std::chrono::duration_cast<std::chrono::milliseconds>(
    time.time_since_epoch()).count();
}

} // namespace

class GameLibraryScanner::Private final {
public:
  Private(
    std::filesystem::path databasePath,
    std::size_t commandCapacity,
    std::size_t eventCapacity)
    : databasePath_(std::move(databasePath)),
      commands_(commandCapacity),
      events_(eventCapacity)
  {
  }

  GameLibraryScannerStatus start()
  {
    std::scoped_lock lock{mutex_};
    if (thread_.joinable() || accepting_) {
      return failure(
        GameLibraryScannerError::alreadyRunning,
        "The game-library scanner is already running.");
    }
    commands_.clear();
    events_.clear();
    stopRequested_ = false;
    cancelRequested_.store(false, std::memory_order_release);
    accepting_ = true;
    shutdownStatus_ = {};
    try {
      thread_ = std::thread{&Private::threadMain, this};
    } catch (const std::system_error& error) {
      accepting_ = false;
      return failure(
        GameLibraryScannerError::threadFailure,
        "The game-library scanner thread could not start: " +
          std::string{error.what()});
    }
    return {};
  }

  GameLibraryScannerStatus requestScan(
    std::uint64_t operationId,
    std::int64_t directoryId)
  {
    if (operationId == 0U || directoryId <= 0) {
      return failure(
        GameLibraryScannerError::invalidRequest,
        "Library scans require a nonzero operation ID and valid directory ID.");
    }
    std::scoped_lock lock{mutex_};
    if (!accepting_ || stopRequested_) {
      return failure(
        GameLibraryScannerError::notRunning,
        "The game-library scanner is not accepting requests.");
    }
    if (!commands_.tryPush({operationId, directoryId})) {
      return failure(
        GameLibraryScannerError::queueFull,
        "The bounded game-library scan queue is full.");
    }
    wake_.notify_one();
    return {};
  }

  std::optional<GameLibraryScanEvent> pollEvent()
  {
    std::scoped_lock lock{mutex_};
    return events_.pop();
  }

  std::optional<GameLibraryScanEvent> waitForEvent(
    std::chrono::milliseconds timeout)
  {
    std::unique_lock lock{mutex_};
    eventReady_.wait_for(lock, timeout, [this] { return !events_.empty(); });
    return events_.pop();
  }

  GameLibraryScannerStatus stop()
  {
    {
      std::scoped_lock lock{mutex_};
      if (!thread_.joinable()) {
        accepting_ = false;
        return shutdownStatus_;
      }
      accepting_ = false;
      stopRequested_ = true;
      cancelRequested_.store(true, std::memory_order_release);
      commands_.clear();
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
    GameLibraryDatabase database{databasePath_};
    const auto initialized = database.initialize();
    if (!initialized) {
      GameLibraryScanEvent event;
      event.type = GameLibraryScanEventType::scanFailed;
      event.status = failure(
        GameLibraryScannerError::databaseFailure,
        initialized.message);
      event.databaseStatus = initialized;
      publish(std::move(event));
      finish(failure(GameLibraryScannerError::databaseFailure, initialized.message));
      return;
    }

    GameLibraryScanEvent started;
    started.type = GameLibraryScanEventType::serviceStarted;
    started.databaseStatus = initialized;
    started.databaseRecovered = database.recoveredCorruption();
    started.recoveryBackupPath = database.recoveryBackupPath();
    publish(std::move(started));

    while (true) {
      std::optional<ScanCommand> command;
      {
        std::unique_lock lock{mutex_};
        wake_.wait(lock, [this] { return stopRequested_ || !commands_.empty(); });
        if (stopRequested_) {
          break;
        }
        command = commands_.pop();
      }
      if (command) {
        process(database, *command);
      }
    }
    static_cast<void>(database.cancelDirectoryScan());
    finish({});
  }

  void process(GameLibraryDatabase& database, const ScanCommand& command)
  {
    const auto directories = database.directories();
    if (!directories.status) {
      publishFailure(command, {}, directories.status,
        GameLibraryScannerError::databaseFailure);
      return;
    }
    const auto found = std::ranges::find_if(
      directories.directories,
      [&command](const auto& directory) {
        return directory.id == command.directoryId;
      });
    if (found == directories.directories.end()) {
      publishFailure(
        command,
        {},
        failureDatabase(
          GameLibraryError::directoryNotFound,
          "The requested game-library directory no longer exists."),
        GameLibraryScannerError::databaseFailure);
      return;
    }
    const auto directory = *found;

    std::error_code filesystemError;
    if (!std::filesystem::is_directory(directory.path, filesystemError) ||
        filesystemError) {
      publishFailure(
        command,
        directory.path,
        failureDatabase(
          GameLibraryError::directoryNotFound,
          "The configured game-library directory is missing."),
        GameLibraryScannerError::databaseFailure);
      return;
    }

    GameLibraryScanEvent started;
    started.type = GameLibraryScanEventType::scanStarted;
    started.operationId = command.operationId;
    started.directoryId = command.directoryId;
    started.directoryPath = directory.path;
    publish(std::move(started));

    const auto scan = database.beginDirectoryScan(command.directoryId);
    if (!scan.status) {
      publishFailure(command, directory.path, scan.status,
        GameLibraryScannerError::databaseFailure);
      return;
    }

    GameLibraryScanSummary summary;
    std::vector<LibraryScanRecord> batch;
    batch.reserve(GameLibraryDatabase::maximumScanBatchSize);
    GameLibraryStatus batchStatus;
    bool limitExceeded = false;
    bool iterationFailed = static_cast<bool>(filesystemError);

    const auto flush = [&] {
      if (batch.empty() || !batchStatus) {
        return;
      }
      batchStatus = database.applyScanBatch(
        command.directoryId, scan.generation, batch);
      batch.clear();
    };
    const auto inspect = [&](const std::filesystem::directory_entry& entry) {
      if (cancelRequested_.load(std::memory_order_acquire) || !batchStatus) {
        return;
      }
      std::error_code error;
      if (entry.is_symlink(error) || error || !entry.is_regular_file(error) || error) {
        return;
      }
      ++summary.visitedFiles;
      if (summary.visitedFiles > GameLibraryScanner::maximumVisitedFilesPerScan) {
        limitExceeded = true;
        return;
      }
      if (!hasSupportedGameExtension(entry.path())) {
        return;
      }
      ++summary.supportedFiles;
      auto metadata = readGameMetadata(entry.path());
      if (!metadata.status) {
        ++summary.skippedFiles;
      } else {
        batch.push_back({
          .metadata = std::move(metadata.metadata),
          .lastModifiedEpochMilliseconds = fileTimeMilliseconds(entry),
        });
        ++summary.indexedGames;
        if (batch.size() == GameLibraryDatabase::maximumScanBatchSize) {
          flush();
        }
      }
      if (summary.visitedFiles % 64U == 0U) {
        publishProgress(command, directory.path, summary);
      }
    };

    if (directory.recursive) {
      std::filesystem::recursive_directory_iterator iterator{
        directory.path,
        std::filesystem::directory_options::skip_permission_denied,
        filesystemError};
      const std::filesystem::recursive_directory_iterator end;
      while (iterator != end && !limitExceeded && batchStatus &&
             !cancelRequested_.load(std::memory_order_acquire)) {
        if (filesystemError) {
          iterationFailed = true;
          ++summary.skippedFiles;
          filesystemError.clear();
        } else {
          inspect(*iterator);
        }
        iterator.increment(filesystemError);
      }
    } else {
      std::filesystem::directory_iterator iterator{
        directory.path,
        std::filesystem::directory_options::skip_permission_denied,
        filesystemError};
      const std::filesystem::directory_iterator end;
      while (iterator != end && !limitExceeded && batchStatus &&
             !cancelRequested_.load(std::memory_order_acquire)) {
        if (filesystemError) {
          iterationFailed = true;
          ++summary.skippedFiles;
          filesystemError.clear();
        } else {
          inspect(*iterator);
        }
        iterator.increment(filesystemError);
      }
    }
    if (filesystemError) {
      iterationFailed = true;
      ++summary.skippedFiles;
    }
    if (!cancelRequested_.load(std::memory_order_acquire) &&
        !limitExceeded && !iterationFailed) {
      flush();
    }

    if (cancelRequested_.load(std::memory_order_acquire)) {
      const auto cancelled = database.cancelDirectoryScan();
      publishFailure(
        command,
        directory.path,
        cancelled,
        GameLibraryScannerError::cancelled,
        summary,
        "The game-library scan was cancelled during shutdown.");
      return;
    }
    if (limitExceeded) {
      const auto cancelled = database.cancelDirectoryScan();
      publishFailure(
        command,
        directory.path,
        cancelled,
        GameLibraryScannerError::scanLimitExceeded,
        summary,
        "The game-library scan exceeded its 100,000-file safety limit.");
      return;
    }
    if (iterationFailed) {
      const auto cancelled = database.cancelDirectoryScan();
      publishFailure(
        command,
        directory.path,
        cancelled,
        GameLibraryScannerError::filesystemFailure,
        summary,
        "The game-library directory could not be enumerated completely.");
      return;
    }
    if (!batchStatus) {
      static_cast<void>(database.cancelDirectoryScan());
      publishFailure(command, directory.path, batchStatus,
        GameLibraryScannerError::databaseFailure, summary);
      return;
    }

    const auto finished = database.finishDirectoryScan(
      command.directoryId, scan.generation);
    if (!finished.status) {
      publishFailure(command, directory.path, finished.status,
        GameLibraryScannerError::databaseFailure, summary);
      return;
    }
    summary.removedGames = finished.removedGames;
    GameLibraryScanEvent completed;
    completed.type = GameLibraryScanEventType::scanCompleted;
    completed.operationId = command.operationId;
    completed.directoryId = command.directoryId;
    completed.directoryPath = directory.path;
    completed.summary = summary;
    publish(std::move(completed));
  }

  static GameLibraryStatus failureDatabase(
    GameLibraryError error,
    std::string message)
  {
    return {.error = error, .message = std::move(message)};
  }

  void publishProgress(
    const ScanCommand& command,
    const std::filesystem::path& path,
    const GameLibraryScanSummary& summary)
  {
    GameLibraryScanEvent event;
    event.type = GameLibraryScanEventType::scanProgress;
    event.operationId = command.operationId;
    event.directoryId = command.directoryId;
    event.directoryPath = path;
    event.summary = summary;
    publish(std::move(event));
  }

  void publishFailure(
    const ScanCommand& command,
    const std::filesystem::path& path,
    GameLibraryStatus databaseStatus,
    GameLibraryScannerError scannerError,
    GameLibraryScanSummary summary = {},
    std::string message = {})
  {
    GameLibraryScanEvent event;
    event.type = GameLibraryScanEventType::scanFailed;
    event.operationId = command.operationId;
    event.directoryId = command.directoryId;
    event.directoryPath = path;
    event.databaseStatus = std::move(databaseStatus);
    event.status = failure(scannerError,
      message.empty() ? event.databaseStatus.message : std::move(message));
    event.summary = summary;
    publish(std::move(event));
  }

  void finish(GameLibraryScannerStatus status)
  {
    {
      std::scoped_lock lock{mutex_};
      accepting_ = false;
      shutdownStatus_ = status;
    }
    GameLibraryScanEvent stopped;
    stopped.type = GameLibraryScanEventType::serviceStopped;
    stopped.status = std::move(status);
    publish(std::move(stopped));
  }

  void publish(GameLibraryScanEvent event)
  {
    std::scoped_lock lock{mutex_};
    static_cast<void>(events_.dropOldestAndPush(std::move(event)));
    eventReady_.notify_all();
  }

  std::filesystem::path databasePath_;
  BoundedQueue<ScanCommand> commands_;
  BoundedQueue<GameLibraryScanEvent> events_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable eventReady_;
  std::thread thread_;
  std::atomic_bool cancelRequested_{false};
  GameLibraryScannerStatus shutdownStatus_;
  bool accepting_{false};
  bool stopRequested_{false};
};

GameLibraryScanner::GameLibraryScanner(
  std::filesystem::path databasePath,
  std::size_t commandCapacity,
  std::size_t eventCapacity)
  : private_(std::make_unique<Private>(
      std::move(databasePath), commandCapacity, eventCapacity))
{
}

GameLibraryScanner::~GameLibraryScanner()
{
  static_cast<void>(private_->stop());
}

GameLibraryScannerStatus GameLibraryScanner::start()
{
  return private_->start();
}

GameLibraryScannerStatus GameLibraryScanner::requestScan(
  std::uint64_t operationId,
  std::int64_t directoryId)
{
  return private_->requestScan(operationId, directoryId);
}

std::optional<GameLibraryScanEvent> GameLibraryScanner::pollEvent()
{
  return private_->pollEvent();
}

std::optional<GameLibraryScanEvent> GameLibraryScanner::waitForEvent(
  std::chrono::milliseconds timeout)
{
  return private_->waitForEvent(timeout);
}

GameLibraryScannerStatus GameLibraryScanner::stop()
{
  return private_->stop();
}

} // namespace genplusgx::library
