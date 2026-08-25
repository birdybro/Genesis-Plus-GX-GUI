#include "genplusgx/core_adapter.h"

#include "desktop_core_host.h"

extern "C" {
#include "shared.h"
#include "cdd.h"
#include "md_ntsc.h"
#include "sms_ntsc.h"
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
constexpr std::size_t maximumAudioFramesPerBatch = 4'096U;

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
  std::vector<std::int16_t> audioScratch;
  std::size_t pendingAudioFrames{0};
  std::uint64_t pendingAudioFrameNumber{0};
  std::uint64_t droppedAudioFrames{0};
  std::uint64_t droppedAudioBatches{0};
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
  privateState->audioScratch.resize(maximumAudioFramesPerBatch * 2U, 0);

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
  private_->pendingAudioFrames = 0;
  private_->pendingAudioFrameNumber = 0;
  private_->droppedAudioFrames = 0;
  private_->droppedAudioBatches = 0;
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
  private_->pendingAudioFrames = 0;
  private_->pendingAudioFrameNumber = 0;
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

  if (private_->pendingAudioFrames > 0U) {
    private_->droppedAudioFrames += private_->pendingAudioFrames;
    ++private_->droppedAudioBatches;
  }
  const int generatedAudioFrames = audio_update(private_->audioScratch.data());
  if (generatedAudioFrames < 0 ||
      static_cast<std::size_t>(generatedAudioFrames) > maximumAudioFramesPerBatch) {
    private_->pendingAudioFrames = 0;
    private_->pendingAudioFrameNumber = 0;
    return failure(CoreError::invalidAudioBatch, "The core generated an invalid audio batch size.");
  }
  private_->pendingAudioFrames = static_cast<std::size_t>(generatedAudioFrames);
  private_->pendingAudioFrameNumber = frameCount_;
  return success();
}

CoreResult CoreAdapter::videoFrameInfo(CoreVideoFrameInfo& output) const
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    return owner;
  }
  return describeVideoFrame(output);
}

CoreResult CoreAdapter::copyVideoFrame(
  std::span<std::uint16_t> destination,
  CoreVideoFrameInfo& output)
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    return owner;
  }
  if (const auto described = describeVideoFrame(output); !described) {
    return described;
  }
  if (destination.size() < output.pixelCount()) {
    return failure(CoreError::videoBufferTooSmall, "The destination cannot hold the complete video frame.");
  }

  const auto sourcePitchBytes = static_cast<std::size_t>(bitmap.pitch);
  const auto copiedRowBytes = static_cast<std::size_t>(output.width) * bytesPerPixel;
  for (std::size_t row = 0; row < output.height; ++row) {
    std::memcpy(
      destination.data() + (row * output.width),
      bitmap.data + (row * sourcePitchBytes),
      copiedRowBytes);
  }
  bitmap.viewport.changed &= ~1;
  return success();
}

CoreResult CoreAdapter::audioBatchInfo(CoreAudioBatchInfo& output) const
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    return owner;
  }
  return describeAudioBatch(output);
}

CoreResult CoreAdapter::copyAudioFrames(
  std::span<StereoAudioFrame> destination,
  CoreAudioBatchInfo& output)
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    return owner;
  }
  if (const auto described = describeAudioBatch(output); !described) {
    return described;
  }
  if (destination.size() < output.frameCount) {
    return failure(CoreError::audioBufferTooSmall, "The destination cannot hold the pending stereo audio batch.");
  }

  for (std::size_t index = 0; index < output.frameCount; ++index) {
    destination[index] = {
      .left = private_->audioScratch[index * 2U],
      .right = private_->audioScratch[(index * 2U) + 1U],
    };
  }
  private_->pendingAudioFrames = 0;
  private_->pendingAudioFrameNumber = 0;
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

