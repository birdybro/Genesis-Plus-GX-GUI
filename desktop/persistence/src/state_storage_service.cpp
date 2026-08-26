#include "genplusgx/state_storage_service.h"

#include "genplusgx/bounded_queue.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

namespace genplusgx {
namespace {

StateStorageStatus success()
{
  return {};
}

StateStorageStatus failure(StateStorageError error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

StateStorageEvent eventFor(StateStorageEventType type)
{
  StateStorageEvent event;
  event.type = type;
  for (std::uint32_t slot = 0U; slot < event.slotSummaries.size(); ++slot) {
    event.slotSummaries[slot].slot = slot;
  }
  return event;
}

} // namespace

StateStorageCommand StateStorageCommand::activate(
  std::uint64_t operationId,
  std::uint64_t gameGeneration,
  std::filesystem::path path,
  std::uint32_t hardware)
{
  StateStorageCommand command;
  command.type = StateStorageCommandType::activateGame;
  command.operationId = operationId;
  command.gameGeneration = gameGeneration;
  command.path = std::move(path);
  command.hardware = hardware;
  return command;
}

StateStorageCommand StateStorageCommand::simple(
  StateStorageCommandType type,
  std::uint64_t operationId,
  std::uint64_t gameGeneration,
  std::uint32_t slot)
{
  StateStorageCommand command;
  command.type = type;
  command.operationId = operationId;
  command.gameGeneration = gameGeneration;
  command.slot = slot;
  return command;
}

StateStorageCommand StateStorageCommand::save(
  std::uint64_t operationId,
  std::uint64_t gameGeneration,
  std::uint32_t slot,
  std::uint64_t emulatedFrameNumber,
  std::vector<std::uint8_t> rawPayload)
{
  auto command = simple(
    StateStorageCommandType::saveSlot, operationId, gameGeneration, slot);
  command.emulatedFrameNumber = emulatedFrameNumber;
  command.rawPayload = std::move(rawPayload);
  return command;
}

class StateStorageService::Private final {
public:
  Private(
    StateStorageService& owner,
    ApplicationPaths paths,
    std::size_t commandCapacity,
    std::size_t eventCapacity)
    : owner_(owner),
      manager_(std::move(paths)),
      commands_(commandCapacity),
      events_(eventCapacity)
  {
  }

  StateStorageStatus start()
  {
    std::scoped_lock lock{mutex_};
    if (thread_.joinable() || acceptingCommands_) {
      return failure(
        StateStorageError::alreadyRunning,
        "The save-state storage service is already running.");
    }
    commands_.clear();
    events_.clear();
    activeIdentity_.reset();
    activeGeneration_ = 0U;
    activeHardware_ = 0U;
    stopRequested_.store(false, std::memory_order_release);
    acceptingCommands_ = true;
    shutdownStatus_ = success();
    owner_.state_.store(StateStorageServiceState::starting, std::memory_order_release);
    try {
      thread_ = std::thread{&Private::threadMain, this};
    } catch (const std::system_error& error) {
      acceptingCommands_ = false;
      owner_.state_.store(StateStorageServiceState::failed, std::memory_order_release);
      return failure(
        StateStorageError::threadFailure,
        "The save-state storage thread could not start: " +
          std::string{error.what()});
    }
    return success();
  }

  StateStorageStatus submit(StateStorageCommand command)
  {
    if (command.operationId == 0U || command.gameGeneration == 0U) {
      return failure(
        StateStorageError::invalidCommand,
        "Save-state commands require nonzero operation and game-generation IDs.");
    }
    if (command.type != StateStorageCommandType::activateGame &&
        command.type != StateStorageCommandType::deactivateGame &&
        command.slot > SaveStateManager::maximumSlot) {
      return failure(
        StateStorageError::invalidCommand,
        "The save-state slot must be between 0 and 9.");
    }
    if (command.type == StateStorageCommandType::activateGame &&
        command.path.empty()) {
      return failure(
        StateStorageError::invalidCommand,
        "An active save-state session requires a game path.");
    }

    std::scoped_lock lock{mutex_};
    if (!acceptingCommands_ ||
        stopRequested_.load(std::memory_order_acquire)) {
      return failure(
        StateStorageError::notRunning,
        "The save-state storage service is not accepting commands.");
    }
    if (!commands_.tryPush(std::move(command))) {
      return failure(
        StateStorageError::queueFull,
        "The bounded save-state command queue is full.");
    }
    wake_.notify_one();
    return success();
  }

