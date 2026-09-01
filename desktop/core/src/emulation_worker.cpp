#include "genplusgx/emulation_worker.h"

#include "genplusgx/bounded_queue.h"
#include "genplusgx/rewind_buffer.h"
#include "genplusgx/timing/host_timer_resolution.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <system_error>
#include <utility>

namespace genplusgx {
namespace {

using Clock = std::chrono::steady_clock;
constexpr std::size_t maximumAudioFramesPerBatch = 4'096U;
constexpr std::size_t defaultAudioRingFrames = 12'000U;
constexpr std::array backupMemoryKinds{
  BackupMemoryKind::cartridgeSram,
  BackupMemoryKind::scdInternalBram,
  BackupMemoryKind::scdRamCartridge,
};

CoreResult persistenceFailure(std::string message)
{
  return {
    .error = CoreError::persistenceFailed,
    .message = std::move(message),
  };
}

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

EmulationCommand EmulationCommand::slowMotion(
  std::uint64_t operationId,
  bool enabled)
{
  auto command = simple(EmulationCommandType::setSlowMotion, operationId);
  command.enabled = enabled;
  return command;
}

EmulationCommand EmulationCommand::updateSpeedSettings(
  std::uint64_t operationId,
  EmulationSpeedConfiguration configuration)
{
  auto command = simple(EmulationCommandType::speedSettings, operationId);
  command.speedConfiguration = configuration;
  return command;
}

EmulationCommand EmulationCommand::rewinding(
  std::uint64_t operationId,
  bool enabled)
{
  auto command = simple(EmulationCommandType::setRewinding, operationId);
  command.enabled = enabled;
  return command;
}

EmulationCommand EmulationCommand::updateRewindSettings(
  std::uint64_t operationId,
  RewindConfiguration configuration)
{
  auto command = simple(EmulationCommandType::rewindSettings, operationId);
  command.rewindConfiguration = configuration;
  return command;
}

EmulationCommand EmulationCommand::updateRunAheadSettings(
  std::uint64_t operationId,
  RunAheadConfiguration configuration)
{
  auto command = simple(EmulationCommandType::runAheadSettings, operationId);
  command.runAheadConfiguration = configuration;
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

EmulationCommand EmulationCommand::updateInputSettings(
  std::uint64_t operationId,
  CoreInputSettings settings)
{
  auto command = simple(EmulationCommandType::inputSettings, operationId);
  command.coreInputSettings = settings;
  return command;
}

EmulationCommand EmulationCommand::updateVideoSettings(
  std::uint64_t operationId,
  CoreVideoSettings settings)
{
  auto command = simple(EmulationCommandType::videoSettings, operationId);
  command.coreVideoSettings = settings;
  return command;
}

EmulationCommand EmulationCommand::updateAudioSettings(
  std::uint64_t operationId,
  CoreAudioSettings settings)
{
  auto command = simple(EmulationCommandType::audioSettings, operationId);
  command.coreAudioSettings = settings;
  return command;
}

EmulationCommand EmulationCommand::updateSystemSettings(
  std::uint64_t operationId,
  CoreSystemSettings settings)
{
  auto command = simple(EmulationCommandType::systemSettings, operationId);
  command.coreSystemSettings = settings;
  return command;
}

EmulationCommand EmulationCommand::updateFirmwareSettings(
  std::uint64_t operationId,
  CoreFirmwareSettings settings)
{
  auto command = simple(EmulationCommandType::firmwareSettings, operationId);
  command.coreFirmwareSettings = std::move(settings);
  return command;
}

EmulationCommand EmulationCommand::updateCheats(
  std::uint64_t operationId,
  std::span<const CoreCheatPatch> patches)
{
  auto command = simple(EmulationCommandType::cheats, operationId);
  command.coreCheats.assign(patches.begin(), patches.end());
  return command;
}

EmulationCommand EmulationCommand::discEjected(
  std::uint64_t operationId,
  bool ejected)
{
  auto command = simple(EmulationCommandType::setDiscEjected, operationId);
  command.enabled = ejected;
  return command;
}

EmulationCommand EmulationCommand::changeDisc(
  std::uint64_t operationId,
  std::filesystem::path path)
{
  auto command = simple(EmulationCommandType::changeDisc, operationId);
  command.path = std::move(path);
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

EmulationCommand EmulationCommand::debug(
  std::uint64_t operationId,
  CoreDebugRequest request)
{
  auto command = simple(EmulationCommandType::debugRequest, operationId);
  command.coreDebugRequest = std::move(request);
  return command;
}

class EmulationWorker::Private final {
public:
  Private(
    EmulationWorker& owner,
    std::size_t commandCapacity,
    std::size_t eventCapacity,
    int audioSampleRate,
    std::shared_ptr<VideoFrameExchange> videoFrames,
    std::shared_ptr<StereoAudioRingBuffer> audioFrames,
    std::shared_ptr<BackupMemoryPersistence> backupPersistence,
    std::shared_ptr<EmulationCaptureSink> captureSink)
    : owner_(owner),
      commands_(commandCapacity),
      events_(eventCapacity),
      audioSampleRate_(audioSampleRate),
      videoFrames_(videoFrames ? std::move(videoFrames)
                               : std::make_shared<VideoFrameExchange>()),
      audioFrames_(audioFrames ? std::move(audioFrames)
                               : std::make_shared<StereoAudioRingBuffer>(
                                   defaultAudioRingFrames)),
      backupPersistence_(std::move(backupPersistence)),
      captureSink_(std::move(captureSink))
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
    stopRequested_.store(false, std::memory_order_release);
    shutdownStatus_ = success();
    acceptingCommands_ = true;
    fastForward_ = false;
    slowMotion_ = false;
    rewinding_ = false;
    rewindBuffer_.clear();
    resetRunAheadSession(false);
    latestInput_ = {};
    hasLatestInput_ = false;
    frameBreakpoints_.clear();
    lastBreakpointHit_.reset();
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
    if (command.type == EmulationCommandType::debugRequest &&
        (command.coreDebugRequest.bytes.size() >
           CoreAdapter::maximumDebugTransferBytes ||
         command.coreDebugRequest.breakpoints.size() >
           maximumCoreDebugBreakpoints)) {
      return failure(
        EmulationWorkerError::invalidCommand,
        "The debug command payload exceeds its fixed transfer limit.");
    }

    std::scoped_lock lock{mutex_};
    if (!acceptingCommands_ ||
        stopRequested_.load(std::memory_order_acquire)) {
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
    if (command.type == EmulationCommandType::inputSettings &&
        commands_.replaceNewestMatching(
          [](const EmulationCommand& queued) {
            return queued.type == EmulationCommandType::inputSettings;
          },
          std::move(command))) {
      ++coalescedInputSettingsCommands_;
      wake_.notify_one();
      return success();
    }
    if (command.type == EmulationCommandType::videoSettings &&
        commands_.replaceNewestMatching(
          [](const EmulationCommand& queued) {
            return queued.type == EmulationCommandType::videoSettings;
          },
          std::move(command))) {
      ++coalescedVideoSettingsCommands_;
      wake_.notify_one();
      return success();
    }
    if (command.type == EmulationCommandType::audioSettings &&
        commands_.replaceNewestMatching(
          [](const EmulationCommand& queued) {
            return queued.type == EmulationCommandType::audioSettings;
          },
          std::move(command))) {
      ++coalescedAudioSettingsCommands_;
      wake_.notify_one();
      return success();
    }
    if (command.type == EmulationCommandType::systemSettings &&
        commands_.replaceNewestMatching(
          [](const EmulationCommand& queued) {
            return queued.type == EmulationCommandType::systemSettings;
          },
          std::move(command))) {
      ++coalescedSystemSettingsCommands_;
      wake_.notify_one();
      return success();
    }
    if (command.type == EmulationCommandType::firmwareSettings &&
        commands_.replaceNewestMatching(
          [](const EmulationCommand& queued) {
            return queued.type == EmulationCommandType::firmwareSettings;
          },
          std::move(command))) {
      ++coalescedFirmwareSettingsCommands_;
      wake_.notify_one();
      return success();
    }
    if (command.type == EmulationCommandType::cheats &&
        commands_.replaceNewestMatching(
          [](const EmulationCommand& queued) {
            return queued.type == EmulationCommandType::cheats;
          },
          std::move(command))) {
      ++coalescedCheatCommands_;
      wake_.notify_one();
      return success();
    }
    if (command.type == EmulationCommandType::rewindSettings &&
        commands_.replaceNewestMatching(
          [](const EmulationCommand& queued) {
            return queued.type == EmulationCommandType::rewindSettings;
          },
          std::move(command))) {
      ++coalescedRewindSettingsCommands_;
      wake_.notify_one();
      return success();
    }
    if (command.type == EmulationCommandType::runAheadSettings &&
        commands_.replaceNewestMatching(
          [](const EmulationCommand& queued) {
            return queued.type == EmulationCommandType::runAheadSettings;
          },
          std::move(command))) {
      ++coalescedRunAheadSettingsCommands_;
      wake_.notify_one();
      return success();
    }
    if (command.type == EmulationCommandType::speedSettings &&
        commands_.replaceNewestMatching(
          [](const EmulationCommand& queued) {
            return queued.type == EmulationCommandType::speedSettings;
          },
          std::move(command))) {
      ++coalescedSpeedSettingsCommands_;
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
        return shutdownStatus_;
      }
      acceptingCommands_ = false;
      stopRequested_.store(true, std::memory_order_release);
      owner_.state_.store(EmulationWorkerState::stopping, std::memory_order_release);
      wake_.notify_all();
      eventReady_.notify_all();
    }
    thread_.join();
    std::scoped_lock lock{mutex_};
    return shutdownStatus_;
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
    const auto rewind = rewindBufferMetrics_;
    return {
      .commandQueueDepth = commands_.size(),
      .commandQueueCapacity = commands_.capacity(),
      .eventQueueDepth = events_.size() + (latestFrame_.has_value() ? 1U : 0U),
      .eventQueueCapacity = events_.capacity() + 1U,
      .coalescedInputCommands = coalescedInputCommands_,
      .coalescedInputSettingsCommands = coalescedInputSettingsCommands_,
      .coalescedVideoSettingsCommands = coalescedVideoSettingsCommands_,
      .coalescedAudioSettingsCommands = coalescedAudioSettingsCommands_,
      .coalescedSystemSettingsCommands = coalescedSystemSettingsCommands_,
      .coalescedFirmwareSettingsCommands = coalescedFirmwareSettingsCommands_,
      .coalescedCheatCommands = coalescedCheatCommands_,
      .coalescedRewindSettingsCommands = coalescedRewindSettingsCommands_,
      .coalescedRunAheadSettingsCommands = coalescedRunAheadSettingsCommands_,
      .coalescedSpeedSettingsCommands = coalescedSpeedSettingsCommands_,
      .replacedFrameEvents = replacedFrameEvents_,
      .droppedOperationEvents = droppedOperationEvents_,
      .pacedFrameCount = pacingMetrics_.scheduledFrames,
      .lateFrameCount = pacingMetrics_.lateFrames,
      .pacingResynchronizations = pacingMetrics_.resynchronizations,
      .maximumLatenessMicroseconds =
        std::chrono::duration_cast<std::chrono::microseconds>(
          pacingMetrics_.maximumLateness).count(),
      .targetFramesPerSecond = pacingMetrics_.targetFramesPerSecond,
      .speedPercent = pacingMetrics_.speedPercent,
      .fastForward = fastForwardMetrics_,
      .slowMotion = slowMotionMetrics_,
      .rewinding = rewindingMetrics_,
      .rewindAvailable = rewindAvailableMetrics_,
      .rewindSnapshotCount = rewind.snapshotCount,
      .rewindPayloadBytes = rewind.payloadBytes,
      .rewindMemoryLimitBytes = rewind.memoryLimitBytes,
      .discardedRewindSnapshots = rewind.discardedSnapshots,
      .runAheadEnabled = runAheadEnabledMetrics_,
      .runAheadSupported = runAheadSupportedMetrics_,
      .runAheadActive = runAheadActiveMetrics_,
      .runAheadVerified = runAheadVerifiedMetrics_,
      .runAheadFrames = runAheadFramesMetrics_,
      .runAheadSpeculativeFrames = runAheadSpeculativeFramesMetrics_,
      .runAheadRollbacks = runAheadRollbacksMetrics_,
      .runAheadDeterminismFailures = runAheadDeterminismFailuresMetrics_,
      .runAheadStateBytes = runAheadStateBytesMetrics_,
      .runAheadStateCapacityBytes = runAheadStateCapacityBytesMetrics_,
    };
  }

  std::shared_ptr<VideoFrameExchange> videoFrames() const
  {
    return videoFrames_;
  }

  std::shared_ptr<StereoAudioRingBuffer> audioFrames() const
  {
    return audioFrames_;
  }

private:
  CoreResult loadBackupMemory(
    CoreAdapter& adapter,
    const std::filesystem::path& path)
  {
    if (!backupPersistence_) {
      return {};
    }
    const auto begun = backupPersistence_->beginGame(path, [this] {
      return stopRequested_.load(std::memory_order_acquire);
    });
    if (!begun) {
      return persistenceFailure(
        "Unable to establish per-game save storage: " + begun.message);
    }
    backupGameActive_ = true;
    for (const auto kind : backupMemoryKinds) {
      BackupMemoryInfo information;
      if (const auto described = adapter.backupMemoryInfo(kind, information);
          !described) {
        return described;
      }
      if (!information.available) {
        continue;
      }
      auto loaded = backupPersistence_->load(kind, information.size);
      if (!loaded.status) {
        return persistenceFailure(
          "Unable to load per-game backup memory: " + loaded.status.message);
      }
      if (loaded.exists) {
        if (const auto applied = adapter.loadBackupMemory(kind, loaded.data);
            !applied) {
          return applied;
        }
      } else if (const auto initialized = adapter.initializeBackupMemory(kind);
                 !initialized) {
        return initialized;
      }
    }
    return {};
  }

  void endBackupGame() noexcept
  {
    if (backupPersistence_ && backupGameActive_) {
      backupPersistence_->endGame();
      backupGameActive_ = false;
    }
  }

  CoreResult saveBackupMemory(CoreAdapter& adapter)
  {
    if (!backupPersistence_ || adapter.state() != CoreLifecycleState::loaded) {
      return {};
    }
    for (const auto kind : backupMemoryKinds) {
      BackupMemoryInfo information;
      if (const auto described = adapter.backupMemoryInfo(kind, information);
          !described) {
        return described;
      }
      if (!information.available) {
        continue;
      }
      backupScratch_.resize(information.size);
      if (const auto copied = adapter.copyBackupMemory(
            kind, backupScratch_, information);
          !copied) {
        return copied;
      }
      const auto saved = backupPersistence_->save(
        kind,
        std::span<const std::uint8_t>{backupScratch_}.first(information.size));
      if (!saved) {
        return persistenceFailure(
          "Unable to save per-game backup memory: " + saved.message);
      }
    }
    return {};
  }

  void threadMain()
  {
    const ScopedHostTimerResolution timerResolution;
    static_cast<void>(configureCurrentThreadForInteractiveTiming());
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

    pacer_ = FramePacer{};
    updateMetrics(adapter);
    owner_.state_.store(EmulationWorkerState::idle, std::memory_order_release);
    publishOperation(eventFor(
      EmulationEventType::workerStarted, EmulationWorkerState::idle));

    while (true) {
      std::optional<EmulationCommand> command;
      bool executeFrame = false;
      {
        std::unique_lock lock{mutex_};
        if (stopRequested_.load(std::memory_order_acquire)) {
          break;
        }
        if (!commands_.empty()) {
          command = commands_.pop();
        } else if (owner_.state_.load(std::memory_order_acquire) ==
                   EmulationWorkerState::running) {
          const auto deadline = pacer_.nextDeadline();
          if (!deadline) {
            owner_.state_.store(EmulationWorkerState::failed, std::memory_order_release);
            auto event = eventFor(
              EmulationEventType::commandFailed, EmulationWorkerState::failed);
            event.error = EmulationWorkerError::threadFailure;
            event.message = "Running emulation has no valid frame deadline.";
            lock.unlock();
            publishOperation(std::move(event));
            break;
          }
          if (Clock::now() < *deadline) {
            lock.unlock();
            sleepUntilHostDeadline(*deadline);
            lock.lock();
          }
          if (stopRequested_.load(std::memory_order_acquire)) {
            break;
          }
          if (!commands_.empty()) {
            command = commands_.pop();
          } else {
            executeFrame = true;
          }
        } else {
          wake_.wait(lock, [this] {
            return stopRequested_.load(std::memory_order_acquire) ||
              !commands_.empty();
          });
          if (stopRequested_.load(std::memory_order_acquire)) {
            break;
          }
          command = commands_.pop();
        }
      }

      if (command) {
        processCommand(adapter, std::move(*command));
        continue;
      }
      if (executeFrame) {
        runOneFrame(adapter);
      }
    }

    finishThread(adapter);
  }

  void finishThread(CoreAdapter& adapter)
  {
    pacer_.pause();
    updateMetrics(adapter);
    const auto persisted = saveBackupMemory(adapter);
    const auto shutdown = adapter.shutdown();
    endBackupGame();
    owner_.state_.store(EmulationWorkerState::stopped, std::memory_order_release);
    {
      std::scoped_lock lock{mutex_};
      acceptingCommands_ = false;
    }
    auto event = eventFor(
      EmulationEventType::workerStopped, EmulationWorkerState::stopped);
    const auto finalStatus = persisted ? shutdown : persisted;
    event.error = finalStatus ? EmulationWorkerError::none
                              : EmulationWorkerError::coreFailure;
    event.coreError = finalStatus.error;
    event.message = finalStatus.message;
    {
      std::scoped_lock lock{mutex_};
      shutdownStatus_ = finalStatus
        ? success()
        : failure(EmulationWorkerError::coreFailure, finalStatus.message);
    }
    publishOperation(std::move(event));
  }

  void processCommand(CoreAdapter& adapter, EmulationCommand command)
  {
    CoreResult coreResult;
    bool validTransition = true;
    EmulationEventType eventType = EmulationEventType::commandCompleted;
    std::vector<std::uint8_t> capturedState;
    CoreDebugResponse debugResponse;
    const auto current = owner_.state_.load(std::memory_order_acquire);

    switch (command.type) {
      case EmulationCommandType::loadGame: {
        frameBreakpoints_.clear();
        lastBreakpointHit_.reset();
        rewinding_ = false;
        fastForward_ = false;
        slowMotion_ = false;
        rewindBuffer_.clear();
        coreResult = saveBackupMemory(adapter);
        if (!coreResult) {
          break;
        }
        resetRunAheadSession(true);
        latestInput_ = {};
        hasLatestInput_ = false;
        discardLatestFrame();
        endBackupGame();
        coreResult = adapter.loadGame(command.path);
        if (!coreResult) {
          owner_.state_.store(EmulationWorkerState::idle, std::memory_order_release);
          fastForward_ = false;
          slowMotion_ = false;
          pacer_.pause();
          break;
        }
        coreResult = loadBackupMemory(adapter, command.path);
        if (coreResult) {
          coreResult = configurePacing(adapter, false);
        }
        if (coreResult) {
          coreResult = resetRewindHistory(adapter);
        }
        if (!coreResult) {
          static_cast<void>(adapter.unloadGame());
          endBackupGame();
          owner_.state_.store(EmulationWorkerState::idle, std::memory_order_release);
          fastForward_ = false;
          slowMotion_ = false;
          pacer_.pause();
          break;
        }
        fastForward_ = false;
        slowMotion_ = false;
        owner_.state_.store(
          EmulationWorkerState::paused, std::memory_order_release);
        break;
      }
      case EmulationCommandType::unloadGame:
        frameBreakpoints_.clear();
        lastBreakpointHit_.reset();
        rewinding_ = false;
        slowMotion_ = false;
        rewindBuffer_.clear();
        coreResult = saveBackupMemory(adapter);
        if (coreResult) {
          discardLatestFrame();
          coreResult = adapter.unloadGame();
        }
        if (coreResult) {
          endBackupGame();
          resetRunAheadSession(true);
          runAheadSupported_ = false;
          latestInput_ = {};
          hasLatestInput_ = false;
          owner_.state_.store(EmulationWorkerState::idle, std::memory_order_release);
          fastForward_ = false;
          slowMotion_ = false;
          pacer_.pause();
        }
        break;
      case EmulationCommandType::start:
      case EmulationCommandType::resume:
        validTransition = current == EmulationWorkerState::paused;
        if (validTransition) {
          lastBreakpointHit_.reset();
          pacer_.resume(Clock::now());
          owner_.state_.store(EmulationWorkerState::running, std::memory_order_release);
        }
        break;
      case EmulationCommandType::pause:
        validTransition = current == EmulationWorkerState::running;
        if (validTransition) {
          rewinding_ = false;
          pacer_.pause();
          owner_.state_.store(EmulationWorkerState::paused, std::memory_order_release);
        }
        break;
      case EmulationCommandType::hardReset:
        discardLatestFrame();
        rewinding_ = false;
        rewindBuffer_.clear();
        coreResult = adapter.reset();
        if (coreResult) {
          coreResult = configurePacing(
            adapter, current == EmulationWorkerState::running);
        }
        if (coreResult) {
          coreResult = resetRewindHistory(adapter);
        }
        break;
      case EmulationCommandType::softReset:
        discardLatestFrame();
        rewinding_ = false;
        rewindBuffer_.clear();
        coreResult = adapter.softReset();
        if (coreResult) {
          coreResult = configurePacing(
            adapter, current == EmulationWorkerState::running);
        }
        if (coreResult) {
          coreResult = resetRewindHistory(adapter);
        }
        break;
      case EmulationCommandType::frameAdvance:
        validTransition = current == EmulationWorkerState::paused;
        if (validTransition) {
          coreResult = executeOneFrame(adapter);
        }
        break;
      case EmulationCommandType::setFastForward:
        validTransition = current == EmulationWorkerState::paused ||
                          current == EmulationWorkerState::running;
        if (validTransition) {
          const auto previousFastForward = fastForward_;
          const auto previousSlowMotion = slowMotion_;
          const auto previousRewinding = rewinding_;
          if (command.enabled) {
            rewinding_ = false;
            slowMotion_ = false;
          }
          fastForward_ = command.enabled;
          if (applyConfiguredSpeed(Clock::now())) {
            audioFrames_->clear();
          } else {
            fastForward_ = previousFastForward;
            slowMotion_ = previousSlowMotion;
            rewinding_ = previousRewinding;
            coreResult = {
              CoreError::invalidTiming,
              "Fast-forward could not be applied to the current frame rate.",
            };
          }
        }
        break;
      case EmulationCommandType::setSlowMotion:
        validTransition = current == EmulationWorkerState::paused ||
                          current == EmulationWorkerState::running;
        if (validTransition) {
          const auto previousFastForward = fastForward_;
          const auto previousSlowMotion = slowMotion_;
          const auto previousRewinding = rewinding_;
          if (command.enabled) {
            rewinding_ = false;
            fastForward_ = false;
          }
          slowMotion_ = command.enabled;
          if (applyConfiguredSpeed(Clock::now())) {
            audioFrames_->clear();
          } else {
            fastForward_ = previousFastForward;
            slowMotion_ = previousSlowMotion;
            rewinding_ = previousRewinding;
            coreResult = {
              CoreError::invalidTiming,
              "Slow motion could not be applied to the current frame rate.",
            };
          }
        }
        break;
      case EmulationCommandType::speedSettings: {
        if (!validateEmulationSpeedConfiguration(command.speedConfiguration)) {
          coreResult = {
            CoreError::invalidTiming,
            "The emulation speed settings are outside their safe limits.",
          };
          break;
        }
        const auto previous = speedConfiguration_;
        speedConfiguration_ = command.speedConfiguration;
        if (adapter.state() == CoreLifecycleState::loaded &&
            !applyConfiguredSpeed(Clock::now())) {
          speedConfiguration_ = previous;
          coreResult = {
            CoreError::invalidTiming,
            "The emulation speed settings cannot represent this core frame rate.",
          };
        } else {
          audioFrames_->clear();
        }
        break;
      }
      case EmulationCommandType::setRewinding:
        validTransition = current == EmulationWorkerState::running ||
          (!command.enabled && current == EmulationWorkerState::paused);
        if (validTransition && command.enabled &&
            (!rewindBuffer_.configuration().enabled ||
             !rewindBuffer_.canRewind(adapter.frameCount()))) {
          coreResult = {
            CoreError::invalidStatePayload,
            rewindBuffer_.configuration().enabled
              ? "No earlier rewind snapshot is available yet."
              : "Rewind is disabled in the current settings.",
          };
        } else if (validTransition) {
          const auto previousFastForward = fastForward_;
          const auto previousSlowMotion = slowMotion_;
          const auto previousRewinding = rewinding_;
          rewinding_ = command.enabled;
          if (rewinding_) {
            fastForward_ = false;
            slowMotion_ = false;
            audioFrames_->clear();
          }
          if (!applyConfiguredSpeed(Clock::now())) {
            coreResult = {
              CoreError::invalidTiming,
              "Rewind could not restore normal frame pacing.",
            };
            fastForward_ = previousFastForward;
            slowMotion_ = previousSlowMotion;
            rewinding_ = previousRewinding;
          }
        }
        break;
      case EmulationCommandType::rewindSettings:
        if (!rewindBuffer_.configure(command.rewindConfiguration)) {
          coreResult = {
            CoreError::invalidStatePayload,
            "The rewind settings are outside their safe limits.",
          };
          break;
        }
        rewinding_ = false;
        if (adapter.state() == CoreLifecycleState::loaded) {
          coreResult = resetRewindHistory(adapter);
        }
        break;
      case EmulationCommandType::runAheadSettings:
        if (!validateRunAheadConfiguration(command.runAheadConfiguration)) {
          coreResult = {
            CoreError::invalidStatePayload,
            "The run-ahead settings are outside their safe limits.",
          };
          break;
        }
        runAheadConfiguration_ = command.runAheadConfiguration;
        resetRunAheadSession(true);
        audioFrames_->clear();
        break;
      case EmulationCommandType::inputSnapshot:
        coreResult = adapter.setInputSnapshot(command.input);
        if (coreResult) {
          latestInput_ = command.input;
          hasLatestInput_ = true;
        }
        break;
      case EmulationCommandType::inputSettings:
        coreResult = adapter.applyInputSettings(command.coreInputSettings);
        if (coreResult && adapter.state() == CoreLifecycleState::loaded &&
            hasLatestInput_) {
          coreResult = adapter.setInputSnapshot(latestInput_);
        }
        if (coreResult && adapter.state() == CoreLifecycleState::loaded) {
          coreResult = updateRunAheadSupport(adapter);
        }
        if (coreResult && adapter.state() == CoreLifecycleState::loaded) {
          coreResult = resetRewindHistory(adapter);
        }
        break;
      case EmulationCommandType::videoSettings:
        discardLatestFrame();
        coreResult = adapter.applyVideoSettings(command.coreVideoSettings);
        if (coreResult && adapter.state() == CoreLifecycleState::loaded) {
          coreResult = resetRewindHistory(adapter);
        }
        break;
      case EmulationCommandType::audioSettings:
        audioFrames_->clear();
        coreResult = adapter.applyAudioSettings(command.coreAudioSettings);
        if (coreResult && adapter.state() == CoreLifecycleState::loaded) {
          coreResult = resetRewindHistory(adapter);
        }
        break;
      case EmulationCommandType::systemSettings:
        coreResult = adapter.applySystemSettings(command.coreSystemSettings);
        if (coreResult && adapter.state() == CoreLifecycleState::loaded) {
          coreResult = resetRewindHistory(adapter);
        }
        break;
      case EmulationCommandType::firmwareSettings:
        coreResult = adapter.applyFirmwareSettings(command.coreFirmwareSettings);
        if (coreResult && adapter.state() == CoreLifecycleState::loaded) {
          coreResult = resetRewindHistory(adapter);
        }
        break;
      case EmulationCommandType::cheats:
        coreResult = adapter.applyCheats(command.coreCheats);
        if (coreResult && adapter.state() == CoreLifecycleState::loaded) {
          coreResult = resetRewindHistory(adapter);
        }
        break;
      case EmulationCommandType::setDiscEjected:
        validTransition = current == EmulationWorkerState::paused ||
                          current == EmulationWorkerState::running;
        if (validTransition) {
          audioFrames_->clear();
          coreResult = adapter.setDiscEjected(command.enabled);
          if (coreResult) {
            coreResult = resetRewindHistory(adapter);
          }
        }
        break;
      case EmulationCommandType::changeDisc:
        validTransition = current == EmulationWorkerState::paused ||
                          current == EmulationWorkerState::running;
        if (validTransition) {
          audioFrames_->clear();
          coreResult = adapter.changeDisc(command.path);
          if (coreResult) {
            coreResult = resetRewindHistory(adapter);
          }
        }
        break;
      case EmulationCommandType::captureState:
        coreResult = adapter.saveRawState(capturedState);
        if (coreResult) {
          eventType = EmulationEventType::stateCaptured;
        }
        break;
      case EmulationCommandType::restoreState:
        discardLatestFrame();
        rewinding_ = false;
        rewindBuffer_.clear();
        coreResult = adapter.loadRawState(command.rawState);
        if (coreResult && hasLatestInput_) {
          coreResult = adapter.setInputSnapshot(latestInput_);
        }
        if (coreResult) {
          coreResult = configurePacing(
            adapter, current == EmulationWorkerState::running);
        }
        if (coreResult) {
          coreResult = resetRewindHistory(adapter);
        }
        break;
      case EmulationCommandType::debugRequest: {
        const auto requestType = command.coreDebugRequest.type;
        const auto writesCore = requestType !=
            CoreDebugRequestType::captureSnapshot &&
          requestType != CoreDebugRequestType::readMemory &&
          requestType != CoreDebugRequestType::setFrameBreakpoints;
        validTransition = current == EmulationWorkerState::paused ||
          (!writesCore && current == EmulationWorkerState::running);
        if (validTransition) {
          if (requestType == CoreDebugRequestType::setFrameBreakpoints) {
            coreResult = setFrameBreakpoints(
              command.coreDebugRequest.breakpoints, debugResponse);
          } else {
            coreResult = adapter.debugRequest(
              command.coreDebugRequest, debugResponse);
          }
          if (coreResult && writesCore &&
              adapter.state() == CoreLifecycleState::loaded) {
            coreResult = resetRewindHistory(adapter);
          }
          if (coreResult) {
            eventType = EmulationEventType::debugResponse;
          }
        }
        break;
      }
    }

    updateMetrics(adapter);

    if (!validTransition) {
      auto event = eventFor(EmulationEventType::commandFailed, current);
      event.command = command.type;
      event.operationId = command.operationId;
      event.error = EmulationWorkerError::invalidTransition;
      event.message = "The command is invalid in the current emulation state.";
      event.frameNumber = adapter.frameCount();
      event.hardware = adapter.hardware();
      event.appliedInputSequence = adapter.appliedInputSequence();
      event.fastForward = fastForward_;
      event.slowMotion = slowMotion_;
      event.speedPercent = pacer_.speedPercent();
      event.rewinding = rewinding_;
      event.rewindAvailable = rewindBuffer_.canRewind(adapter.frameCount());
      populateRunAheadEvent(event, adapter);
      if (adapter.state() == CoreLifecycleState::loaded) {
        static_cast<void>(adapter.discInfo(event.disc));
      }
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
      event.hardware = adapter.hardware();
      event.appliedInputSequence = adapter.appliedInputSequence();
      event.fastForward = fastForward_;
      event.slowMotion = slowMotion_;
      event.speedPercent = pacer_.speedPercent();
      event.rewinding = rewinding_;
      event.rewindAvailable = rewindBuffer_.canRewind(adapter.frameCount());
      populateRunAheadEvent(event, adapter);
      if (adapter.state() == CoreLifecycleState::loaded) {
        static_cast<void>(adapter.discInfo(event.disc));
      }
      publishOperation(std::move(event));
      return;
    }

    auto event = eventFor(
      eventType, owner_.state_.load(std::memory_order_acquire));
    event.command = command.type;
    event.operationId = command.operationId;
    event.frameNumber = adapter.frameCount();
    event.hardware = adapter.hardware();
    event.videoGeneration = videoFrames_->metrics().publishedFrames;
    event.appliedInputSequence = adapter.appliedInputSequence();
    event.fastForward = fastForward_;
    event.slowMotion = slowMotion_;
    event.speedPercent = pacer_.speedPercent();
    event.rewinding = rewinding_;
    event.rewindAvailable = rewindBuffer_.canRewind(adapter.frameCount());
    populateRunAheadEvent(event, adapter);
    event.rawState = std::move(capturedState);
    event.debug = std::move(debugResponse);
    if (adapter.state() == CoreLifecycleState::loaded) {
      static_cast<void>(adapter.discInfo(event.disc));
    }
    publishOperation(std::move(event));
  }

  void runOneFrame(CoreAdapter& adapter)
  {
    const auto result = executeOneFrame(adapter);
    if (!result) {
      owner_.state_.store(EmulationWorkerState::paused, std::memory_order_release);
      pacer_.pause();
      rewinding_ = false;
      updateMetrics(adapter);
      auto event = eventFor(
        EmulationEventType::commandFailed, EmulationWorkerState::paused);
      event.error = EmulationWorkerError::coreFailure;
      event.coreError = result.error;
      event.message = result.message;
      event.frameNumber = adapter.frameCount();
      event.hardware = adapter.hardware();
      publishOperation(std::move(event));
      return;
    }

    pacer_.frameExecuted(Clock::now());
    auto frameState = EmulationWorkerState::running;
    if (!rewinding_) {
      if (const auto breakpoint = matchingFrameBreakpoint(adapter)) {
        frameState = EmulationWorkerState::paused;
        owner_.state_.store(frameState, std::memory_order_release);
        pacer_.pause();
        audioFrames_->clear();
        lastBreakpointHit_ = breakpoint;
        auto event = eventFor(EmulationEventType::debugBreakpointHit, frameState);
        event.command = EmulationCommandType::debugRequest;
        event.frameNumber = adapter.frameCount();
        event.hardware = adapter.hardware();
        event.videoGeneration = videoFrames_->metrics().publishedFrames;
        event.appliedInputSequence = adapter.appliedInputSequence();
        event.fastForward = fastForward_;
        event.slowMotion = slowMotion_;
        event.speedPercent = pacer_.speedPercent();
        event.rewinding = rewinding_;
        event.rewindAvailable = rewindBuffer_.canRewind(adapter.frameCount());
        populateRunAheadEvent(event, adapter);
        event.debug.type = CoreDebugRequestType::setFrameBreakpoints;
        event.debug.breakpointHit = breakpoint;
        publishOperation(std::move(event));
      }
    }
    updateMetrics(adapter);

    std::scoped_lock lock{mutex_};
    if (latestFrame_) {
      ++replacedFrameEvents_;
    }
    auto event = eventFor(
      EmulationEventType::frameCompleted, frameState);
    event.frameNumber = adapter.frameCount();
    event.hardware = adapter.hardware();
    event.videoGeneration = videoFrames_->metrics().publishedFrames;
    event.appliedInputSequence = adapter.appliedInputSequence();
    event.fastForward = fastForward_;
    event.slowMotion = slowMotion_;
    event.speedPercent = pacer_.speedPercent();
    event.rewinding = rewinding_;
    event.rewindAvailable = rewindBuffer_.canRewind(adapter.frameCount());
    populateRunAheadEvent(event, adapter);
    latestFrame_ = std::move(event);
    eventReady_.notify_all();
  }

  CoreResult setFrameBreakpoints(
    const std::vector<CoreDebugBreakpoint>& requested,
    CoreDebugResponse& response)
  {
    if (requested.size() > maximumCoreDebugBreakpoints) {
      return {
        CoreError::invalidDebugRequest,
        "At most 64 frame-boundary breakpoints may be active.",
      };
    }
    auto breakpoints = requested;
    for (const auto& breakpoint : breakpoints) {
      const auto maximum = breakpoint.cpu == CoreDebugCpu::m68k
        ? 0x00FF'FFFFU : 0x0000'FFFFU;
      if (breakpoint.address > maximum) {
        return {
          CoreError::invalidDebugRequest,
          "A frame-boundary breakpoint address is outside its CPU address range.",
        };
      }
    }
    std::ranges::sort(breakpoints, [](const auto& left, const auto& right) {
      if (left.cpu != right.cpu) {
        return left.cpu < right.cpu;
      }
      return left.address < right.address;
    });
    breakpoints.erase(std::unique(breakpoints.begin(), breakpoints.end()),
      breakpoints.end());
    frameBreakpoints_ = std::move(breakpoints);
    lastBreakpointHit_.reset();
    response = {};
    response.type = CoreDebugRequestType::setFrameBreakpoints;
    return {};
  }

  std::optional<CoreDebugBreakpoint> matchingFrameBreakpoint(CoreAdapter& adapter)
  {
    if (frameBreakpoints_.empty()) {
      return std::nullopt;
    }
    CoreDebugProgramCounters counters;
    if (!adapter.debugProgramCounters(counters)) {
      return std::nullopt;
    }
    const auto found = std::ranges::find_if(
      frameBreakpoints_, [&counters](const auto& breakpoint) {
        if (breakpoint.cpu == CoreDebugCpu::m68k) {
          return counters.m68kActive &&
            breakpoint.address == (counters.m68k & 0x00FF'FFFFU);
        }
        return breakpoint.address == counters.z80;
      });
    if (found == frameBreakpoints_.end() ||
        (lastBreakpointHit_ && *lastBreakpointHit_ == *found)) {
      return std::nullopt;
    }
    return *found;
  }

  CoreResult executeOneFrame(CoreAdapter& adapter)
  {
    if (rewinding_) {
      const auto currentFrame = adapter.frameCount();
      const auto latestUsableState = currentFrame > 0U
        ? currentFrame - 1U : 0U;
      auto snapshot = rewindBuffer_.takePrevious(latestUsableState);
      if (!snapshot) {
        return {};
      }
      audioFrames_->clear();
      if (const auto restored = adapter.loadRawState(
            snapshot->rawState, snapshot->frameNumber); !restored) {
        return restored;
      }
      return executeCoreFrame(adapter, false);
    }

    if (runAheadActive(adapter)) {
      const auto result = executeRunAheadFrame(adapter);
      if (!result) {
        return result;
      }
    } else {
      const auto result = executeCoreFrame(adapter, true);
      if (!result) {
        return result;
      }
    }

    if (!rewindBuffer_.shouldCapture(adapter.frameCount())) {
      return {};
    }
    std::vector<std::uint8_t> state;
    if (const auto captured = adapter.saveRawState(state); !captured) {
      return captured;
    }
    if (!rewindBuffer_.capture(adapter.frameCount(), std::move(state))) {
      return {
        CoreError::stateSaveFailed,
        "The bounded rewind history rejected a core snapshot.",
      };
    }
    return {};
  }

  bool runAheadActive(const CoreAdapter& adapter) const noexcept
  {
    return adapter.state() == CoreLifecycleState::loaded &&
      runAheadConfiguration_.enabled && runAheadSupported_ &&
      !runAheadDeterminism_.faulted() && !fastForward_ && !slowMotion_ &&
      !rewinding_;
  }

  CoreResult transferCoreAudio(
    CoreAdapter& adapter,
    bool writeHostAudio,
    CoreAudioBatchInfo& audioInfo)
  {
    audioInfo = {};
    const auto describedAudio = adapter.audioBatchInfo(audioInfo);
    if (!describedAudio) {
      return describedAudio.error == CoreError::noAudioAvailable
        ? CoreResult{} : describedAudio;
    }
    if (audioInfo.frameCount > audioScratch_.size()) {
      return {
        CoreError::invalidAudioBatch,
        "The core audio batch exceeds the worker's fixed transfer buffer.",
      };
    }
    const auto copiedAudio = adapter.copyAudioFrames(
      std::span<StereoAudioFrame>{audioScratch_}.first(audioInfo.frameCount),
      audioInfo);
    if (!copiedAudio) {
      return copiedAudio;
    }
    if (writeHostAudio && pacer_.speedPercent() == 100U) {
      static_cast<void>(audioFrames_->write(
        std::span<const StereoAudioFrame>{audioScratch_}.first(
          audioInfo.frameCount)));
    }
    return {};
  }

  CoreResult publishPreparedFrame(
    CoreVideoFrameInfo frame,
    std::span<const std::uint16_t> pixels,
    const CoreAudioBatchInfo& audioInfo)
  {
    auto write = videoFrames_->beginWrite();
    if (!write) {
      return {};
    }
    if (pixels.size() < frame.pixelCount() ||
        write->pixels().size() < frame.pixelCount()) {
      return {
        CoreError::videoBufferTooSmall,
        "The run-ahead video transfer buffer is too small.",
      };
    }
    std::ranges::copy(
      pixels.first(frame.pixelCount()), write->pixels().begin());
    if (captureSink_ && captureSink_->active()) {
      const auto captureAudio = audioInfo.frameCount > 0U
        ? std::span<const StereoAudioFrame>{audioScratch_}.first(
            audioInfo.frameCount)
        : std::span<const StereoAudioFrame>{};
      static_cast<void>(captureSink_->submitFrame(
        frame, pixels.first(frame.pixelCount()), audioInfo, captureAudio));
    }
    if (!write->publish(frame)) {
      return {
        CoreError::invalidVideoFrame,
        "The bounded video exchange rejected a run-ahead frame.",
      };
    }
    return {};
  }

  CoreResult restoreRunAheadRollback(CoreAdapter& adapter)
  {
    auto restored = adapter.restoreRollbackState(runAheadRollbackState_);
    if (!restored) {
      return restored;
    }
    ++runAheadRollbacks_;
    if (hasLatestInput_) {
      restored = adapter.setInputSnapshot(latestInput_);
    }
    return restored;
  }

  CoreResult executeRunAheadFrame(CoreAdapter& adapter)
  {
    if (const auto saved = adapter.saveRollbackState(runAheadRollbackState_);
        !saved) {
      return saved;
    }

    CoreVideoFrameInfo speculativeVideo;
    for (std::uint32_t index = 0U;
         index < runAheadConfiguration_.frames;
         ++index) {
      if (const auto executed = adapter.runFrame(false); !executed) {
        const auto restored = restoreRunAheadRollback(adapter);
        return restored ? executed : restored;
      }
      ++runAheadSpeculativeFrames_;
      CoreAudioBatchInfo discardedAudio;
      if (const auto drained = transferCoreAudio(
            adapter, false, discardedAudio); !drained) {
        const auto restored = restoreRunAheadRollback(adapter);
        return restored ? drained : restored;
      }
      if (index == 0U && runAheadDeterminism_.pending()) {
        if (const auto saved = adapter.saveRawState(runAheadFirstFrameState_);
            !saved) {
          const auto restored = restoreRunAheadRollback(adapter);
          return restored ? saved : restored;
        }
      }
    }

    if (const auto copied = adapter.copyVideoFrame(
          runAheadVideoScratch_, speculativeVideo); !copied) {
      const auto restored = restoreRunAheadRollback(adapter);
      return restored ? copied : restored;
    }
    if (const auto restored = restoreRunAheadRollback(adapter); !restored) {
      return restored;
    }

    const bool verifying = runAheadDeterminism_.pending();
    if (const auto executed = adapter.runFrame(false); !executed) {
      return executed;
    }
    CoreAudioBatchInfo authoritativeAudio;
    if (const auto transferred = transferCoreAudio(
          adapter, true, authoritativeAudio); !transferred) {
      return transferred;
    }

    if (verifying) {
      if (const auto saved = adapter.saveRawState(runAheadCanonicalState_);
          !saved) {
        return saved;
      }
      const auto verification = runAheadDeterminism_.verify(
        runAheadFirstFrameState_, runAheadCanonicalState_);
      if (verification == RunAheadVerificationResult::mismatch) {
        if (const auto copied = adapter.copyVideoFrame(
              runAheadVideoScratch_, speculativeVideo); !copied) {
          return copied;
        }
        auto event = eventFor(
          EmulationEventType::runAheadDisabled,
          owner_.state_.load(std::memory_order_acquire));
        event.message = "Run-ahead was suspended because speculative and "
          "authoritative core states were not deterministic.";
        event.frameNumber = adapter.frameCount();
        event.hardware = adapter.hardware();
        populateRunAheadEvent(event, adapter);
        publishOperation(std::move(event));
      }
    }

    speculativeVideo.frameNumber = adapter.frameCount();
    return publishPreparedFrame(
      speculativeVideo,
      std::span<const std::uint16_t>{runAheadVideoScratch_}.first(
        speculativeVideo.pixelCount()),
      authoritativeAudio);
  }

  CoreResult executeCoreFrame(CoreAdapter& adapter, bool writeAudio)
  {
    const auto result = adapter.runFrame(false);
    if (!result) {
      return result;
    }

    CoreAudioBatchInfo audioInfo;
    if (const auto transferred = transferCoreAudio(
          adapter, writeAudio, audioInfo); !transferred) {
      return transferred;
    }

    auto write = videoFrames_->beginWrite();
    if (!write) {
      return {};
    }
    CoreVideoFrameInfo frame;
    const auto copied = adapter.copyVideoFrame(write->pixels(), frame);
    if (!copied) {
      return copied;
    }
    if (captureSink_ && captureSink_->active()) {
      if (audioInfo.frameCount > 0U && !writeAudio) {
        std::ranges::fill(
          std::span<StereoAudioFrame>{audioScratch_}.first(
            audioInfo.frameCount),
          StereoAudioFrame{});
      }
      const auto captureAudio = audioInfo.frameCount > 0U
        ? std::span<const StereoAudioFrame>{audioScratch_}.first(
            audioInfo.frameCount)
        : std::span<const StereoAudioFrame>{};
      static_cast<void>(captureSink_->submitFrame(
        frame,
        std::span<const std::uint16_t>{write->pixels()}.first(
          frame.pixelCount()),
        audioInfo,
        captureAudio));
    }
    if (!write->publish(frame)) {
      return {
        CoreError::invalidVideoFrame,
        "The bounded video exchange rejected a core frame.",
      };
    }
    return {};
  }

  CoreResult resetRewindHistory(CoreAdapter& adapter)
  {
    rewindBuffer_.clear();
    resetRunAheadSession(true);
    if (!rewindBuffer_.configuration().enabled ||
        adapter.state() != CoreLifecycleState::loaded) {
      return {};
    }
    std::vector<std::uint8_t> state;
    if (const auto saved = adapter.saveRawState(state); !saved) {
      return saved;
    }
    if (!rewindBuffer_.capture(adapter.frameCount(), std::move(state))) {
      return {
        CoreError::stateSaveFailed,
        "The bounded rewind history rejected its initial core snapshot.",
      };
    }
    return {};
  }

  CoreResult configurePacing(CoreAdapter& adapter, bool resume)
  {
    CoreTimingInfo timing;
    if (const auto result = adapter.timingInfo(timing); !result) {
      return result;
    }
    if (const auto support = updateRunAheadSupport(adapter); !support) {
      return support;
    }
    const FrameRateRatio rate{
      .framesNumerator = timing.masterClockHz,
      .framesDenominator = timing.masterCyclesPerFrame(),
    };
    if (!pacer_.configure(rate) || !applyConfiguredSpeed(Clock::now())) {
      return {
        CoreError::invalidTiming,
        "The core frame rate cannot be represented by the frontend scheduler.",
      };
    }
    if (resume) {
      pacer_.resume(Clock::now());
    }
    return {};
  }

  CoreResult updateRunAheadSupport(CoreAdapter& adapter)
  {
    CoreInputSettings inputSettings;
    if (const auto result = adapter.inputSettings(inputSettings); !result) {
      return result;
    }
    runAheadSupported_ = runAheadSupportedForHardware(adapter.hardware()) &&
      runAheadSupportedForInputSettings(inputSettings);
    return {};
  }

  bool applyConfiguredSpeed(Clock::time_point now) noexcept
  {
    const auto mode = fastForward_
      ? EmulationSpeedMode::fastForward
      : slowMotion_ ? EmulationSpeedMode::slowMotion
                    : EmulationSpeedMode::normal;
    return pacer_.setSpeed(
      mode, speedPercentForMode(speedConfiguration_, mode), now);
  }

  void updateMetrics(CoreAdapter& adapter)
  {
    std::scoped_lock lock{mutex_};
    pacingMetrics_ = pacer_.metrics();
    fastForwardMetrics_ = fastForward_;
    slowMotionMetrics_ = slowMotion_;
    rewindBufferMetrics_ = rewindBuffer_.metrics();
    rewindingMetrics_ = rewinding_;
    rewindAvailableMetrics_ = rewindBuffer_.canRewind(adapter.frameCount());
    runAheadEnabledMetrics_ = runAheadConfiguration_.enabled;
    runAheadSupportedMetrics_ = runAheadSupported_;
    runAheadActiveMetrics_ = runAheadActive(adapter);
    runAheadVerifiedMetrics_ = runAheadDeterminism_.verified();
    runAheadFramesMetrics_ = runAheadConfiguration_.frames;
    runAheadSpeculativeFramesMetrics_ = runAheadSpeculativeFrames_;
    runAheadRollbacksMetrics_ = runAheadRollbacks_;
    runAheadDeterminismFailuresMetrics_ = runAheadDeterminism_.failures();
    runAheadStateBytesMetrics_ = runAheadRollbackState_.rawState.size() +
      runAheadRollbackState_.transientSystemState.size() +
      runAheadFirstFrameState_.size() + runAheadCanonicalState_.size();
    runAheadStateCapacityBytesMetrics_ =
      runAheadRollbackState_.rawState.capacity() +
      runAheadRollbackState_.transientSystemState.capacity() +
      runAheadFirstFrameState_.capacity() + runAheadCanonicalState_.capacity();
  }

  void resetRunAheadSession(bool preserveCounters) noexcept
  {
    runAheadDeterminism_.reset(
      runAheadConfiguration_.enabled, preserveCounters);
    runAheadRollbackState_.rawState.clear();
    runAheadRollbackState_.transientSystemState.clear();
    runAheadFirstFrameState_.clear();
    runAheadCanonicalState_.clear();
    if (!preserveCounters) {
      runAheadSpeculativeFrames_ = 0U;
      runAheadRollbacks_ = 0U;
    }
  }

  void populateRunAheadEvent(
    EmulationEvent& event,
    const CoreAdapter& adapter) const noexcept
  {
    event.runAheadEnabled = runAheadConfiguration_.enabled;
    event.runAheadSupported = runAheadSupported_;
    event.runAheadActive = runAheadActive(adapter);
    event.runAheadVerified = runAheadDeterminism_.verified();
    event.runAheadFrames = runAheadConfiguration_.frames;
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
    videoFrames_->clear();
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
  std::atomic_bool stopRequested_{false};
  bool fastForward_{false};
  bool slowMotion_{false};
  bool rewinding_{false};
  RunAheadConfiguration runAheadConfiguration_;
  bool runAheadSupported_{false};
  RunAheadDeterminismGuard runAheadDeterminism_;
  EmulationWorkerStatus shutdownStatus_;
  std::shared_ptr<VideoFrameExchange> videoFrames_;
  std::shared_ptr<StereoAudioRingBuffer> audioFrames_;
  std::shared_ptr<BackupMemoryPersistence> backupPersistence_;
  std::shared_ptr<EmulationCaptureSink> captureSink_;
  bool backupGameActive_{false};
  std::array<StereoAudioFrame, maximumAudioFramesPerBatch> audioScratch_{};
  std::array<std::uint16_t, maximumCoreSurfacePixels> runAheadVideoScratch_{};
  CoreRollbackState runAheadRollbackState_;
  std::vector<std::uint8_t> runAheadFirstFrameState_;
  std::vector<std::uint8_t> runAheadCanonicalState_;
  InputSnapshot latestInput_;
  bool hasLatestInput_{false};
  std::vector<std::uint8_t> backupScratch_;
  std::vector<CoreDebugBreakpoint> frameBreakpoints_;
  std::optional<CoreDebugBreakpoint> lastBreakpointHit_;
  RewindBuffer rewindBuffer_;
  EmulationSpeedConfiguration speedConfiguration_;
  RewindBufferMetrics rewindBufferMetrics_;
  bool fastForwardMetrics_{false};
  bool slowMotionMetrics_{false};
  bool rewindingMetrics_{false};
  bool rewindAvailableMetrics_{false};
  bool runAheadEnabledMetrics_{false};
  bool runAheadSupportedMetrics_{false};
  bool runAheadActiveMetrics_{false};
  bool runAheadVerifiedMetrics_{false};
  std::uint32_t runAheadFramesMetrics_{1U};
  std::uint64_t runAheadSpeculativeFrames_{0U};
  std::uint64_t runAheadRollbacks_{0U};
  std::uint64_t runAheadSpeculativeFramesMetrics_{0U};
  std::uint64_t runAheadRollbacksMetrics_{0U};
  std::uint64_t runAheadDeterminismFailuresMetrics_{0U};
  std::size_t runAheadStateBytesMetrics_{0U};
  std::size_t runAheadStateCapacityBytesMetrics_{0U};
  FramePacer pacer_;
  FramePacerMetrics pacingMetrics_;
  std::uint64_t coalescedInputCommands_{0};
  std::uint64_t coalescedInputSettingsCommands_{0};
  std::uint64_t coalescedVideoSettingsCommands_{0};
  std::uint64_t coalescedAudioSettingsCommands_{0};
  std::uint64_t coalescedSystemSettingsCommands_{0};
  std::uint64_t coalescedFirmwareSettingsCommands_{0};
  std::uint64_t coalescedCheatCommands_{0};
  std::uint64_t coalescedRewindSettingsCommands_{0};
  std::uint64_t coalescedRunAheadSettingsCommands_{0};
  std::uint64_t coalescedSpeedSettingsCommands_{0};
  std::uint64_t replacedFrameEvents_{0};
  std::uint64_t droppedOperationEvents_{0};
};

EmulationWorker::EmulationWorker(
  std::size_t commandCapacity,
  std::size_t eventCapacity,
  int audioSampleRate,
  std::shared_ptr<VideoFrameExchange> videoFrames,
  std::shared_ptr<StereoAudioRingBuffer> audioFrames,
  std::shared_ptr<BackupMemoryPersistence> backupPersistence,
  std::shared_ptr<EmulationCaptureSink> captureSink)
  : private_(std::make_unique<Private>(
      *this,
      commandCapacity,
      eventCapacity,
      audioSampleRate,
      std::move(videoFrames),
      std::move(audioFrames),
      std::move(backupPersistence),
      std::move(captureSink)))
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

std::shared_ptr<VideoFrameExchange> EmulationWorker::videoFrames() const
{
  return private_->videoFrames();
}

std::shared_ptr<StereoAudioRingBuffer> EmulationWorker::audioFrames() const
{
  return private_->audioFrames();
}

} // namespace genplusgx
