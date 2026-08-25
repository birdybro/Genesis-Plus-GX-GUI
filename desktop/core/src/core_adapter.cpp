#include "genplusgx/core_adapter.h"
#include "genplusgx/game_file.h"

#include "desktop_core_host.h"

extern "C" {
#include "shared.h"
#include "cdd.h"
#include "md_ntsc.h"
#include "sms_ntsc.h"
#include "sms_cart.h"

extern md_ntsc_t* md_ntsc;
extern sms_ntsc_t* sms_ntsc;
}

#include <algorithm>
#include <array>
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
constexpr std::size_t maximumAudioFramesPerBatch = 4'096U;
constexpr std::size_t cartridgeSramSize = 0x1'0000U;
constexpr std::size_t scdInternalBramSize = 0x2'000U;
constexpr std::array<std::uint8_t, 64> bramFormat{
  0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x00,0x00,0x00,0x00,0x40,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x53,0x45,0x47,0x41,0x5f,0x43,0x44,0x5f,0x52,0x4f,0x4d,0x00,0x01,0x00,0x00,0x00,
  0x52,0x41,0x4d,0x5f,0x43,0x41,0x52,0x54,0x52,0x49,0x44,0x47,0x45,0x5f,0x5f,0x5f,
};

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

bool usesNukedYm2612(CoreYm2612Core core) noexcept
{
  return core == CoreYm2612Core::nukedYm2612 ||
    core == CoreYm2612Core::nukedYm3438;
}

uint8 ym2413Mode(CoreYm2413Mode mode) noexcept
{
  switch (mode) {
    case CoreYm2413Mode::disabled: return 0U;
    case CoreYm2413Mode::enabled: return 1U;
    case CoreYm2413Mode::autoDetect: return 2U;
  }
  return 2U;
}

uint8 mameYm2612Type(CoreYm2612Core core) noexcept
{
  switch (core) {
    case CoreYm2612Core::mameDiscrete: return YM2612_DISCRETE;
    case CoreYm2612Core::mameIntegrated: return YM2612_INTEGRATED;
    case CoreYm2612Core::mameEnhanced: return YM2612_ENHANCED;
    case CoreYm2612Core::nukedYm2612:
    case CoreYm2612Core::nukedYm3438: return YM2612_DISCRETE;
  }
  return YM2612_DISCRETE;
}

std::span<std::uint8_t> backupMemory(BackupMemoryKind kind)
{
  switch (kind) {
    case BackupMemoryKind::cartridgeSram:
      if (sram.on != 0U) {
        return {sram.sram, cartridgeSramSize};
      }
      break;
    case BackupMemoryKind::scdInternalBram:
      if (system_hw == SYSTEM_MCD) {
        return {scd.bram, scdInternalBramSize};
      }
      break;
    case BackupMemoryKind::scdRamCartridge:
      if (system_hw == SYSTEM_MCD && scd.cartridge.id != 0U &&
          scd.cartridge.mask < sizeof(scd.cartridge.area)) {
        return {scd.cartridge.area,
          static_cast<std::size_t>(scd.cartridge.mask) + 1U};
      }
      break;
  }
  return {};
}

bool hasValidBramFormat(std::span<const std::uint8_t> data)
{
  constexpr std::size_t signatureSize = 32U;
  return data.size() >= bramFormat.size() &&
    std::equal(
      bramFormat.end() - static_cast<std::ptrdiff_t>(signatureSize),
      bramFormat.end(),
      data.end() - static_cast<std::ptrdiff_t>(signatureSize));
}

