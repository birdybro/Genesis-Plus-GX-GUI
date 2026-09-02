#pragma once

#include "genplusgx/core_adapter.h"
#include "genplusgx/emulation_capture_sink.h"
#include "genplusgx/input_snapshot.h"
#include "genplusgx/netplay/netplay_bridge.h"
#include "genplusgx/netplay/netplay_timeline.h"
#include "genplusgx/run_ahead_configuration.h"
#include "genplusgx/rewind_configuration.h"
#include "genplusgx/audio_ring_buffer.h"
#include "genplusgx/timing/frame_pacer.h"
#include "genplusgx/video/frame_exchange.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace genplusgx {

enum class EmulationWorkerState {
  stopped,
  starting,
  idle,
  paused,
  running,
  stopping,
  failed,
};

enum class EmulationCommandType {
  loadGame,
  unloadGame,
  start,
  pause,
  resume,
  hardReset,
  softReset,
  frameAdvance,
  setFastForward,
  setSlowMotion,
  speedSettings,
  setRewinding,
  rewindSettings,
  runAheadSettings,
  inputSnapshot,
  inputSettings,
  videoSettings,
  audioSettings,
  systemSettings,
  firmwareSettings,
  cheats,
  setDiscEjected,
  changeDisc,
  captureState,
  restoreState,
  debugRequest,
  startNetplay,
  remoteNetplayInput,
  stopNetplay,
};

struct EmulationCommand final {
  EmulationCommandType type{EmulationCommandType::pause};
  std::uint64_t operationId{0};
  std::filesystem::path path;
  bool enabled{false};
  InputSnapshot input;
  CoreInputSettings coreInputSettings;
  CoreVideoSettings coreVideoSettings;
  CoreAudioSettings coreAudioSettings;
  CoreSystemSettings coreSystemSettings;
  CoreFirmwareSettings coreFirmwareSettings;
  RewindConfiguration rewindConfiguration;
  RunAheadConfiguration runAheadConfiguration;
  EmulationSpeedConfiguration speedConfiguration;
  std::vector<std::uint8_t> rawState;
  std::vector<CoreCheatPatch> coreCheats;
  CoreDebugRequest coreDebugRequest;
  netplay::NetplayConfiguration netplayConfiguration;
  netplay::NetplayInputFrame netplayInput;

  [[nodiscard]] static EmulationCommand simple(
    EmulationCommandType type,
    std::uint64_t operationId);
  [[nodiscard]] static EmulationCommand load(
    std::uint64_t operationId,
    std::filesystem::path path);
  [[nodiscard]] static EmulationCommand fastForward(
    std::uint64_t operationId,
    bool enabled);
  [[nodiscard]] static EmulationCommand slowMotion(
    std::uint64_t operationId,
    bool enabled);
  [[nodiscard]] static EmulationCommand updateSpeedSettings(
    std::uint64_t operationId,
    EmulationSpeedConfiguration configuration);
  [[nodiscard]] static EmulationCommand rewinding(
    std::uint64_t operationId,
    bool enabled);
  [[nodiscard]] static EmulationCommand updateRewindSettings(
    std::uint64_t operationId,
    RewindConfiguration configuration);
  [[nodiscard]] static EmulationCommand updateRunAheadSettings(
    std::uint64_t operationId,
    RunAheadConfiguration configuration);
  [[nodiscard]] static EmulationCommand updateInput(
    std::uint64_t operationId,
    InputSnapshot input);
  [[nodiscard]] static EmulationCommand updateInputSettings(
    std::uint64_t operationId,
    CoreInputSettings settings);
  [[nodiscard]] static EmulationCommand updateVideoSettings(
    std::uint64_t operationId,
    CoreVideoSettings settings);
  [[nodiscard]] static EmulationCommand updateAudioSettings(
    std::uint64_t operationId,
    CoreAudioSettings settings);
  [[nodiscard]] static EmulationCommand updateSystemSettings(
    std::uint64_t operationId,
    CoreSystemSettings settings);
  [[nodiscard]] static EmulationCommand updateFirmwareSettings(
    std::uint64_t operationId,
    CoreFirmwareSettings settings);
  [[nodiscard]] static EmulationCommand updateCheats(
    std::uint64_t operationId,
    std::span<const CoreCheatPatch> patches);
  [[nodiscard]] static EmulationCommand discEjected(
    std::uint64_t operationId,
    bool ejected);
  [[nodiscard]] static EmulationCommand changeDisc(
    std::uint64_t operationId,
    std::filesystem::path path);
  [[nodiscard]] static EmulationCommand restore(
    std::uint64_t operationId,
    std::span<const std::uint8_t> rawState);
  [[nodiscard]] static EmulationCommand debug(
    std::uint64_t operationId,
    CoreDebugRequest request);
  [[nodiscard]] static EmulationCommand startNetplaySession(
    std::uint64_t operationId,
    netplay::NetplayConfiguration configuration);
  [[nodiscard]] static EmulationCommand remoteNetplayFrame(
    std::uint64_t operationId,
    netplay::NetplayInputFrame frame);
};

enum class EmulationWorkerError {
  none,
  notRunning,
  alreadyRunning,
  queueFull,
  invalidCommand,
  invalidTransition,
  threadFailure,
  coreFailure,
};

