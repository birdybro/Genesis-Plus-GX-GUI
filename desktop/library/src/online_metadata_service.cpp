#include "genplusgx/library/online_metadata_service.h"

#include "genplusgx/bounded_queue.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

namespace genplusgx::library {
namespace {

OnlineMetadataStatus failure(OnlineMetadataError error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

struct Command final {
  std::uint64_t operationId{0U};
  std::int64_t libraryGameId{0};
  OnlineMetadataSettings settings;
  GameMetadata game;
  std::filesystem::path cacheDirectory;
};

} // namespace

class OnlineMetadataService::Private final {
public:
  Private(std::size_t commandCapacity, std::size_t eventCapacity,
    OnlineTransportFactory transportFactory)
    : commands_(commandCapacity), events_(eventCapacity),
      transportFactory_(std::move(transportFactory))
  {
    if (!transportFactory_) {
      transportFactory_ = [](OnlineMetadataCancellation cancellation) {
        return std::make_unique<QtOnlineHttpTransport>(std::move(cancellation));
      };
    }
  }

  OnlineMetadataStatus start()
  {
    std::scoped_lock lock{mutex_};
    if (thread_.joinable() || accepting_) {
      return failure(OnlineMetadataError::threadFailure,
        "The online metadata service is already running.");
    }
    commands_.clear();
    events_.clear();
    stopRequested_.store(false, std::memory_order_release);
    accepting_ = true;
    shutdownStatus_ = {};
    try {
      thread_ = std::thread{&Private::threadMain, this};
    } catch (const std::system_error& error) {
      accepting_ = false;
      return failure(OnlineMetadataError::threadFailure,
        "The online metadata thread could not start: " +
          std::string{error.what()});
    }
    return {};
  }

  OnlineMetadataStatus request(
    std::uint64_t operationId,
    std::int64_t libraryGameId,
    OnlineMetadataSettings settings,
    GameMetadata game,
    std::filesystem::path cacheDirectory)
  {
    if (operationId == 0U || libraryGameId <= 0 || game.sha256.empty() ||
        cacheDirectory.empty()) {
      return failure(OnlineMetadataError::invalidRequest,
        "Online metadata requests require operation, game, hash, and cache identities.");
    }
    std::scoped_lock lock{mutex_};
    if (!accepting_ || stopRequested_.load(std::memory_order_acquire)) {
      return failure(OnlineMetadataError::notRunning,
        "The online metadata service is not accepting requests.");
    }
    if (!commands_.tryPush({operationId, libraryGameId, std::move(settings),
          std::move(game), std::move(cacheDirectory)})) {
      return failure(OnlineMetadataError::queueFull,
        "The bounded online metadata request queue is full.");
    }
    wake_.notify_one();
    return {};
  }

  std::optional<OnlineMetadataEvent> pollEvent()
  {
    std::scoped_lock lock{mutex_};
    return events_.pop();
  }

  std::optional<OnlineMetadataEvent> waitForEvent(
    std::chrono::milliseconds timeout)
  {
    std::unique_lock lock{mutex_};
    eventReady_.wait_for(lock, timeout, [this] { return !events_.empty(); });
    return events_.pop();
  }

  OnlineMetadataStatus stop()
  {
    {
      std::scoped_lock lock{mutex_};
      if (!thread_.joinable()) {
        accepting_ = false;
        return shutdownStatus_;
      }
      accepting_ = false;
      stopRequested_.store(true, std::memory_order_release);
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
    publish({.type = OnlineMetadataEventType::serviceStarted,
      .operationId = 0U, .libraryGameId = 0, .result = {}});
    while (true) {
      std::optional<Command> command;
      {
        std::unique_lock lock{mutex_};
        wake_.wait(lock, [this] {
          return stopRequested_.load(std::memory_order_acquire) ||
            !commands_.empty();
        });
        if (stopRequested_.load(std::memory_order_acquire)) {
          break;
        }
        command = commands_.pop();
      }
      if (!command) {
        continue;
      }
      const auto cancellation = [this] {
        return stopRequested_.load(std::memory_order_acquire);
      };
      auto transport = transportFactory_(cancellation);
      OnlineMetadataLookupResult result;
      if (!transport) {
        result.status = failure(OnlineMetadataError::threadFailure,
          "The online metadata HTTP transport could not be created.");
      } else {
        result = lookupOnlineMetadata(command->settings, command->game,
          command->cacheDirectory, *transport, cancellation);
      }
      if (result.status.error == OnlineMetadataError::cancelled &&
          stopRequested_.load(std::memory_order_acquire)) {
        break;
      }
      publish({
        .type = result.status ? OnlineMetadataEventType::lookupCompleted
                              : OnlineMetadataEventType::lookupFailed,
        .operationId = command->operationId,
        .libraryGameId = command->libraryGameId,
        .result = std::move(result),
      });
    }
    {
      std::scoped_lock lock{mutex_};
      accepting_ = false;
      shutdownStatus_ = {};
    }
    publish({.type = OnlineMetadataEventType::serviceStopped,
      .operationId = 0U, .libraryGameId = 0, .result = {}});
  }

  void publish(OnlineMetadataEvent event)
  {
    std::scoped_lock lock{mutex_};
    static_cast<void>(events_.dropOldestAndPush(std::move(event)));
    eventReady_.notify_all();
  }

  BoundedQueue<Command> commands_;
  BoundedQueue<OnlineMetadataEvent> events_;
  OnlineTransportFactory transportFactory_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable eventReady_;
  std::thread thread_;
  OnlineMetadataStatus shutdownStatus_;
  bool accepting_{false};
  std::atomic_bool stopRequested_{false};
};

OnlineMetadataService::OnlineMetadataService(
  std::size_t commandCapacity,
  std::size_t eventCapacity,
  OnlineTransportFactory transportFactory)
  : private_(std::make_unique<Private>(commandCapacity, eventCapacity,
      std::move(transportFactory)))
{
}

OnlineMetadataService::~OnlineMetadataService()
{
  static_cast<void>(private_->stop());
}

OnlineMetadataStatus OnlineMetadataService::start()
{
  return private_->start();
}

OnlineMetadataStatus OnlineMetadataService::request(
  std::uint64_t operationId,
  std::int64_t libraryGameId,
  OnlineMetadataSettings settings,
  GameMetadata game,
  std::filesystem::path cacheDirectory)
{
  return private_->request(operationId, libraryGameId, std::move(settings),
    std::move(game), std::move(cacheDirectory));
}

std::optional<OnlineMetadataEvent> OnlineMetadataService::pollEvent()
{
  return private_->pollEvent();
}

std::optional<OnlineMetadataEvent> OnlineMetadataService::waitForEvent(
  std::chrono::milliseconds timeout)
{
  return private_->waitForEvent(timeout);
}

OnlineMetadataStatus OnlineMetadataService::stop()
{
  return private_->stop();
}

} // namespace genplusgx::library