void formatBram(std::span<std::uint8_t> data)
{
  std::fill(data.begin(), data.end(), 0U);
  auto format = bramFormat;
  const auto blocks = (data.size() / 64U) - 3U;
  const auto upper = static_cast<std::uint8_t>((blocks >> 8U) & 0xFFU);
  const auto lower = static_cast<std::uint8_t>(blocks & 0xFFU);
  for (const auto index : {0x10U, 0x12U, 0x14U, 0x16U}) {
    format[index] = upper;
    format[index + 1U] = lower;
  }
  std::copy(format.begin(), format.end(),
    data.end() - static_cast<std::ptrdiff_t>(format.size()));
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
  InputSnapshot pendingInput{};
  std::uint64_t appliedInputSequence{0};
  std::uint64_t newestInputSequence{0};
  bool hasPendingInput{false};
  std::vector<std::uint8_t> stateScratch;
  std::vector<std::uint8_t> stateLoadScratch;
  std::unique_ptr<md_ntsc_t> mdNtsc;
  std::unique_ptr<sms_ntsc_t> smsNtsc;
  CoreVideoSettings videoSettings;
  CoreAudioSettings audioSettings;
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
  privateState->stateScratch.resize(STATE_SIZE, 0U);
  privateState->stateLoadScratch.resize(STATE_SIZE, 0U);
  privateState->mdNtsc = std::make_unique<md_ntsc_t>();
  privateState->smsNtsc = std::make_unique<sms_ntsc_t>();

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
  if (system_hw == SYSTEM_MCD) {
    formatBram(backupMemory(BackupMemoryKind::scdInternalBram));
    const auto cartridgeBram = backupMemory(BackupMemoryKind::scdRamCartridge);
    if (!cartridgeBram.empty()) {
      formatBram(cartridgeBram);
    }
  }
  loadedPath_ = path;
  frameCount_ = 0;
  hardware_ = system_hw;
  private_->pendingAudioFrames = 0;
  private_->pendingAudioFrameNumber = 0;
  private_->droppedAudioFrames = 0;
  private_->droppedAudioBatches = 0;
  private_->pendingInput = {};
  private_->appliedInputSequence = 0;
  private_->newestInputSequence = 0;
  private_->hasPendingInput = false;
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

CoreResult CoreAdapter::softReset()
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    return owner;
  }

  gen_reset(0);
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

  applyPendingInput();
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

CoreResult CoreAdapter::timingInfo(CoreTimingInfo& output) const
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    return owner;
  }
  if (system_clock == 0U || lines_per_frame == 0U) {
    output = {};
    return failure(CoreError::invalidTiming, "The core reported invalid timing values.");
  }
  output = {
    .masterClockHz = system_clock,
    .linesPerFrame = lines_per_frame,
    .masterCyclesPerLine = MCYCLES_PER_LINE,
    .pal = vdp_pal != 0U,
    .segaCd = system_hw == SYSTEM_MCD,
  };
  return success();
}

CoreResult CoreAdapter::setInputSnapshot(const InputSnapshot& snapshot)
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    return owner;
  }
  if (snapshot.sequence < private_->newestInputSequence) {
    return failure(CoreError::staleInputSnapshot, "The input snapshot is older than the newest queued state.");
  }

  private_->pendingInput = snapshot;
  private_->newestInputSequence = snapshot.sequence;
  private_->hasPendingInput = true;
  return success();
}

CoreResult CoreAdapter::saveRawState(std::vector<std::uint8_t>& output)
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    return owner;
  }

  const int stateBytes = state_save(private_->stateScratch.data());
  if (stateBytes <= 0 || stateBytes > STATE_SIZE) {
    output.clear();
    return failure(CoreError::stateSaveFailed, "Genesis Plus GX generated an invalid raw state size.");
  }
  output.assign(
    private_->stateScratch.begin(),
    private_->stateScratch.begin() + stateBytes);
  return success();
}

CoreResult CoreAdapter::loadRawState(std::span<const std::uint8_t> state)
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    return owner;
  }
  if (state.size() < 16U || state.size() > private_->stateScratch.size()) {
    return failure(CoreError::invalidStatePayload, "The raw state size is outside the core's safe bounds.");
  }

  const int expectedBytes = state_save(private_->stateScratch.data());
  if (expectedBytes <= 0 || expectedBytes > STATE_SIZE ||
      state.size() != static_cast<std::size_t>(expectedBytes)) {
    return failure(CoreError::invalidStatePayload, "The raw state size does not match the loaded hardware.");
  }

  std::ranges::fill(private_->stateLoadScratch, 0U);
  std::ranges::copy(state, private_->stateLoadScratch.begin());
  const int loadedBytes = state_load(private_->stateLoadScratch.data());
  if (loadedBytes != expectedBytes) {
    if (state_load(private_->stateScratch.data()) != expectedBytes) {
      return failure(CoreError::stateLoadFailed,
        "Genesis Plus GX rejected the state payload and could not restore its prior state.");
    }
    return failure(CoreError::stateLoadFailed, "Genesis Plus GX rejected the raw state payload.");
  }

  private_->pendingAudioFrames = 0;
  private_->pendingAudioFrameNumber = 0;
  bitmap.viewport.changed = 3;
  frameCount_ = 0;
  return success();
}