struct EmulationWorkerStatus final {
  EmulationWorkerError error{EmulationWorkerError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept { return error == EmulationWorkerError::none; }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

enum class EmulationEventType {
  workerStarted,
  commandCompleted,
  commandFailed,
  frameCompleted,
  stateCaptured,
  debugResponse,
  debugBreakpointHit,
  runAheadDisabled,
  netplayStarted,
  netplayStopped,
  netplayRollback,
  workerStopped,
};

struct EmulationEvent final {
  EmulationEventType type{EmulationEventType::commandFailed};
  std::optional<EmulationCommandType> command;
  std::uint64_t operationId{0};
  EmulationWorkerState workerState{EmulationWorkerState::stopped};
  EmulationWorkerError error{EmulationWorkerError::none};
  CoreError coreError{CoreError::none};
  std::string message;
  std::uint64_t frameNumber{0};
  std::uint32_t hardware{0};
  std::uint64_t videoGeneration{0};
  std::uint64_t appliedInputSequence{0};
  bool fastForward{false};
  bool slowMotion{false};
  std::uint32_t speedPercent{100U};
  bool rewinding{false};
  bool rewindAvailable{false};
  bool runAheadEnabled{false};
  bool runAheadSupported{false};
  bool runAheadActive{false};
  bool runAheadVerified{false};
  std::uint32_t runAheadFrames{1U};
  bool netplayActive{false};
  std::uint64_t netplayRollbackFrame{0U};
  std::thread::id workerThreadId;
  std::vector<std::uint8_t> rawState;
  CoreDiscInfo disc;
  CoreDebugResponse debug;

  [[nodiscard]] bool succeeded() const noexcept
  {
    return error == EmulationWorkerError::none && coreError == CoreError::none;
  }
};

struct EmulationWorkerMetrics final {
  std::size_t commandQueueDepth{0};
  std::size_t commandQueueCapacity{0};
  std::size_t eventQueueDepth{0};
  std::size_t eventQueueCapacity{0};
  std::uint64_t coalescedInputCommands{0};
  std::uint64_t coalescedInputSettingsCommands{0};
  std::uint64_t coalescedVideoSettingsCommands{0};
  std::uint64_t coalescedAudioSettingsCommands{0};
  std::uint64_t coalescedSystemSettingsCommands{0};
  std::uint64_t coalescedFirmwareSettingsCommands{0};
  std::uint64_t coalescedCheatCommands{0};
  std::uint64_t coalescedRewindSettingsCommands{0};
  std::uint64_t coalescedRunAheadSettingsCommands{0};
  std::uint64_t coalescedSpeedSettingsCommands{0};
  std::uint64_t replacedFrameEvents{0};
  std::uint64_t droppedOperationEvents{0};
  std::uint64_t pacedFrameCount{0};
  std::uint64_t lateFrameCount{0};
  std::uint64_t pacingResynchronizations{0};
  std::int64_t maximumLatenessMicroseconds{0};
  double targetFramesPerSecond{0.0};
  std::uint32_t speedPercent{100U};
  bool fastForward{false};
  bool slowMotion{false};
  bool rewinding{false};
  bool rewindAvailable{false};
  std::size_t rewindSnapshotCount{0U};
  std::size_t rewindPayloadBytes{0U};
  std::size_t rewindMemoryLimitBytes{0U};
  std::uint64_t discardedRewindSnapshots{0U};
  bool runAheadEnabled{false};
  bool runAheadSupported{false};
  bool runAheadActive{false};
  bool runAheadVerified{false};
  std::uint32_t runAheadFrames{1U};
  std::uint64_t runAheadSpeculativeFrames{0U};
  std::uint64_t runAheadRollbacks{0U};
  std::uint64_t runAheadDeterminismFailures{0U};
  std::size_t runAheadStateBytes{0U};
  std::size_t runAheadStateCapacityBytes{0U};
  bool netplayActive{false};
  std::uint64_t netplayPredictedFrames{0U};
  std::uint64_t netplayRollbackRequests{0U};
  std::uint64_t netplayRollbacks{0U};
  std::size_t netplayHistoryFrames{0U};
  std::size_t netplayHistoryBytes{0U};
};

class EmulationWorker final {
public:
  explicit EmulationWorker(
    std::size_t commandCapacity = 64U,
    std::size_t eventCapacity = 64U,
    int audioSampleRate = 48'000,
    std::shared_ptr<VideoFrameExchange> videoFrames = {},
    std::shared_ptr<StereoAudioRingBuffer> audioFrames = {},
    std::shared_ptr<BackupMemoryPersistence> backupPersistence = {},
    std::shared_ptr<EmulationCaptureSink> captureSink = {},
    std::shared_ptr<netplay::NetplayBridge> netplayBridge = {});
  ~EmulationWorker();

  EmulationWorker(const EmulationWorker&) = delete;
  EmulationWorker& operator=(const EmulationWorker&) = delete;
  EmulationWorker(EmulationWorker&&) = delete;
  EmulationWorker& operator=(EmulationWorker&&) = delete;

  [[nodiscard]] EmulationWorkerStatus start();
  [[nodiscard]] EmulationWorkerStatus submit(EmulationCommand command);
  [[nodiscard]] EmulationWorkerStatus stop();

  [[nodiscard]] std::optional<EmulationEvent> pollEvent();
  [[nodiscard]] std::optional<EmulationEvent> waitForEvent(
    std::chrono::milliseconds timeout);
  [[nodiscard]] EmulationWorkerState state() const noexcept;
  [[nodiscard]] EmulationWorkerMetrics metrics() const;
  [[nodiscard]] std::shared_ptr<VideoFrameExchange> videoFrames() const;
  [[nodiscard]] std::shared_ptr<StereoAudioRingBuffer> audioFrames() const;

private:
  class Private;
  std::unique_ptr<Private> private_;
  std::atomic<EmulationWorkerState> state_{EmulationWorkerState::stopped};
};

} // namespace genplusgx