  StateStorageStatus stop()
  {
    {
      std::scoped_lock lock{mutex_};
      if (!thread_.joinable()) {
        acceptingCommands_ = false;
        owner_.state_.store(StateStorageServiceState::stopped, std::memory_order_release);
        return shutdownStatus_;
      }
      acceptingCommands_ = false;
      stopRequested_.store(true, std::memory_order_release);
      owner_.state_.store(StateStorageServiceState::stopping, std::memory_order_release);
      wake_.notify_all();
      eventReady_.notify_all();
    }
    thread_.join();
    std::scoped_lock lock{mutex_};
    return shutdownStatus_;
  }

  std::optional<StateStorageEvent> pollEvent()
  {
    std::scoped_lock lock{mutex_};
    return events_.pop();
  }

  std::optional<StateStorageEvent> waitForEvent(std::chrono::milliseconds timeout)
  {
    std::unique_lock lock{mutex_};
    eventReady_.wait_for(lock, timeout, [this] { return !events_.empty(); });
    return events_.pop();
  }

  StateStorageServiceMetrics metrics() const
  {
    std::scoped_lock lock{mutex_};
    return {
      .commandQueueDepth = commands_.size(),
      .eventQueueDepth = events_.size(),
      .droppedEvents = droppedEvents_,
    };
  }

private:
  void threadMain()
  {
    const auto initialized = manager_.initialize();
    if (!initialized) {
      owner_.state_.store(StateStorageServiceState::failed, std::memory_order_release);
      auto event = eventFor(StateStorageEventType::operationFailed);
      event.error = StateStorageError::persistenceFailure;
      event.saveStateError = initialized.error;
      event.message = initialized.message;
      publish(std::move(event));
      finish(failure(StateStorageError::persistenceFailure, initialized.message));
      return;
    }

    owner_.state_.store(StateStorageServiceState::running, std::memory_order_release);
    publish(eventFor(StateStorageEventType::serviceStarted));

    while (true) {
      std::optional<StateStorageCommand> command;
      {
        std::unique_lock lock{mutex_};
        wake_.wait(lock, [this] {
          return stopRequested_.load(std::memory_order_acquire) ||
            !commands_.empty();
        });
        if (commands_.empty() &&
            stopRequested_.load(std::memory_order_acquire)) {
          break;
        }
        command = commands_.pop();
      }
      if (command) {
        process(std::move(*command));
      }
    }
    finish(success());
  }

  StateSlotSummaries scanSlots() const
  {
    StateSlotSummaries summaries;
    for (std::uint32_t slot = 0U; slot < summaries.size(); ++slot) {
      auto& summary = summaries[slot];
      summary.slot = slot;
      if (!activeIdentity_) {
        continue;
      }
      const auto loaded = manager_.loadSlot(*activeIdentity_, slot, activeHardware_);
      if (loaded.status) {
        summary.availability = StateSlotAvailability::available;
        summary.metadata = loaded.metadata;
      } else if (loaded.status.error == SaveStateError::missingState) {
        summary.availability = StateSlotAvailability::empty;
      } else {
        summary.availability = StateSlotAvailability::invalid;
        summary.message = loaded.status.message;
      }
    }
    return summaries;
  }

