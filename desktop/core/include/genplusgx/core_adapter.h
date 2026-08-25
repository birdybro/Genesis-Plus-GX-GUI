#pragma once

#include <cstdint>
#include <filesystem>
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
};

struct CoreResult final {
  CoreError error{CoreError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept { return error == CoreError::none; }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

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

  [[nodiscard]] CoreLifecycleState state() const noexcept;
  [[nodiscard]] std::filesystem::path loadedPath() const;
  [[nodiscard]] std::uint64_t frameCount() const noexcept;
  [[nodiscard]] std::uint8_t hardware() const noexcept;

private:
  [[nodiscard]] CoreResult requireOwner(bool requireLoaded) const;
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
