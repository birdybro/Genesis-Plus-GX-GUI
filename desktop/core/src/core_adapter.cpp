#include "genplusgx/core_adapter.h"

#include "desktop_core_host.h"

extern "C" {
#include "shared.h"
#include "cdd.h"
}

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace genplusgx {
namespace {

constexpr std::size_t framebufferWidth = 720U;
constexpr std::size_t framebufferHeight = 576U;
constexpr std::size_t bytesPerPixel = 2U;
constexpr std::size_t maximumCorePathBytes = 255U;

std::mutex coreMutex;
CoreAdapter* activeAdapter = nullptr;

CoreResult success()
{
  return {};
}

CoreResult failure(CoreError errorCode, std::string message)
{
  return {errorCode, std::move(message)};
}

} // namespace

class CoreAdapter::Private final {
public:
  std::thread::id ownerThread;
  std::vector<std::uint8_t> framebuffer;
};

CoreAdapter::CoreAdapter(int audioSampleRate)
  : audioSampleRate_(audioSampleRate)
{
}

CoreAdapter::~CoreAdapter()
{
  std::scoped_lock lock{coreMutex};
  if (state_ != CoreLifecycleState::uninitialized) {
    unloadUnchecked();
    releaseOwnership();
  }
}

CoreResult CoreAdapter::initialize()
{
  std::scoped_lock lock{coreMutex};
  if (state_ != CoreLifecycleState::uninitialized) {
    return failure(CoreError::coreAlreadyOwned, "This core adapter is already initialized.");
  }
  if (activeAdapter != nullptr) {
    return failure(CoreError::coreAlreadyOwned, "Another core adapter owns the emulator context.");
  }
  if (audioSampleRate_ < 8'000 || audioSampleRate_ > 48'000) {
    return failure(CoreError::audioInitializationFailed, "The requested audio sample rate is outside the supported host range.");
  }

  auto privateState = std::make_unique<Private>();
  privateState->ownerThread = std::this_thread::get_id();
  privateState->framebuffer.resize(
    framebufferWidth * framebufferHeight * bytesPerPixel, 0U);

  private_ = privateState.release();
  activeAdapter = this;
  genplusgx_host_reset_defaults();
  audio_shutdown();

  std::memset(&bitmap, 0, sizeof(bitmap));
  bitmap.width = static_cast<int>(framebufferWidth);
  bitmap.height = static_cast<int>(framebufferHeight);
  bitmap.pitch = static_cast<int>(framebufferWidth * bytesPerPixel);
  bitmap.data = private_->framebuffer.data();
  bitmap.viewport.changed = 3;

  state_ = CoreLifecycleState::ready;
  return success();
}

CoreResult CoreAdapter::shutdown()
{
  std::scoped_lock lock{coreMutex};
  if (state_ == CoreLifecycleState::uninitialized) {
    return success();
  }
  if (private_->ownerThread != std::this_thread::get_id()) {
    return failure(CoreError::wrongThread, "Core shutdown must run on its owning emulation thread.");
  }

  unloadUnchecked();
  releaseOwnership();
  return success();
}

CoreResult CoreAdapter::loadGame(const std::filesystem::path& path)
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(false); !owner) {
    return owner;
  }

  std::error_code pathError;
  if (path.empty() || !std::filesystem::is_regular_file(path, pathError) || pathError) {
    return failure(CoreError::invalidPath, "The game path does not identify a readable regular file.");
  }

  const std::string nativePath = path.string();
  if (nativePath.size() < 3U || nativePath.size() > maximumCorePathBytes) {
    return failure(CoreError::invalidPath, "The game path cannot be represented safely by the core file interface.");
  }

  if (state_ == CoreLifecycleState::loaded) {
    unloadUnchecked();
  }

  std::vector<char> mutablePath(nativePath.begin(), nativePath.end());
  mutablePath.push_back('\0');
  if (load_rom(mutablePath.data()) == 0) {
    unloadUnchecked();
    return failure(CoreError::loadFailed, "Genesis Plus GX could not load the selected game image.");
  }

  if (audio_init(audioSampleRate_, 0.0) != 0) {
    unloadUnchecked();
    return failure(CoreError::audioInitializationFailed, "Genesis Plus GX could not initialize its audio resamplers.");
  }

  system_init();
  system_reset();
  loadedPath_ = path;
  frameCount_ = 0;
  hardware_ = system_hw;
  state_ = CoreLifecycleState::loaded;
  return success();
}

CoreResult CoreAdapter::unloadGame()
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(false); !owner) {
    return owner;
  }

  unloadUnchecked();
  return success();
}

CoreResult CoreAdapter::reset()
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    return owner;
  }

  system_reset();
  frameCount_ = 0;
  return success();
}

CoreResult CoreAdapter::runFrame(bool skipVideo)
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    return owner;
  }

  if (system_hw == SYSTEM_MCD) {
    system_frame_scd(skipVideo ? 1 : 0);
  } else if ((system_hw & SYSTEM_PBC) == SYSTEM_MD) {
    system_frame_gen(skipVideo ? 1 : 0);
  } else {
    system_frame_sms(skipVideo ? 1 : 0);
  }
  ++frameCount_;
  return success();
}

CoreLifecycleState CoreAdapter::state() const noexcept
{
  std::scoped_lock lock{coreMutex};
  return state_;
}

std::filesystem::path CoreAdapter::loadedPath() const
{
  std::scoped_lock lock{coreMutex};
  return loadedPath_;
}

std::uint64_t CoreAdapter::frameCount() const noexcept
{
  std::scoped_lock lock{coreMutex};
  return frameCount_;
}

std::uint8_t CoreAdapter::hardware() const noexcept
{
  std::scoped_lock lock{coreMutex};
  return hardware_;
}

CoreResult CoreAdapter::requireOwner(bool requireLoaded) const
{
  if (state_ == CoreLifecycleState::uninitialized || private_ == nullptr) {
    return failure(CoreError::notInitialized, "The core adapter is not initialized.");
  }
  if (private_->ownerThread != std::this_thread::get_id()) {
    return failure(CoreError::wrongThread, "Core operations must run on the owning emulation thread.");
  }
  if (requireLoaded && state_ != CoreLifecycleState::loaded) {
    return failure(CoreError::noGameLoaded, "No game is loaded.");
  }
  return success();
}

void CoreAdapter::unloadUnchecked() noexcept
{
  audio_shutdown();
  ggenie_shutdown();
  areplay_shutdown();
  cdd_unload();
  cart.romsize = 0;
  std::memset(&rominfo, 0, sizeof(rominfo));
  std::memset(rompath, 0, sizeof(rompath));
  loadedPath_.clear();
  frameCount_ = 0;
  hardware_ = 0;
  if (state_ != CoreLifecycleState::uninitialized) {
    state_ = CoreLifecycleState::ready;
  }
}

void CoreAdapter::releaseOwnership() noexcept
{
  std::memset(&bitmap, 0, sizeof(bitmap));
  delete private_;
  private_ = nullptr;
  state_ = CoreLifecycleState::uninitialized;
  if (activeAdapter == this) {
    activeAdapter = nullptr;
  }
}

} // namespace genplusgx
