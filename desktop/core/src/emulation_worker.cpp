#include "genplusgx/emulation_worker.h"

#include "genplusgx/bounded_queue.h"

#include <condition_variable>
#include <mutex>
#include <system_error>
#include <utility>

namespace genplusgx {
namespace {

using Clock = std::chrono::steady_clock;
constexpr auto normalFrameInterval = std::chrono::microseconds{16'667};
constexpr auto fastFrameInterval = std::chrono::milliseconds{1};

EmulationWorkerStatus success()
{
  return {};
}

EmulationWorkerStatus failure(EmulationWorkerError error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

EmulationEvent eventFor(
  EmulationEventType type,
  EmulationWorkerState state)
{
  EmulationEvent event;
  event.type = type;
  event.workerState = state;
  event.workerThreadId = std::this_thread::get_id();
  return event;
}

} // namespace

EmulationCommand EmulationCommand::simple(
  EmulationCommandType type,
  std::uint64_t operationId)
{
  EmulationCommand command;
  command.type = type;
  command.operationId = operationId;
  return command;
}

EmulationCommand EmulationCommand::load(
  std::uint64_t operationId,
  std::filesystem::path path)
{
  auto command = simple(EmulationCommandType::loadGame, operationId);
  command.path = std::move(path);
  return command;
}

EmulationCommand EmulationCommand::fastForward(
  std::uint64_t operationId,
  bool enabled)
{
  auto command = simple(EmulationCommandType::setFastForward, operationId);
  command.enabled = enabled;
  return command;
}

EmulationCommand EmulationCommand::updateInput(
  std::uint64_t operationId,
  InputSnapshot input)
{
  auto command = simple(EmulationCommandType::inputSnapshot, operationId);
  command.input = std::move(input);
  return command;
}

EmulationCommand EmulationCommand::restore(
  std::uint64_t operationId,
  std::span<const std::uint8_t> rawState)
{
  auto command = simple(EmulationCommandType::restoreState, operationId);
  command.rawState.assign(rawState.begin(), rawState.end());
  return command;
}

class EmulationWorker::Private final {
public:
  Private(
    EmulationWorker& owner,
    std::size_t commandCapacity,
    std::size_t eventCapacity,
    int audioSampleRate)
    : owner_(owner),
      commands_(commandCapacity),
      events_(eventCapacity),
      audioSampleRate_(audioSampleRate)
  {
  }

  EmulationWorkerStatus start()
  {
    std::scoped_lock lock{mutex_};
    if (thread_.joinable() || acceptingCommands_) {
      return failure(
        EmulationWorkerError::alreadyRunning,
        "The emulation worker is already running.");
    }
    commands_.clear();
    events_.clear();
    latestFrame_.reset();
    stopRequested_ = false;
    acceptingCommands_ = true;
    fastForward_ = false;
    owner_.state_.store(EmulationWorkerState::starting, std::memory_order_release);
    try {
      thread_ = std::thread{&Private::threadMain, this};
    } catch (const std::system_error& error) {
      acceptingCommands_ = false;
      owner_.state_.store(EmulationWorkerState::stopped, std::memory_order_release);
      return failure(
        EmulationWorkerError::threadFailure,
        "The emulation thread could not be started: " + std::string{error.what()});
    }
    return success();
  }

  EmulationWorkerStatus submit(EmulationCommand command)
  {
    if (command.operationId == 0U) {
      return failure(
        EmulationWorkerError::invalidCommand,
        "A command operation ID must be nonzero.");
    }

    std::scoped_lock lock{mutex_};
    if (!acceptingCommands_ || stopRequested_) {
      return failure(
        EmulationWorkerError::notRunning,
        "The emulation worker is not accepting commands.");
    }
    if (command.type == EmulationCommandType::inputSnapshot &&
        commands_.replaceNewestMatching(
          [](const EmulationCommand& queued) {
            return queued.type == EmulationCommandType::inputSnapshot;
          },
          std::move(command))) {
      ++coalescedInputCommands_;
      wake_.notify_one();
      return success();
    }
    if (!commands_.tryPush(std::move(command))) {
      return failure(
        EmulationWorkerError::queueFull,
        "The bounded emulation command queue is full.");
    }
    wake_.notify_one();
    return success();
  }

  EmulationWorkerStatus requestStop()
  {
    {
      std::scoped_lock lock{mutex_};
      if (!thread_.joinable()) {
        acceptingCommands_ = false;
        owner_.state_.store(EmulationWorkerState::stopped, std::memory_order_release);
        return success();
      }
      acceptingCommands_ = false;
      stopRequested_ = true;
      owner_.state_.store(EmulationWorkerState::stopping, std::memory_order_release);
      wake_.notify_all();
      eventReady_.notify_all();
    }
    thread_.join();
    return success();
  }

  std::optional<EmulationEvent> pollEvent()
  {
    std::scoped_lock lock{mutex_};
    return takeEvent();
  }

  std::optional<EmulationEvent> waitForEvent(std::chrono::milliseconds timeout)
  {
    std::unique_lock lock{mutex_};
    eventReady_.wait_for(lock, timeout, [this] {
      return !events_.empty() || latestFrame_.has_value();
    });
    return takeEvent();
  }

  EmulationWorkerMetrics metrics() const
  {
    std::scoped_lock lock{mutex_};
    return {
      .commandQueueDepth = commands_.size(),
      .eventQueueDepth = events_.size() + (latestFrame_.has_value() ? 1U : 0U),
      .coalescedInputCommands = coalescedInputCommands_,
      .replacedFrameEvents = replacedFrameEvents_,
      .droppedOperationEvents = droppedOperationEvents_,
    };
  }

private:
  void threadMain()
  {
    CoreAdapter adapter{audioSampleRate_};
    const auto initialized = adapter.initialize();
    if (!initialized) {
      owner_.state_.store(EmulationWorkerState::failed, std::memory_order_release);
      auto event = eventFor(
        EmulationEventType::commandFailed, EmulationWorkerState::failed);
      event.error = EmulationWorkerError::coreFailure;
      event.coreError = initialized.error;
      event.message = initialized.message;
      publishOperation(std::move(event));
      finishThread(adapter);
      return;
    }

    owner_.state_.store(EmulationWorkerState::idle, std::memory_order_release);
    publishOperation(eventFor(
      EmulationEventType::workerStarted, EmulationWorkerState::idle));

    auto nextFrame = Clock::now();
    while (true) {
      std::optional<EmulationCommand> command;
      bool executeFrame = false;
      {
        std::unique_lock lock{mutex_};
        if (stopRequested_) {
          break;
        }
        if (!commands_.empty()) {
          command = commands_.pop();
        } else if (owner_.state_.load(std::memory_order_acquire) ==
                   EmulationWorkerState::running) {
          const bool interrupted = wake_.wait_until(lock, nextFrame, [this] {
            return stopRequested_ || !commands_.empty();
          });
          if (stopRequested_) {
            break;
          }
          if (interrupted) {
            command = commands_.pop();
          } else {
            executeFrame = true;
          }
        } else {
          wake_.wait(lock, [this] { return stopRequested_ || !commands_.empty(); });
          if (stopRequested_) {
            break;
          }
          command = commands_.pop();
        }
      }

      if (command) {
        processCommand(adapter, std::move(*command));
        nextFrame = Clock::now();
        continue;
      }
      if (executeFrame) {
        runOneFrame(adapter);
        const auto interval = fastForward_ ? fastFrameInterval : normalFrameInterval;
        nextFrame += interval;
        if (nextFrame < Clock::now()) {
          nextFrame = Clock::now() + interval;
        }
      }
    }

    finishThread(adapter);
  }

  void finishThread(CoreAdapter& adapter)
  {
    const auto shutdown = adapter.shutdown();
    owner_.state_.store(EmulationWorkerState::stopped, std::memory_order_release);
    {
      std::scoped_lock lock{mutex_};
      acceptingCommands_ = false;
    }
    auto event = eventFor(
      EmulationEventType::workerStopped, EmulationWorkerState::stopped);
    event.error = shutdown ? EmulationWorkerError::none
                           : EmulationWorkerError::coreFailure;
    event.coreError = shutdown.error;
    event.message = shutdown.message;
    publishOperation(std::move(event));
  }

  void processCommand(CoreAdapter& adapter, EmulationCommand command)
  {
    CoreResult coreResult;
    bool validTransition = true;
    EmulationEventType eventType = EmulationEventType::commandCompleted;
    std::vector<std::uint8_t> capturedState;
    const auto current = owner_.state_.load(std::memory_order_acquire);

    switch (command.type) {
      case EmulationCommandType::loadGame:
        discardLatestFrame();
        coreResult = adapter.loadGame(command.path);
        if (coreResult) {
          owner_.state_.store(EmulationWorkerState::paused, std::memory_order_release);
        } else if (adapter.state() != CoreLifecycleState::loaded) {
          owner_.state_.store(EmulationWorkerState::idle, std::memory_order_release);
          fastForward_ = false;
        }
        break;
      case EmulationCommandType::unloadGame:
        discardLatestFrame();
        coreResult = adapter.unloadGame();
        if (coreResult) {
          owner_.state_.store(EmulationWorkerState::idle, std::memory_order_release);
          fastForward_ = false;
        }
        break;
      case EmulationCommandType::start:
      case EmulationCommandType::resume:
        validTransition = current == EmulationWorkerState::paused;
        if (validTransition) {
          owner_.state_.store(EmulationWorkerState::running, std::memory_order_release);
        }
        break;
      case EmulationCommandType::pause:
        validTransition = current == EmulationWorkerState::running;
        if (validTransition) {
          owner_.state_.store(EmulationWorkerState::paused, std::memory_order_release);
        }
        break;
      case EmulationCommandType::hardReset:
        discardLatestFrame();
        coreResult = adapter.reset();
        break;
      case EmulationCommandType::softReset:
        discardLatestFrame();
        coreResult = adapter.softReset();
        break;
      case EmulationCommandType::frameAdvance:
        validTransition = current == EmulationWorkerState::paused;
        if (validTransition) {
          coreResult = adapter.runFrame(false);
        }
        break;
      case EmulationCommandType::setFastForward:
        validTransition = current == EmulationWorkerState::paused ||
                          current == EmulationWorkerState::running;
        if (validTransition) {
          fastForward_ = command.enabled;
        }
        break;
      case EmulationCommandType::inputSnapshot:
        coreResult = adapter.setInputSnapshot(command.input);
        break;
      case EmulationCommandType::captureState:
        coreResult = adapter.saveRawState(capturedState);
        if (coreResult) {
          eventType = EmulationEventType::stateCaptured;
        }
        break;
      case EmulationCommandType::restoreState:
        discardLatestFrame();
        coreResult = adapter.loadRawState(command.rawState);
        break;
    }

    if (!validTransition) {
      auto event = eventFor(EmulationEventType::commandFailed, current);
      event.command = command.type;
      event.operationId = command.operationId;
      event.error = EmulationWorkerError::invalidTransition;
      event.message = "The command is invalid in the current emulation state.";
      event.frameNumber = adapter.frameCount();
      event.appliedInputSequence = adapter.appliedInputSequence();
      event.fastForward = fastForward_;
      publishOperation(std::move(event));
      return;
    }
    if (!coreResult) {
      auto event = eventFor(EmulationEventType::commandFailed,
        owner_.state_.load(std::memory_order_acquire));
      event.command = command.type;
      event.operationId = command.operationId;
      event.error = EmulationWorkerError::coreFailure;
      event.coreError = coreResult.error;
      event.message = coreResult.message;
      event.frameNumber = adapter.frameCount();
      event.appliedInputSequence = adapter.appliedInputSequence();
      event.fastForward = fastForward_;
      publishOperation(std::move(event));
      return;
    }

    auto event = eventFor(
      eventType, owner_.state_.load(std::memory_order_acquire));
    event.command = command.type;
    event.operationId = command.operationId;
    event.frameNumber = adapter.frameCount();
    event.appliedInputSequence = adapter.appliedInputSequence();
    event.fastForward = fastForward_;
    event.rawState = std::move(capturedState);
    publishOperation(std::move(event));
  }

  void runOneFrame(CoreAdapter& adapter)
  {
    const auto result = adapter.runFrame(false);
    if (!result) {
      owner_.state_.store(EmulationWorkerState::paused, std::memory_order_release);
      auto event = eventFor(
        EmulationEventType::commandFailed, EmulationWorkerState::paused);
      event.error = EmulationWorkerError::coreFailure;
      event.coreError = result.error;
      event.message = result.message;
      event.frameNumber = adapter.frameCount();
      publishOperation(std::move(event));
      return;
    }

    std::scoped_lock lock{mutex_};
    if (latestFrame_) {
      ++replacedFrameEvents_;
    }
    auto event = eventFor(
      EmulationEventType::frameCompleted, EmulationWorkerState::running);
    event.frameNumber = adapter.frameCount();
    event.appliedInputSequence = adapter.appliedInputSequence();
    event.fastForward = fastForward_;
    latestFrame_ = std::move(event);
    eventReady_.notify_all();
  }

  void publishOperation(EmulationEvent event)
  {
    std::scoped_lock lock{mutex_};
    if (events_.dropOldestAndPush(std::move(event))) {
      ++droppedOperationEvents_;
    }
    eventReady_.notify_all();
  }

  void discardLatestFrame()
  {
    std::scoped_lock lock{mutex_};
    latestFrame_.reset();
  }

  std::optional<EmulationEvent> takeEvent()
  {
    if (auto event = events_.pop()) {
      return event;
    }
    if (latestFrame_) {
      auto event = std::move(latestFrame_);
      latestFrame_.reset();
      return event;
    }
    return std::nullopt;
  }

  EmulationWorker& owner_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable eventReady_;
  BoundedQueue<EmulationCommand> commands_;
  BoundedQueue<EmulationEvent> events_;
  std::optional<EmulationEvent> latestFrame_;
  std::thread thread_;
  int audioSampleRate_;
  bool acceptingCommands_{false};
  bool stopRequested_{false};
  bool fastForward_{false};
  std::uint64_t coalescedInputCommands_{0};
  std::uint64_t replacedFrameEvents_{0};
  std::uint64_t droppedOperationEvents_{0};
};

EmulationWorker::EmulationWorker(
  std::size_t commandCapacity,
  std::size_t eventCapacity,
  int audioSampleRate)
  : private_(std::make_unique<Private>(
      *this, commandCapacity, eventCapacity, audioSampleRate))
{
}

EmulationWorker::~EmulationWorker()
{
  static_cast<void>(stop());
}

EmulationWorkerStatus EmulationWorker::start()
{
  return private_->start();
}

EmulationWorkerStatus EmulationWorker::submit(EmulationCommand command)
{
  return private_->submit(std::move(command));
}

EmulationWorkerStatus EmulationWorker::stop()
{
  return private_->requestStop();
}

std::optional<EmulationEvent> EmulationWorker::pollEvent()
{
  return private_->pollEvent();
}

std::optional<EmulationEvent> EmulationWorker::waitForEvent(
  std::chrono::milliseconds timeout)
{
  return private_->waitForEvent(timeout);
}

EmulationWorkerState EmulationWorker::state() const noexcept
{
  return state_.load(std::memory_order_acquire);
}

EmulationWorkerMetrics EmulationWorker::metrics() const
{
  return private_->metrics();
}

} // namespace genplusgx