CoreResult CoreAdapter::backupMemoryInfo(
  BackupMemoryKind kind,
  BackupMemoryInfo& output) const
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    output = {.kind = kind};
    return owner;
  }
  const auto memory = backupMemory(kind);
  output = {
    .kind = kind,
    .size = memory.size(),
    .available = !memory.empty(),
  };
  return success();
}

CoreResult CoreAdapter::copyBackupMemory(
  BackupMemoryKind kind,
  std::span<std::uint8_t> destination,
  BackupMemoryInfo& output) const
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    output = {.kind = kind};
    return owner;
  }
  const auto memory = backupMemory(kind);
  output = {
    .kind = kind,
    .size = memory.size(),
    .available = !memory.empty(),
  };
  if (memory.empty()) {
    return success();
  }
  if (destination.size() < memory.size()) {
    return failure(
      CoreError::invalidBackupMemory,
      "The destination cannot hold the complete backup memory image.");
  }
  std::ranges::copy(memory, destination.begin());
  return success();
}

CoreResult CoreAdapter::loadBackupMemory(
  BackupMemoryKind kind,
  std::span<const std::uint8_t> data)
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    return owner;
  }
  const auto memory = backupMemory(kind);
  if (memory.empty()) {
    return failure(
      CoreError::invalidBackupMemory,
      "The selected backup memory type is unavailable for the loaded hardware.");
  }
  if (data.size() != memory.size()) {
    return failure(
      CoreError::invalidBackupMemory,
      "The backup memory image size does not match the loaded hardware.");
  }
  if (kind != BackupMemoryKind::cartridgeSram && !hasValidBramFormat(data)) {
    return failure(
      CoreError::invalidBackupMemory,
      "The Sega CD backup memory image has an invalid format signature.");
  }
  std::ranges::copy(data, memory.begin());
  return success();
}

CoreResult CoreAdapter::initializeBackupMemory(BackupMemoryKind kind)
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    return owner;
  }
  const auto memory = backupMemory(kind);
  if (memory.empty()) {
    return success();
  }
  if (kind != BackupMemoryKind::cartridgeSram) {
    formatBram(memory);
  }
  return success();
}

CoreResult CoreAdapter::applyVideoSettings(const CoreVideoSettings& settings)
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(false); !owner) {
    return owner;
  }
  if (!validateCoreVideoSettings(settings)) {
    return failure(CoreError::invalidSettings,
      "The requested core video settings contain an unsupported value.");
  }

  if (settings.ntscFilter != CoreNtscFilter::disabled) {
    const md_ntsc_setup_t* mdSetup = &md_ntsc_composite;
    const sms_ntsc_setup_t* smsSetup = &sms_ntsc_composite;
    switch (settings.ntscFilter) {
      case CoreNtscFilter::monochrome:
        mdSetup = &md_ntsc_monochrome;
        smsSetup = &sms_ntsc_monochrome;
        break;
      case CoreNtscFilter::composite:
        break;
      case CoreNtscFilter::sVideo:
        mdSetup = &md_ntsc_svideo;
        smsSetup = &sms_ntsc_svideo;
        break;
      case CoreNtscFilter::rgb:
        mdSetup = &md_ntsc_rgb;
        smsSetup = &sms_ntsc_rgb;
        break;
      case CoreNtscFilter::disabled:
        break;
    }
    md_ntsc_init(private_->mdNtsc.get(), mdSetup);
    sms_ntsc_init(private_->smsNtsc.get(), smsSetup);
    md_ntsc = private_->mdNtsc.get();
    sms_ntsc = private_->smsNtsc.get();
  }

  config.overscan = static_cast<uint8>(settings.overscan);
  config.ntsc = settings.ntscFilter == CoreNtscFilter::disabled ? 0U : 1U;
  config.gg_extra = settings.gameGearExtendedScreen ? 1U : 0U;
  config.render = static_cast<uint8>(settings.interlacedRender);
  private_->videoSettings = settings;
  bitmap.viewport.changed = 11;
  if (state_ == CoreLifecycleState::loaded) {
    if (system_hw == SYSTEM_GG && config.gg_extra == 0U) {
      bitmap.viewport.x = (config.overscan & 2U) != 0U ? 14 : -48;
    } else {
      bitmap.viewport.x = (config.overscan & 2U) != 0U ? 14 : 0;
    }
  }
  return success();
}

