#pragma once

#include "genplusgx/audio_frame.h"
#include "genplusgx/backup_memory.h"
#include "genplusgx/core_video_settings.h"
#include "genplusgx/input_snapshot.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace genplusgx {

enum class CoreLifecycleState {
  uninitialized,
  ready,
  loaded,
};

enum class CoreError {
  none,
  notInitialized,
  noGameLoaded,
  wrongThread,
  coreAlreadyOwned,
  invalidPath,
  loadFailed,
  audioInitializationFailed,
  invalidVideoFrame,
  videoBufferTooSmall,
  noAudioAvailable,
  audioBufferTooSmall,
  invalidAudioBatch,
  invalidTiming,
  invalidSettings,
  staleInputSnapshot,
  invalidStatePayload,
  invalidBackupMemory,
  persistenceFailed,
  stateSaveFailed,
  stateLoadFailed,
};

struct CoreResult final {
  CoreError error{CoreError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept { return error == CoreError::none; }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

enum class CorePixelFormat {
  rgb565,
};

struct CoreVideoFrameInfo final {
  CorePixelFormat format{CorePixelFormat::rgb565};
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::uint32_t sourceSurfaceWidth{0};
  std::uint32_t sourceSurfaceHeight{0};
  std::uint32_t sourcePitchPixels{0};
  std::int32_t coreViewportX{0};
  std::int32_t coreViewportY{0};
  std::uint64_t frameNumber{0};
  bool viewportChanged{false};
  bool interlaced{false};
  bool oddField{false};

  [[nodiscard]] std::size_t pixelCount() const noexcept
  {
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  }
};

[[nodiscard]] std::uint64_t hashVideoFrame(
  std::span<const std::uint16_t> pixels) noexcept;

struct CoreAudioBatchInfo final {
  std::uint32_t sampleRate{0};
  std::uint32_t channels{2};
  std::size_t frameCount{0};
  std::uint64_t emulatedFrameNumber{0};
  std::uint64_t droppedFrameCount{0};
  std::uint64_t droppedBatchCount{0};
};

struct CoreTimingInfo final {
  std::uint64_t masterClockHz{0};
  std::uint32_t linesPerFrame{0};
  std::uint32_t masterCyclesPerLine{0};
  bool pal{false};
  bool segaCd{false};

  [[nodiscard]] std::uint64_t masterCyclesPerFrame() const noexcept
  {
    return static_cast<std::uint64_t>(linesPerFrame) * masterCyclesPerLine;
  }

  [[nodiscard]] double framesPerSecond() const noexcept
  {
    const auto cycles = masterCyclesPerFrame();
    return cycles == 0U ? 0.0
                        : static_cast<double>(masterClockHz) /
                            static_cast<double>(cycles);
  }
};

[[nodiscard]] std::uint64_t hashAudioFrames(
  std::span<const StereoAudioFrame> frames) noexcept;

class CoreAdapter final {
public:
  explicit CoreAdapter(int audioSampleRate = 48'000);
  ~CoreAdapter();

  CoreAdapter(const CoreAdapter&) = delete;
  CoreAdapter& operator=(const CoreAdapter&) = delete;
  CoreAdapter(CoreAdapter&&) = delete;
  CoreAdapter& operator=(CoreAdapter&&) = delete;

  [[nodiscard]] CoreResult initialize();
  [[nodiscard]] CoreResult shutdown();

  [[nodiscard]] CoreResult loadGame(const std::filesystem::path& path);
  [[nodiscard]] CoreResult unloadGame();
  [[nodiscard]] CoreResult reset();
  [[nodiscard]] CoreResult softReset();
  [[nodiscard]] CoreResult runFrame(bool skipVideo = false);
  [[nodiscard]] CoreResult videoFrameInfo(CoreVideoFrameInfo& output) const;
  [[nodiscard]] CoreResult copyVideoFrame(
    std::span<std::uint16_t> destination,
    CoreVideoFrameInfo& output);
  [[nodiscard]] CoreResult audioBatchInfo(CoreAudioBatchInfo& output) const;
  [[nodiscard]] CoreResult copyAudioFrames(
    std::span<StereoAudioFrame> destination,
    CoreAudioBatchInfo& output);
  [[nodiscard]] CoreResult timingInfo(CoreTimingInfo& output) const;
  [[nodiscard]] CoreResult setInputSnapshot(const InputSnapshot& snapshot);
  [[nodiscard]] CoreResult saveRawState(std::vector<std::uint8_t>& output);
  [[nodiscard]] CoreResult loadRawState(std::span<const std::uint8_t> state);
  [[nodiscard]] CoreResult backupMemoryInfo(
    BackupMemoryKind kind,
    BackupMemoryInfo& output) const;
  [[nodiscard]] CoreResult copyBackupMemory(
    BackupMemoryKind kind,
    std::span<std::uint8_t> destination,
    BackupMemoryInfo& output) const;
  [[nodiscard]] CoreResult loadBackupMemory(
    BackupMemoryKind kind,
    std::span<const std::uint8_t> data);
  [[nodiscard]] CoreResult initializeBackupMemory(BackupMemoryKind kind);
  [[nodiscard]] CoreResult applyVideoSettings(
    const CoreVideoSettings& settings);
  [[nodiscard]] CoreResult videoSettings(CoreVideoSettings& output) const;

  [[nodiscard]] CoreLifecycleState state() const noexcept;
  [[nodiscard]] std::filesystem::path loadedPath() const;
  [[nodiscard]] std::uint64_t frameCount() const noexcept;
  [[nodiscard]] std::uint8_t hardware() const noexcept;
  [[nodiscard]] std::uint64_t appliedInputSequence() const noexcept;

private:
  [[nodiscard]] CoreResult requireOwner(bool requireLoaded) const;
  [[nodiscard]] CoreResult describeVideoFrame(CoreVideoFrameInfo& output) const;
  [[nodiscard]] CoreResult describeAudioBatch(CoreAudioBatchInfo& output) const;
  void applyPendingInput() noexcept;
  void unloadUnchecked() noexcept;
  void releaseOwnership() noexcept;

  int audioSampleRate_;
  CoreLifecycleState state_{CoreLifecycleState::uninitialized};
  std::filesystem::path loadedPath_;
  std::uint64_t frameCount_{0};
  std::uint8_t hardware_{0};
  class Private;
  Private* private_{nullptr};
};

} // namespace genplusgx
