#pragma once

#include "genplusgx/state_manager.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace genplusgx {

enum class StateSlotAvailability {
  empty,
  available,
  invalid,
};

struct StateSlotSummary final {
  std::uint32_t slot{0};
  StateSlotAvailability availability{StateSlotAvailability::empty};
  SaveStateMetadata metadata;
  std::string message;
};

using StateSlotSummaries = std::array<StateSlotSummary, 10>;

enum class StateStorageCommandType {
  activateGame,
  deactivateGame,
  saveSlot,
  loadSlot,
  deleteSlot,
  importSlot,
  exportSlot,
  renameSlot,
  refreshSlots,
  saveResume,
  loadResume,
  deleteResume,
};

struct StateStorageCommand final {
  StateStorageCommandType type{StateStorageCommandType::refreshSlots};
  std::uint64_t operationId{0};
  std::uint64_t gameGeneration{0};
  std::filesystem::path path;
  std::uint32_t hardware{0};
  std::uint32_t slot{0};
  std::uint64_t emulatedFrameNumber{0};
  std::string name;
  std::vector<std::uint8_t> thumbnailPng;
  std::vector<std::uint8_t> rawPayload;
  std::vector<std::uint8_t> achievementProgress{};

  [[nodiscard]] static StateStorageCommand activate(
    std::uint64_t operationId,
    std::uint64_t gameGeneration,
    std::filesystem::path path,
    std::uint32_t hardware);
  [[nodiscard]] static StateStorageCommand simple(
    StateStorageCommandType type,
    std::uint64_t operationId,
    std::uint64_t gameGeneration,
    std::uint32_t slot = 0U);
  [[nodiscard]] static StateStorageCommand save(
    std::uint64_t operationId,
    std::uint64_t gameGeneration,
    std::uint32_t slot,
    std::uint64_t emulatedFrameNumber,
    std::vector<std::uint8_t> rawPayload,
    std::string name = {},
    std::vector<std::uint8_t> thumbnailPng = {},
    std::vector<std::uint8_t> achievementProgress = {});
  [[nodiscard]] static StateStorageCommand file(
    StateStorageCommandType type,
    std::uint64_t operationId,
    std::uint64_t gameGeneration,
    std::uint32_t slot,
    std::filesystem::path path);
  [[nodiscard]] static StateStorageCommand rename(
    std::uint64_t operationId,
    std::uint64_t gameGeneration,
    std::uint32_t slot,
    std::string name);
  [[nodiscard]] static StateStorageCommand saveResumeState(
    std::uint64_t operationId,
    std::uint64_t gameGeneration,
    std::uint64_t emulatedFrameNumber,
    std::vector<std::uint8_t> rawPayload,
    std::vector<std::uint8_t> achievementProgress = {});
};

enum class StateStorageServiceState {
  stopped,
  starting,
  running,
  stopping,
  failed,
};

enum class StateStorageError {
  none,
  notRunning,
  alreadyRunning,
  queueFull,
  invalidCommand,
  staleGame,
  persistenceFailure,
  threadFailure,
};

struct StateStorageStatus final {
  StateStorageError error{StateStorageError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept { return error == StateStorageError::none; }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

enum class StateStorageEventType {
  serviceStarted,
  sessionActivated,
  sessionDeactivated,
  slotSaved,
  slotLoaded,
  slotDeleted,
  slotImported,
  slotExported,
  slotRenamed,
  slotsRefreshed,
  resumeSaved,
  resumeLoaded,
  resumeDeleted,
  operationFailed,
  serviceStopped,
};

struct StateStorageEvent final {
  StateStorageEventType type{StateStorageEventType::operationFailed};
  std::optional<StateStorageCommandType> command;
  std::uint64_t operationId{0};
  std::uint64_t gameGeneration{0};
  std::uint32_t slot{0};
  std::filesystem::path path;
  StateStorageError error{StateStorageError::none};
  SaveStateError saveStateError{SaveStateError::none};
  std::string message;
  StateSlotSummaries slotSummaries;
  SaveStateMetadata metadata;
  std::vector<std::uint8_t> rawPayload;
  std::vector<std::uint8_t> achievementProgress{};

  [[nodiscard]] bool succeeded() const noexcept
  {
    return error == StateStorageError::none &&
      saveStateError == SaveStateError::none;
  }
};

struct StateStorageServiceMetrics final {
  std::size_t commandQueueDepth{0};
  std::size_t eventQueueDepth{0};
  std::uint64_t droppedEvents{0};
};

class StateStorageService final {
public:
  explicit StateStorageService(
    ApplicationPaths paths,
    std::size_t commandCapacity = 32U,
    std::size_t eventCapacity = 32U);
  ~StateStorageService();

  StateStorageService(const StateStorageService&) = delete;
  StateStorageService& operator=(const StateStorageService&) = delete;
  StateStorageService(StateStorageService&&) = delete;
  StateStorageService& operator=(StateStorageService&&) = delete;

  [[nodiscard]] StateStorageStatus start();
  [[nodiscard]] StateStorageStatus submit(StateStorageCommand command);
  [[nodiscard]] StateStorageStatus stop();
  [[nodiscard]] std::optional<StateStorageEvent> pollEvent();
  [[nodiscard]] std::optional<StateStorageEvent> waitForEvent(
    std::chrono::milliseconds timeout);
  [[nodiscard]] StateStorageServiceState state() const noexcept;
  [[nodiscard]] StateStorageServiceMetrics metrics() const;

private:
  class Private;
  std::unique_ptr<Private> private_;
  std::atomic<StateStorageServiceState> state_{StateStorageServiceState::stopped};
};

} // namespace genplusgx