CoreResult CoreAdapter::videoSettings(CoreVideoSettings& output) const
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(false); !owner) {
    output = {};
    return owner;
  }
  output = private_->videoSettings;
  return success();
}

CoreResult CoreAdapter::applyAudioSettings(const CoreAudioSettings& settings)
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(false); !owner) {
    return owner;
  }
  if (!validateCoreAudioSettings(settings)) {
    return failure(CoreError::invalidSettings,
      "The requested core audio settings contain an unsupported value.");
  }

  const bool loaded = state_ == CoreLifecycleState::loaded;
  const bool ym2612ImplementationChanged =
    usesNukedYm2612(private_->audioSettings.ym2612Core) !=
      usesNukedYm2612(settings.ym2612Core);
  const bool ym2413ImplementationChanged =
    private_->audioSettings.ym2413Core != settings.ym2413Core;
  const bool ym2413ModeChanged =
    private_->audioSettings.ym2413Mode != settings.ym2413Mode;
  const uint8 detectedYm2413 = config.ym2413;

  config.mono = settings.output == CoreSoundOutput::mono ? 1U : 0U;
  config.filter = static_cast<uint8>(settings.filter);
  config.psg_preamp = static_cast<uint16>(settings.psgLevelPercent);
  config.fm_preamp = static_cast<uint16>(settings.fmLevelPercent);
  config.cdda_volume = static_cast<uint16>(settings.cddaLevelPercent);
  config.pcm_volume = static_cast<uint16>(settings.pcmLevelPercent);
  config.lp_range = static_cast<uint32>(
    (static_cast<std::uint64_t>(settings.lowPassPercent) * 65'536U) / 100U);
  config.lg = static_cast<uint16>(settings.equalizerLowPercent);
  config.mg = static_cast<uint16>(settings.equalizerMidPercent);
  config.hg = static_cast<uint16>(settings.equalizerHighPercent);
  config.hq_fm = settings.highQualityFm ? 1U : 0U;
  config.hq_psg = settings.highQualityPsg ? 1U : 0U;
  config.ym2612 = mameYm2612Type(settings.ym2612Core);
  config.ym3438 = usesNukedYm2612(settings.ym2612Core) ? 1U : 0U;
  config.ym2413 = settings.ym2413Mode == CoreYm2413Mode::autoDetect &&
      loaded && !ym2413ModeChanged
    ? detectedYm2413 : ym2413Mode(settings.ym2413Mode);
  config.opll = settings.ym2413Core == CoreYm2413Core::nuked ? 1U : 0U;
  private_->audioSettings = settings;

  if (!loaded) {
    return success();
  }

  audio_set_equalizer();
  psg_config(0, config.psg_preamp,
    (system_hw & SYSTEM_PBC) == SYSTEM_MD ? 0xff : io_reg[6]);

  const bool genesisAudio = (system_hw & SYSTEM_PBC) == SYSTEM_MD;
  if (!genesisAudio && ym2413ModeChanged &&
      settings.ym2413Mode == CoreYm2413Mode::autoDetect) {
    std::array<uint8, cartridgeSramSize> savedSram{};
    std::ranges::copy(sram.sram, sram.sram + cartridgeSramSize,
      savedSram.begin());
    sms_cart_init();
    std::ranges::copy(savedSram, sram.sram);
  }
  if ((genesisAudio && ym2612ImplementationChanged) ||
      (!genesisAudio && (ym2413ImplementationChanged || ym2413ModeChanged))) {
    sound_init();
    if (genesisAudio && config.ym3438 != 0U) {
      OPN2_SetChipType(settings.ym2612Core == CoreYm2612Core::nukedYm2612
        ? ym3438_mode_ym2612 : ym3438_mode_readmode);
    }
    sound_reset();
  } else if (genesisAudio) {
    if (config.ym3438 != 0U) {
      OPN2_SetChipType(settings.ym2612Core == CoreYm2612Core::nukedYm2612
        ? ym3438_mode_ym2612 : ym3438_mode_readmode);
    } else {
      YM2612Config(config.ym2612);
    }
  }
  return success();
}

CoreResult CoreAdapter::audioSettings(CoreAudioSettings& output) const
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(false); !owner) {
    output = {};
    return owner;
  }
  output = private_->audioSettings;
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

