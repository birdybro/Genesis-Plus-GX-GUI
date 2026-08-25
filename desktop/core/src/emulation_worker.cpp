#include "genplusgx/emulation_worker.h"

#include "genplusgx/bounded_queue.h"

#include <array>
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

EmulationCommand EmulationCommand::updateInput(
  std::uint64_t operationId,
  InputSnapshot input)
{
  auto command = simple(EmulationCommandType::inputSnapshot, operationId);
  command.input = std::move(input);
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

class EmulationWorker::Private final {
public:
  Private(
    EmulationWorker& owner,
    std::size_t commandCapacity,
    std::size_t eventCapacity,
    int audioSampleRate,
    std::shared_ptr<VideoFrameExchange> videoFrames,
    std::shared_ptr<StereoAudioRingBuffer> audioFrames,
    std::shared_ptr<BackupMemoryPersistence> backupPersistence)
    : owner_(owner),
      commands_(commandCapacity),
      events_(eventCapacity),
      audioSampleRate_(audioSampleRate),
      videoFrames_(videoFrames ? std::move(videoFrames)
                               : std::make_shared<VideoFrameExchange>()),
      audioFrames_(audioFrames ? std::move(audioFrames)
                               : std::make_shared<StereoAudioRingBuffer>(
                                   defaultAudioRingFrames)),
      backupPersistence_(std::move(backupPersistence))
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
    shutdownStatus_ = success();
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
      stopRequested_ = true;
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
    return {
      .commandQueueDepth = commands_.size(),
      .commandQueueCapacity = commands_.capacity(),
      .eventQueueDepth = events_.size() + (latestFrame_.has_value() ? 1U : 0U),
      .eventQueueCapacity = events_.capacity() + 1U,
      .coalescedInputCommands = coalescedInputCommands_,
      .coalescedVideoSettingsCommands = coalescedVideoSettingsCommands_,
      .coalescedAudioSettingsCommands = coalescedAudioSettingsCommands_,
      .coalescedSystemSettingsCommands = coalescedSystemSettingsCommands_,
      .coalescedFirmwareSettingsCommands = coalescedFirmwareSettingsCommands_,
      .coalescedCheatCommands = coalescedCheatCommands_,
      .replacedFrameEvents = replacedFrameEvents_,
      .droppedOperationEvents = droppedOperationEvents_,
      .pacedFrameCount = pacingMetrics_.scheduledFrames,
      .lateFrameCount = pacingMetrics_.lateFrames,
      .pacingResynchronizations = pacingMetrics_.resynchronizations,
      .maximumLatenessMicroseconds =
        std::chrono::duration_cast<std::chrono::microseconds>(
          pacingMetrics_.maximumLateness).count(),
      .targetFramesPerSecond = pacingMetrics_.targetFramesPerSecond,
      .fastForward = pacingMetrics_.fastForward,
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
    const auto begun = backupPersistence_->beginGame(path);
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
    updatePacingMetrics();
    owner_.state_.store(EmulationWorkerState::idle, std::memory_order_release);
    publishOperation(eventFor(
      EmulationEventType::workerStarted, EmulationWorkerState::idle));

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
          const bool interrupted = wake_.wait_until(lock, *deadline, [this] {
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
    updatePacingMetrics();
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
    const auto current = owner_.state_.load(std::memory_order_acquire);

    switch (command.type) {
      case EmulationCommandType::loadGame: {
        coreResult = saveBackupMemory(adapter);
        if (!coreResult) {
          break;
        }
        discardLatestFrame();
        endBackupGame();
        coreResult = adapter.loadGame(command.path);
        if (!coreResult) {
          owner_.state_.store(EmulationWorkerState::idle, std::memory_order_release);
          fastForward_ = false;
          pacer_.pause();
          break;
        }
        coreResult = loadBackupMemory(adapter, command.path);
        if (coreResult) {
          coreResult = configurePacing(adapter, false);
        }
        if (!coreResult) {
          static_cast<void>(adapter.unloadGame());
          endBackupGame();
          owner_.state_.store(EmulationWorkerState::idle, std::memory_order_release);
          fastForward_ = false;
          pacer_.pause();
          break;
        }
        fastForward_ = false;
        owner_.state_.store(
          EmulationWorkerState::paused, std::memory_order_release);
        break;
      }
      case EmulationCommandType::unloadGame:
        coreResult = saveBackupMemory(adapter);
        if (coreResult) {
          discardLatestFrame();
          coreResult = adapter.unloadGame();
        }
        if (coreResult) {
          endBackupGame();
          owner_.state_.store(EmulationWorkerState::idle, std::memory_order_release);
          fastForward_ = false;
          pacer_.pause();
        }
        break;
      case EmulationCommandType::start:
      case EmulationCommandType::resume:
        validTransition = current == EmulationWorkerState::paused;
        if (validTransition) {
          pacer_.resume(Clock::now());
          owner_.state_.store(EmulationWorkerState::running, std::memory_order_release);
        }
        break;
      case EmulationCommandType::pause:
        validTransition = current == EmulationWorkerState::running;
        if (validTransition) {
          pacer_.pause();
          owner_.state_.store(EmulationWorkerState::paused, std::memory_order_release);
        }
        break;
      case EmulationCommandType::hardReset:
        discardLatestFrame();
        coreResult = adapter.reset();
        if (coreResult) {
          coreResult = configurePacing(
            adapter, current == EmulationWorkerState::running);
        }
        break;
      case EmulationCommandType::softReset:
        discardLatestFrame();
        coreResult = adapter.softReset();
        if (coreResult) {
          coreResult = configurePacing(
            adapter, current == EmulationWorkerState::running);
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
          if (pacer_.setFastForward(command.enabled, Clock::now())) {
            fastForward_ = command.enabled;
          } else {
            coreResult = {
              CoreError::invalidTiming,
              "Fast-forward could not be applied to the current frame rate.",
            };
          }
        }
        break;
      case EmulationCommandType::inputSnapshot:
        coreResult = adapter.setInputSnapshot(command.input);
        break;
      case EmulationCommandType::videoSettings:
        discardLatestFrame();
        coreResult = adapter.applyVideoSettings(command.coreVideoSettings);
        break;
      case EmulationCommandType::audioSettings:
        audioFrames_->clear();
        coreResult = adapter.applyAudioSettings(command.coreAudioSettings);
        break;
      case EmulationCommandType::systemSettings:
        coreResult = adapter.applySystemSettings(command.coreSystemSettings);
        break;
      case EmulationCommandType::firmwareSettings:
        coreResult = adapter.applyFirmwareSettings(command.coreFirmwareSettings);
        break;
      case EmulationCommandType::cheats:
        coreResult = adapter.applyCheats(command.coreCheats);
        break;
      case EmulationCommandType::setDiscEjected:
        validTransition = current == EmulationWorkerState::paused ||
                          current == EmulationWorkerState::running;
        if (validTransition) {
          audioFrames_->clear();
          coreResult = adapter.setDiscEjected(command.enabled);
        }
        break;
      case EmulationCommandType::changeDisc:
        validTransition = current == EmulationWorkerState::paused ||
                          current == EmulationWorkerState::running;
        if (validTransition) {
          audioFrames_->clear();
          coreResult = adapter.changeDisc(command.path);
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
        coreResult = adapter.loadRawState(command.rawState);
        if (coreResult) {
          coreResult = configurePacing(
            adapter, current == EmulationWorkerState::running);
        }
        break;
    }

    updatePacingMetrics();

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
    event.rawState = std::move(capturedState);
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
      updatePacingMetrics();
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
    updatePacingMetrics();

    std::scoped_lock lock{mutex_};
    if (latestFrame_) {
      ++replacedFrameEvents_;
    }
    auto event = eventFor(
      EmulationEventType::frameCompleted, EmulationWorkerState::running);
    event.frameNumber = adapter.frameCount();
    event.hardware = adapter.hardware();
    event.videoGeneration = videoFrames_->metrics().publishedFrames;
    event.appliedInputSequence = adapter.appliedInputSequence();
    event.fastForward = fastForward_;
    latestFrame_ = std::move(event);
    eventReady_.notify_all();
  }

  CoreResult executeOneFrame(CoreAdapter& adapter)
  {
    const auto result = adapter.runFrame(false);
    if (!result) {
      return result;
    }

    CoreAudioBatchInfo audioInfo;
    const auto describedAudio = adapter.audioBatchInfo(audioInfo);
    if (!describedAudio && describedAudio.error != CoreError::noAudioAvailable) {
      return describedAudio;
    }
    if (describedAudio) {
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
      if (!fastForward_) {
        static_cast<void>(audioFrames_->write(
          std::span<const StereoAudioFrame>{audioScratch_}.first(audioInfo.frameCount)));
      }
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
    if (!write->publish(frame)) {
      return {
        CoreError::invalidVideoFrame,
        "The bounded video exchange rejected a core frame.",
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
    const FrameRateRatio rate{
      .framesNumerator = timing.masterClockHz,
      .framesDenominator = timing.masterCyclesPerFrame(),
    };
    if (!pacer_.configure(rate) ||
        !pacer_.setFastForward(fastForward_, Clock::now())) {
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

  void updatePacingMetrics()
  {
    std::scoped_lock lock{mutex_};
    pacingMetrics_ = pacer_.metrics();
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
  bool stopRequested_{false};
  bool fastForward_{false};
  EmulationWorkerStatus shutdownStatus_;
  std::shared_ptr<VideoFrameExchange> videoFrames_;
  std::shared_ptr<StereoAudioRingBuffer> audioFrames_;
  std::shared_ptr<BackupMemoryPersistence> backupPersistence_;
  bool backupGameActive_{false};
  std::array<StereoAudioFrame, maximumAudioFramesPerBatch> audioScratch_{};
  std::vector<std::uint8_t> backupScratch_;
  FramePacer pacer_;
  FramePacerMetrics pacingMetrics_;
  std::uint64_t coalescedInputCommands_{0};
  std::uint64_t coalescedVideoSettingsCommands_{0};
  std::uint64_t coalescedAudioSettingsCommands_{0};
  std::uint64_t coalescedSystemSettingsCommands_{0};
  std::uint64_t coalescedFirmwareSettingsCommands_{0};
  std::uint64_t coalescedCheatCommands_{0};
  std::uint64_t replacedFrameEvents_{0};
  std::uint64_t droppedOperationEvents_{0};
};

EmulationWorker::EmulationWorker(
  std::size_t commandCapacity,
  std::size_t eventCapacity,
  int audioSampleRate,
  std::shared_ptr<VideoFrameExchange> videoFrames,
  std::shared_ptr<StereoAudioRingBuffer> audioFrames,
  std::shared_ptr<BackupMemoryPersistence> backupPersistence)
  : private_(std::make_unique<Private>(
      *this,
      commandCapacity,
      eventCapacity,
      audioSampleRate,
      std::move(videoFrames),
      std::move(audioFrames),
      std::move(backupPersistence)))
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
