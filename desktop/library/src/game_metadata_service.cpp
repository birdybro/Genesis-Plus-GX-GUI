#include "genplusgx/library/game_metadata_service.h"

#include "genplusgx/bounded_queue.h"

#include <condition_variable>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

namespace genplusgx::library {
namespace {

GameMetadataServiceStatus failure(
  GameMetadataServiceError error,
  std::string message)
{
  return {.error = error, .message = std::move(message)};
}

struct MetadataCommand final {
  std::uint64_t operationId{0};
  std::filesystem::path path;
};

} // namespace

class GameMetadataService::Private final {
public:
  Private(std::size_t commandCapacity, std::size_t eventCapacity)
    : commands_(commandCapacity), events_(eventCapacity)
  {
  }

  GameMetadataServiceStatus start()
  {
    std::scoped_lock lock{mutex_};
    if (thread_.joinable() || accepting_) {
      return failure(
        GameMetadataServiceError::alreadyRunning,
        "The game metadata service is already running.");
    }
    commands_.clear();
    events_.clear();
    stopRequested_ = false;
    accepting_ = true;
    shutdownStatus_ = {};
    try {
      thread_ = std::thread{&Private::threadMain, this};
    } catch (const std::system_error& error) {
      accepting_ = false;
      return failure(
        GameMetadataServiceError::threadFailure,
        "The game metadata thread could not start: " + std::string{error.what()});
    }
    return {};
  }

  GameMetadataServiceStatus request(
    std::uint64_t operationId,
    std::filesystem::path path)
  {
    if (operationId == 0U || path.empty()) {
      return failure(
        GameMetadataServiceError::invalidRequest,
        "Metadata requests require a nonzero operation ID and game path.");
    }
    std::scoped_lock lock{mutex_};
    if (!accepting_ || stopRequested_) {
      return failure(
        GameMetadataServiceError::notRunning,
        "The game metadata service is not accepting requests.");
    }
    if (!commands_.tryPush({operationId, std::move(path)})) {
      return failure(
        GameMetadataServiceError::queueFull,
        "The bounded game metadata request queue is full.");
    }
    wake_.notify_one();
    return {};
  }

  std::optional<GameMetadataEvent> pollEvent()
  {
    std::scoped_lock lock{mutex_};
    return events_.pop();
  }

  std::optional<GameMetadataEvent> waitForEvent(std::chrono::milliseconds timeout)
  {
    std::unique_lock lock{mutex_};
    eventReady_.wait_for(lock, timeout, [this] { return !events_.empty(); });
    return events_.pop();
  }

  GameMetadataServiceStatus stop()
  {
    {
      std::scoped_lock lock{mutex_};
      if (!thread_.joinable()) {
        accepting_ = false;
        return shutdownStatus_;
      }
      accepting_ = false;
      stopRequested_ = true;
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
    GameMetadataEvent started;
    started.type = GameMetadataEventType::serviceStarted;
    publish(std::move(started));
    while (true) {
      std::optional<MetadataCommand> command;
      {
        std::unique_lock lock{mutex_};
        wake_.wait(lock, [this] { return stopRequested_ || !commands_.empty(); });
        if (stopRequested_) {
          break;
        }
        command = commands_.pop();
      }
      if (!command) {
        continue;
      }
      auto result = readGameMetadata(command->path);
      GameMetadataEvent event;
      event.type = result.status
        ? GameMetadataEventType::metadataReady
        : GameMetadataEventType::operationFailed;
      event.operationId = command->operationId;
      event.path = std::move(command->path);
      event.status = std::move(result.status);
      event.metadata = std::move(result.metadata);
      publish(std::move(event));
    }
    {
      std::scoped_lock lock{mutex_};
      accepting_ = false;
      shutdownStatus_ = {};
    }
    GameMetadataEvent stopped;
    stopped.type = GameMetadataEventType::serviceStopped;
    publish(std::move(stopped));
  }

  void publish(GameMetadataEvent event)
  {
    std::scoped_lock lock{mutex_};
    static_cast<void>(events_.dropOldestAndPush(std::move(event)));
    eventReady_.notify_all();
  }

  BoundedQueue<MetadataCommand> commands_;
  BoundedQueue<GameMetadataEvent> events_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable eventReady_;
  std::thread thread_;
  GameMetadataServiceStatus shutdownStatus_;
  bool accepting_{false};
  bool stopRequested_{false};
};

GameMetadataService::GameMetadataService(
  std::size_t commandCapacity,
  std::size_t eventCapacity)
  : private_(std::make_unique<Private>(commandCapacity, eventCapacity))
{
}

GameMetadataService::~GameMetadataService()
{
  static_cast<void>(private_->stop());
}

GameMetadataServiceStatus GameMetadataService::start()
{
  return private_->start();
}

GameMetadataServiceStatus GameMetadataService::request(
  std::uint64_t operationId,
  std::filesystem::path path)
{
  return private_->request(operationId, std::move(path));
}

std::optional<GameMetadataEvent> GameMetadataService::pollEvent()
{
  return private_->pollEvent();
}

std::optional<GameMetadataEvent> GameMetadataService::waitForEvent(
  std::chrono::milliseconds timeout)
{
  return private_->waitForEvent(timeout);
}

GameMetadataServiceStatus GameMetadataService::stop()
{
  return private_->stop();
}

} // namespace genplusgx::library