std::uint64_t CoreAdapter::appliedInputSequence() const noexcept
{
  std::scoped_lock lock{coreMutex};
  return private_ == nullptr ? 0U : private_->appliedInputSequence;
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

void CoreAdapter::applyPendingInput() noexcept
{
  if (!private_->hasPendingInput) {
    return;
  }

  for (std::size_t slot = 0; slot < MAX_DEVICES; ++slot) {
    input.pad[slot] = 0;
    input.analog[slot][0] = 0;
    input.analog[slot][1] = 0;
  }

  std::size_t player = 0;
  for (std::size_t slot = 0;
       slot < MAX_DEVICES && player < InputSnapshot::maximumPlayers;
       ++slot) {
    if (input.dev[slot] == NO_DEVICE) {
      continue;
    }

    const auto& source = private_->pendingInput.players[player++];
    std::uint16_t coreButtons = 0;
    if (source.connected) {
      if (hasButton(source.buttons, InputButton::up)) {
        coreButtons |= INPUT_UP;
      }
      if (hasButton(source.buttons, InputButton::down)) {
        coreButtons |= INPUT_DOWN;
      }
      if (hasButton(source.buttons, InputButton::left)) {
        coreButtons |= INPUT_LEFT;
      }
      if (hasButton(source.buttons, InputButton::right)) {
        coreButtons |= INPUT_RIGHT;
      }
      if (hasButton(source.buttons, InputButton::a)) {
        coreButtons |= INPUT_A;
      }
      if (hasButton(source.buttons, InputButton::b)) {
        coreButtons |= INPUT_B;
      }
      if (hasButton(source.buttons, InputButton::c)) {
        coreButtons |= INPUT_C;
      }
      if (hasButton(source.buttons, InputButton::start)) {
        coreButtons |= INPUT_START;
      }
      if (hasButton(source.buttons, InputButton::x)) {
        coreButtons |= INPUT_X;
      }
      if (hasButton(source.buttons, InputButton::y)) {
        coreButtons |= INPUT_Y;
      }
      if (hasButton(source.buttons, InputButton::z)) {
        coreButtons |= INPUT_Z;
      }
      if (hasButton(source.buttons, InputButton::mode)) {
        coreButtons |= INPUT_MODE;
      }
      input.analog[slot][0] = source.analogX;
      input.analog[slot][1] = source.analogY;
    }
    input.pad[slot] = coreButtons;
  }
  private_->appliedInputSequence = private_->pendingInput.sequence;
  private_->hasPendingInput = false;
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
    private_->pendingInput = {};
    private_->appliedInputSequence = 0;
    private_->newestInputSequence = 0;
    private_->hasPendingInput = false;
  }
  if (state_ != CoreLifecycleState::uninitialized) {
    state_ = CoreLifecycleState::ready;
  }
}

void CoreAdapter::releaseOwnership() noexcept
{
  std::memset(&bitmap, 0, sizeof(bitmap));
  md_ntsc = nullptr;
  sms_ntsc = nullptr;
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
