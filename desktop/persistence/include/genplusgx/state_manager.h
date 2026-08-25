#pragma once

#include "genplusgx/persistence.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace genplusgx {

enum class SaveStateError {
  none,
  invalidSlot,
  invalidGameIdentity,
  invalidPayload,
  missingState,
  corruptState,
  unsupportedSchema,
  wrongGame,
  wrongSystem,
  checksumMismatch,
  ioError,
};

struct SaveStateStatus final {
  SaveStateError error{SaveStateError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept { return error == SaveStateError::none; }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

struct SaveStateMetadata final {
  std::uint32_t schemaVersion{0};
  std::uint32_t slot{0};
  std::uint32_t hardware{0};
  std::uint64_t emulatedFrameNumber{0};
  std::chrono::system_clock::time_point timestamp{};
  std::size_t payloadBytes{0};
};

struct SaveStateLoadResult final {
  SaveStateStatus status;
  SaveStateMetadata metadata;
  std::vector<std::uint8_t> rawPayload;
};

class SaveStateManager final {
public:
  static constexpr std::uint32_t currentSchemaVersion = 1U;
  static constexpr std::uint32_t minimumSlot = 0U;
  static constexpr std::uint32_t maximumSlot = 9U;
  static constexpr std::size_t maximumPayloadBytes = 2U * 1024U * 1024U;

  explicit SaveStateManager(ApplicationPaths paths);

  [[nodiscard]] SaveStateStatus initialize() const;
  [[nodiscard]] std::filesystem::path gameStateDirectory(
    const GameIdentity& identity) const;
  [[nodiscard]] std::filesystem::path statePath(
    const GameIdentity& identity,
    std::uint32_t slot) const;

  [[nodiscard]] SaveStateStatus saveSlot(
    const GameIdentity& identity,
    std::uint32_t slot,
    std::uint32_t hardware,
    std::uint64_t emulatedFrameNumber,
    std::span<const std::uint8_t> rawPayload,
    std::chrono::system_clock::time_point timestamp =
      std::chrono::system_clock::now()) const;
  [[nodiscard]] SaveStateLoadResult loadSlot(
    const GameIdentity& identity,
    std::uint32_t slot,
    std::uint32_t expectedHardware) const;
  [[nodiscard]] SaveStateLoadResult loadStateFile(
    const std::filesystem::path& path,
    const GameIdentity& expectedIdentity,
    std::uint32_t expectedHardware) const;
  [[nodiscard]] SaveStateStatus deleteSlot(
    const GameIdentity& identity,
    std::uint32_t slot) const;

private:
  [[nodiscard]] SaveStateLoadResult loadFile(
    const std::filesystem::path& path,
    const GameIdentity& expectedIdentity,
    std::uint32_t expectedHardware,
    const std::uint32_t* expectedSlot) const;

  ApplicationPaths paths_;
};

} // namespace genplusgx