CoreResult CoreAdapter::describeVideoFrame(CoreVideoFrameInfo& output) const
{
  const int nativeWidth = bitmap.viewport.w + (2 * bitmap.viewport.x);
  int width = nativeWidth;
  if (config.ntsc != 0) {
    width = (reg[12] & 0x01) != 0 ? MD_NTSC_OUT_WIDTH(nativeWidth)
                                  : SMS_NTSC_OUT_WIDTH(nativeWidth);
  }
  int height = bitmap.viewport.h + (2 * bitmap.viewport.y);
  const bool doublesInterlacedLines = interlaced != 0 && config.render != 0;
  if (doublesInterlacedLines) {
    height *= 2;
  }

  const bool dimensionsAreValid = bitmap.data != nullptr && bitmap.width > 0 &&
                                  bitmap.height > 0 && bitmap.pitch > 0 &&
                                  width > 0 && height > 0 &&
                                  width <= (bitmap.pitch / static_cast<int>(bytesPerPixel)) &&
                                  height <= bitmap.height;
  if (!dimensionsAreValid) {
    output = {};
    return failure(CoreError::invalidVideoFrame, "The core reported an invalid video viewport.");
  }

  output = {
    .format = CorePixelFormat::rgb565,
    .width = static_cast<std::uint32_t>(width),
    .height = static_cast<std::uint32_t>(height),
    .sourceSurfaceWidth = static_cast<std::uint32_t>(bitmap.width),
    .sourceSurfaceHeight = static_cast<std::uint32_t>(bitmap.height),
    .sourcePitchPixels = static_cast<std::uint32_t>(
      bitmap.pitch / static_cast<int>(bytesPerPixel)),
    .coreViewportX = bitmap.viewport.x,
    .coreViewportY = bitmap.viewport.y,
    .frameNumber = frameCount_,
    .viewportChanged = (bitmap.viewport.changed & 1) != 0,
    .interlaced = interlaced != 0,
    .oddField = odd_frame != 0,
  };
  return success();
}

CoreResult CoreAdapter::describeAudioBatch(CoreAudioBatchInfo& output) const
{
  if (private_->pendingAudioFrames == 0U) {
    output = {};
    return failure(CoreError::noAudioAvailable, "No generated audio batch is pending.");
  }
  output = {
    .sampleRate = static_cast<std::uint32_t>(audioSampleRate_),
    .channels = 2,
    .frameCount = private_->pendingAudioFrames,
    .emulatedFrameNumber = private_->pendingAudioFrameNumber,
    .droppedFrameCount = private_->droppedAudioFrames,
    .droppedBatchCount = private_->droppedAudioBatches,
  };
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
  if (private_ != nullptr) {
    private_->pendingAudioFrames = 0;
    private_->pendingAudioFrameNumber = 0;
    private_->droppedAudioFrames = 0;
    private_->droppedAudioBatches = 0;
  }
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

std::uint64_t hashVideoFrame(std::span<const std::uint16_t> pixels) noexcept
{
  constexpr std::uint64_t fnvOffsetBasis = 14'695'981'039'346'656'037ULL;
  constexpr std::uint64_t fnvPrime = 1'099'511'628'211ULL;
  std::uint64_t hash = fnvOffsetBasis;
  for (const auto pixel : pixels) {
    hash ^= static_cast<std::uint8_t>(pixel & 0xFFU);
    hash *= fnvPrime;
    hash ^= static_cast<std::uint8_t>(pixel >> 8U);
    hash *= fnvPrime;
  }
  return hash;
}

std::uint64_t hashAudioFrames(std::span<const StereoAudioFrame> frames) noexcept
{
  constexpr std::uint64_t fnvOffsetBasis = 14'695'981'039'346'656'037ULL;
  constexpr std::uint64_t fnvPrime = 1'099'511'628'211ULL;
  std::uint64_t hash = fnvOffsetBasis;
  for (const auto& frame : frames) {
    for (const auto sample : {frame.left, frame.right}) {
      const auto bits = static_cast<std::uint16_t>(sample);
      hash ^= static_cast<std::uint8_t>(bits & 0xFFU);
      hash *= fnvPrime;
      hash ^= static_cast<std::uint8_t>(bits >> 8U);
      hash *= fnvPrime;
    }
  }
  return hash;
}

} // namespace genplusgx
