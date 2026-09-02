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

struct SaveStatePresentation final {
  std::string name;
  std::vector<std::uint8_t> thumbnailPng;
};

struct SaveStateMetadata final {
  std::uint32_t schemaVersion{0};
  std::uint32_t slot{0};
  std::uint32_t hardware{0};
  std::uint64_t emulatedFrameNumber{0};
  std::chrono::system_clock::time_point timestamp{};
  std::size_t payloadBytes{0};
  std::string name;
  std::vector<std::uint8_t> thumbnailPng;
};

struct SaveStateLoadResult final {
  SaveStateStatus status;
  SaveStateMetadata metadata;
  std::vector<std::uint8_t> rawPayload;
  std::vector<std::uint8_t> achievementProgress{};
};

class SaveStateManager final {
public:
  static constexpr std::uint32_t currentSchemaVersion = 3U;
  static constexpr std::uint32_t presentationSchemaVersion = 2U;
  static constexpr std::uint32_t legacySchemaVersion = 1U;
  static constexpr std::uint32_t minimumSlot = 0U;
  static constexpr std::uint32_t maximumSlot = 9U;
  static constexpr std::uint32_t resumeSlot = 0xFFFF'FFFFU;
  static constexpr std::size_t maximumPayloadBytes = 2U * 1024U * 1024U;
  static constexpr std::size_t maximumDisplayNameBytes = 96U;
  static constexpr std::size_t maximumThumbnailBytes = 512U * 1024U;
  static constexpr std::size_t maximumAchievementProgressBytes =
    4U * 1024U * 1024U;
  static constexpr std::size_t maximumFileBytes =
    maximumPayloadBytes + maximumAchievementProgressBytes +
    maximumThumbnailBytes + maximumDisplayNameBytes + 192U;

  explicit SaveStateManager(ApplicationPaths paths);

  [[nodiscard]] SaveStateStatus initialize() const;
  [[nodiscard]] std::filesystem::path gameStateDirectory(
    const GameIdentity& identity) const;
  [[nodiscard]] std::filesystem::path statePath(
    const GameIdentity& identity,
    std::uint32_t slot) const;
  [[nodiscard]] std::filesystem::path resumeStatePath(
    const GameIdentity& identity) const;

  [[nodiscard]] SaveStateStatus saveSlot(
    const GameIdentity& identity,
    std::uint32_t slot,
    std::uint32_t hardware,
    std::uint64_t emulatedFrameNumber,
    std::span<const std::uint8_t> rawPayload,
    std::chrono::system_clock::time_point timestamp =
      std::chrono::system_clock::now()) const;
  [[nodiscard]] SaveStateStatus saveSlot(
    const GameIdentity& identity,
    std::uint32_t slot,
    std::uint32_t hardware,
    std::uint64_t emulatedFrameNumber,
    std::span<const std::uint8_t> rawPayload,
    const SaveStatePresentation& presentation,
    std::chrono::system_clock::time_point timestamp =
      std::chrono::system_clock::now(),
    std::span<const std::uint8_t> achievementProgress = {}) const;
  [[nodiscard]] SaveStateLoadResult loadSlot(
    const GameIdentity& identity,
    std::uint32_t slot,
    std::uint32_t expectedHardware) const;
  [[nodiscard]] SaveStateLoadResult loadStateFile(
    const std::filesystem::path& path,
    const GameIdentity& expectedIdentity,
    std::uint32_t expectedHardware) const;
  [[nodiscard]] SaveStateStatus importSlot(
    const std::filesystem::path& source,
    const GameIdentity& identity,
    std::uint32_t slot,
    std::uint32_t expectedHardware) const;
  [[nodiscard]] SaveStateStatus exportSlot(
    const GameIdentity& identity,
    std::uint32_t slot,
    std::uint32_t expectedHardware,
    const std::filesystem::path& destination) const;
  [[nodiscard]] SaveStateStatus renameSlot(
    const GameIdentity& identity,
    std::uint32_t slot,
    std::uint32_t expectedHardware,
    std::string name) const;
  [[nodiscard]] SaveStateStatus saveResumeState(
    const GameIdentity& identity,
    std::uint32_t hardware,
    std::uint64_t emulatedFrameNumber,
    std::span<const std::uint8_t> rawPayload,
    std::chrono::system_clock::time_point timestamp =
      std::chrono::system_clock::now(),
    std::span<const std::uint8_t> achievementProgress = {}) const;
  [[nodiscard]] SaveStateLoadResult loadResumeState(
    const GameIdentity& identity,
    std::uint32_t expectedHardware) const;
  [[nodiscard]] SaveStateStatus deleteSlot(
    const GameIdentity& identity,
    std::uint32_t slot) const;
  [[nodiscard]] SaveStateStatus deleteResumeState(
    const GameIdentity& identity) const;

private:
  [[nodiscard]] SaveStateStatus saveFile(
    const std::filesystem::path& path,
    const GameIdentity& identity,
    std::uint32_t encodedSlot,
    std::uint32_t hardware,
    std::uint64_t emulatedFrameNumber,
    std::span<const std::uint8_t> rawPayload,
    const SaveStatePresentation& presentation,
    std::chrono::system_clock::time_point timestamp,
    std::span<const std::uint8_t> achievementProgress) const;
  [[nodiscard]] SaveStateLoadResult loadFile(
    const std::filesystem::path& path,
    const GameIdentity& expectedIdentity,
    std::uint32_t expectedHardware,
    const std::uint32_t* expectedSlot) const;
  [[nodiscard]] SaveStateStatus deleteFile(
    const GameIdentity& identity,
    const std::filesystem::path& path) const;

  ApplicationPaths paths_;
};

} // namespace genplusgx
