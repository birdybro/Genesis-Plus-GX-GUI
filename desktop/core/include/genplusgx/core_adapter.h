#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

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
  [[nodiscard]] CoreResult runFrame(bool skipVideo = false);
  [[nodiscard]] CoreResult videoFrameInfo(CoreVideoFrameInfo& output) const;
  [[nodiscard]] CoreResult copyVideoFrame(
    std::span<std::uint16_t> destination,
    CoreVideoFrameInfo& output);

  [[nodiscard]] CoreLifecycleState state() const noexcept;
  [[nodiscard]] std::filesystem::path loadedPath() const;
  [[nodiscard]] std::uint64_t frameCount() const noexcept;
  [[nodiscard]] std::uint8_t hardware() const noexcept;

private:
  [[nodiscard]] CoreResult requireOwner(bool requireLoaded) const;
  [[nodiscard]] CoreResult describeVideoFrame(CoreVideoFrameInfo& output) const;
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
