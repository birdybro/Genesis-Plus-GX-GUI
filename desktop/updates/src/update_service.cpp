#include "genplusgx/updates/update_service.h"

#include "genplusgx/bounded_queue.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>

namespace genplusgx::updates {
namespace {

Status failure(Error error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

struct CheckCommand final {
  Settings settings;
  std::string currentVersion;
};

struct DownloadCommand final {
  Asset asset;
  std::filesystem::path destinationDirectory;
};

struct Command final {
  std::uint64_t operationId{0U};
  Trust trust;
  std::variant<CheckCommand, DownloadCommand> operation;
};

} // namespace

class Service::Private final {
public:
  Private(std::size_t commandCapacity, std::size_t eventCapacity,
    TransportFactory transportFactory)
    : commands_(commandCapacity), events_(eventCapacity),
      transportFactory_(std::move(transportFactory))
  {
    if (!transportFactory_) {
      transportFactory_ = [](Cancellation cancellation) {
        return std::make_unique<QtHttpTransport>(std::move(cancellation));
      };
    }
  }

  Status start()
  {
    std::scoped_lock lock{mutex_};
    if (thread_.joinable() || accepting_) {
      return failure(Error::threadFailure,
        "The signed update service is already running.");
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
      return failure(Error::threadFailure,
        "The signed update thread could not start: " + std::string{error.what()});
    }
    return {};
  }

  Status push(Command command)
  {
    if (command.operationId == 0U) {
      return failure(Error::invalidRequest,
        "Signed update operations require a nonzero identity.");
    }
    std::scoped_lock lock{mutex_};
    if (!accepting_ || stopRequested_.load(std::memory_order_acquire)) {
      return failure(Error::notRunning,
        "The signed update service is not accepting requests.");
    }
    if (!commands_.tryPush(std::move(command))) {
      return failure(Error::queueFull,
        "The bounded signed update request queue is full.");
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
  void publish(Event event)
  {
    std::scoped_lock lock{mutex_};
    static_cast<void>(events_.dropOldestAndPush(std::move(event)));
    eventReady_.notify_all();
  }

  void threadMain()
  {
    publish({.type = EventType::serviceStarted, .operationId = 0U,
      .check = {}, .download = {}});
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
      if (!transport) {
        Event event{.type = EventType::checkFailed,
          .operationId = command->operationId, .check = {}, .download = {}};
        const auto status = failure(Error::threadFailure,
          "The signed update HTTP transport could not be created.");
        if (std::holds_alternative<CheckCommand>(command->operation)) {
          event.check.status = status;
        } else {
          event.type = EventType::downloadFailed;
          event.download.status = status;
        }
        publish(std::move(event));
        continue;
      }
      if (const auto* check = std::get_if<CheckCommand>(&command->operation)) {
        auto result = checkForUpdate(check->settings, check->currentVersion,
          command->trust, *transport);
        if (result.status.error == Error::cancelled && cancellation()) {
          break;
        }
        publish({.type = result.status ? EventType::checkCompleted
                                      : EventType::checkFailed,
          .operationId = command->operationId, .check = std::move(result),
          .download = {}});
      } else {
        const auto& download = std::get<DownloadCommand>(command->operation);
        auto result = transport->download(download.asset,
          download.destinationDirectory, command->trust);
        if (result.status.error == Error::cancelled && cancellation()) {
          break;
        }
        publish({.type = result.status ? EventType::downloadCompleted
                                      : EventType::downloadFailed,
          .operationId = command->operationId, .check = {},
          .download = std::move(result)});
      }
    }
    {
      std::scoped_lock lock{mutex_};
      accepting_ = false;
      shutdownStatus_ = {};
    }
    publish({.type = EventType::serviceStopped, .operationId = 0U,
      .check = {}, .download = {}});
  }

  BoundedQueue<Command> commands_;
  BoundedQueue<Event> events_;
  TransportFactory transportFactory_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable eventReady_;
  std::thread thread_;
  Status shutdownStatus_;
  bool accepting_{false};
  std::atomic_bool stopRequested_{false};
};

Service::Service(std::size_t commandCapacity, std::size_t eventCapacity,
  TransportFactory transportFactory)
  : private_(std::make_unique<Private>(commandCapacity, eventCapacity,
      std::move(transportFactory)))
{
}

Service::~Service()
{
  static_cast<void>(private_->stop());
}

Status Service::start()
{
  return private_->start();
}

Status Service::requestCheck(std::uint64_t operationId, Settings settings,
  std::string currentVersion, Trust trust)
{
  return private_->push({.operationId = operationId, .trust = std::move(trust),
    .operation = CheckCommand{std::move(settings), std::move(currentVersion)}});
}

Status Service::requestDownload(std::uint64_t operationId, Asset asset,
  std::filesystem::path destinationDirectory, Trust trust)
{
  return private_->push({.operationId = operationId, .trust = std::move(trust),
    .operation = DownloadCommand{std::move(asset),
      std::move(destinationDirectory)}});
}

std::optional<Event> Service::pollEvent()
{
  return private_->pollEvent();
}

std::optional<Event> Service::waitForEvent(std::chrono::milliseconds timeout)
{
  return private_->waitForEvent(timeout);
}

Status Service::stop()
{
  return private_->stop();
}

} // namespace genplusgx::updates