  void process(StateStorageCommand command)
  {
    auto event = eventFor(StateStorageEventType::operationFailed);
    event.command = command.type;
    event.operationId = command.operationId;
    event.gameGeneration = command.gameGeneration;
    event.slot = command.slot;

    if (command.type == StateStorageCommandType::activateGame) {
      activeIdentity_.reset();
      activeGeneration_ = 0U;
      activeHardware_ = 0U;
      const auto identified = identifyGame(command.path, {}, [this] {
        return stopRequested_.load(std::memory_order_acquire);
      });
      if (!identified.status) {
        event.error = StateStorageError::persistenceFailure;
        event.message = identified.status.message;
        publish(std::move(event));
        return;
      }
      activeIdentity_ = identified.identity;
      activeGeneration_ = command.gameGeneration;
      activeHardware_ = command.hardware;
      event.type = StateStorageEventType::sessionActivated;
      event.slotSummaries = scanSlots();
      publish(std::move(event));
      return;
    }

    if (!activeIdentity_ || command.gameGeneration != activeGeneration_) {
      event.error = StateStorageError::staleGame;
      event.message = "The save-state request belongs to an inactive game session.";
      publish(std::move(event));
      return;
    }

    if (command.type == StateStorageCommandType::deactivateGame) {
      activeIdentity_.reset();
      activeGeneration_ = 0U;
      activeHardware_ = 0U;
      event.type = StateStorageEventType::sessionDeactivated;
      publish(std::move(event));
      return;
    }

    SaveStateStatus status;
    switch (command.type) {
      case StateStorageCommandType::saveSlot:
        status = manager_.saveSlot(
          *activeIdentity_,
          command.slot,
          activeHardware_,
          command.emulatedFrameNumber,
          command.rawPayload);
        if (status) {
          event.type = StateStorageEventType::slotSaved;
          event.slotSummaries = scanSlots();
        }
        break;
      case StateStorageCommandType::loadSlot: {
        auto loaded = manager_.loadSlot(
          *activeIdentity_, command.slot, activeHardware_);
        status = loaded.status;
        if (status) {
          event.type = StateStorageEventType::slotLoaded;
          event.metadata = loaded.metadata;
          event.rawPayload = std::move(loaded.rawPayload);
          event.slotSummaries = scanSlots();
        }
        break;
      }
      case StateStorageCommandType::deleteSlot:
        status = manager_.deleteSlot(*activeIdentity_, command.slot);
        if (status) {
          event.type = StateStorageEventType::slotDeleted;
          event.slotSummaries = scanSlots();
        }
        break;
      case StateStorageCommandType::refreshSlots:
        event.type = StateStorageEventType::slotsRefreshed;
        event.slotSummaries = scanSlots();
        break;
      case StateStorageCommandType::activateGame:
      case StateStorageCommandType::deactivateGame:
        break;
    }
    if (!status) {
      event.error = StateStorageError::persistenceFailure;
      event.saveStateError = status.error;
      event.message = status.message;
    }
    publish(std::move(event));
  }

  void finish(StateStorageStatus status)
  {
    activeIdentity_.reset();
    activeGeneration_ = 0U;
    activeHardware_ = 0U;
    owner_.state_.store(StateStorageServiceState::stopped, std::memory_order_release);
    {
      std::scoped_lock lock{mutex_};
      acceptingCommands_ = false;
      shutdownStatus_ = status;
    }
    auto event = eventFor(StateStorageEventType::serviceStopped);
    event.error = status.error;
    event.message = status.message;
    publish(std::move(event));
  }

  void publish(StateStorageEvent event)
  {
    std::scoped_lock lock{mutex_};
    if (events_.dropOldestAndPush(std::move(event))) {
      ++droppedEvents_;
    }
    eventReady_.notify_all();
  }

  StateStorageService& owner_;
  SaveStateManager manager_;
  BoundedQueue<StateStorageCommand> commands_;
  BoundedQueue<StateStorageEvent> events_;
  std::optional<GameIdentity> activeIdentity_;
  std::uint64_t activeGeneration_{0};
  std::uint32_t activeHardware_{0};
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable eventReady_;
  std::thread thread_;
  bool acceptingCommands_{false};
  std::atomic_bool stopRequested_{false};
  StateStorageStatus shutdownStatus_;
  std::uint64_t droppedEvents_{0};
};

StateStorageService::StateStorageService(
  ApplicationPaths paths,
  std::size_t commandCapacity,
  std::size_t eventCapacity)
  : private_(std::make_unique<Private>(
      *this, std::move(paths), commandCapacity, eventCapacity))
{
}

StateStorageService::~StateStorageService()
{
  static_cast<void>(private_->stop());
}

StateStorageStatus StateStorageService::start()
{
  return private_->start();
}

StateStorageStatus StateStorageService::submit(StateStorageCommand command)
{
  return private_->submit(std::move(command));
}

StateStorageStatus StateStorageService::stop()
{
  return private_->stop();
}

std::optional<StateStorageEvent> StateStorageService::pollEvent()
{
  return private_->pollEvent();
}

std::optional<StateStorageEvent> StateStorageService::waitForEvent(
  std::chrono::milliseconds timeout)
{
  return private_->waitForEvent(timeout);
}

StateStorageServiceState StateStorageService::state() const noexcept
{
  return state_.load(std::memory_order_acquire);
}

StateStorageServiceMetrics StateStorageService::metrics() const
{
  return private_->metrics();
}

} // namespace genplusgx
