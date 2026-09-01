#include "genplusgx/core_adapter.h"
#include "genplusgx/game_file.h"

#include "desktop_core_host.h"
#include "desktop_core_debug.h"

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
#include <cctype>
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

uint8 systemHardware(CoreSystemHardware hardware) noexcept
{
  switch (hardware) {
    case CoreSystemHardware::automatic: return 0U;
    case CoreSystemHardware::sg1000: return SYSTEM_SG;
    case CoreSystemHardware::sg1000II: return SYSTEM_SGII;
    case CoreSystemHardware::sg1000IIRamExtension: return SYSTEM_SGII_RAM_EXT;
    case CoreSystemHardware::markIII: return SYSTEM_MARKIII;
    case CoreSystemHardware::masterSystem: return SYSTEM_SMS;
    case CoreSystemHardware::masterSystemII: return SYSTEM_SMS2;
    case CoreSystemHardware::gameGear: return SYSTEM_GG;
    case CoreSystemHardware::genesis: return SYSTEM_MD;
  }
  return 0U;
}

void configureSystem(const CoreSystemSettings& settings) noexcept
{
  config.system = systemHardware(settings.hardware);
  config.region_detect = static_cast<uint8>(settings.region);
  config.vdp_mode = static_cast<uint8>(settings.videoStandard);
  config.master_clock = static_cast<uint8>(settings.masterClock);
  config.force_dtack = settings.emulateIllegalAccessLockups ? 0U : 1U;
  config.addr_error = settings.enableAddressErrors ? 1U : 0U;
}

std::size_t configuredDeviceCount(const CoreInputSettings& settings) noexcept
{
  return static_cast<std::size_t>(std::distance(
    settings.devices.begin(),
    std::ranges::find(settings.devices, CoreInputDevice::none)));
}

uint8 padType(CoreInputDevice device) noexcept
{
  if ((system_hw & SYSTEM_MD) == 0U) {
    return DEVICE_PAD2B;
  }
  return device == CoreInputDevice::pad3Button
    ? DEVICE_PAD3B : DEVICE_PAD6B;
}

uint8 portSystem(CoreInputDevice device, std::size_t port) noexcept
{
  switch (device) {
    case CoreInputDevice::none:
      return NO_SYSTEM;
    case CoreInputDevice::pad3Button:
    case CoreInputDevice::pad6Button:
    case CoreInputDevice::pico:
    case CoreInputDevice::terebiOekaki:
      return SYSTEM_GAMEPAD;
    case CoreInputDevice::segaMouse:
      return SYSTEM_MOUSE;
    case CoreInputDevice::lightGun:
      return (system_hw & SYSTEM_MD) != 0U && port == 1U
        ? SYSTEM_MENACER : SYSTEM_LIGHTPHASER;
    case CoreInputDevice::paddle:
      return SYSTEM_PADDLE;
    case CoreInputDevice::sportsPad:
      return SYSTEM_SPORTSPAD;
    case CoreInputDevice::xe1Ap:
      return SYSTEM_XE_1AP;
    case CoreInputDevice::graphicBoard:
      return SYSTEM_GRAPHIC_BOARD;
    case CoreInputDevice::activator:
      return SYSTEM_ACTIVATOR;
  }
  return NO_SYSTEM;
}

void configureInputPorts(const CoreInputSettings& settings) noexcept
{
  old_system[0] = -1;
  old_system[1] = -1;
  input.system[0] = NO_SYSTEM;
  input.system[1] = NO_SYSTEM;
  for (std::size_t player = 0U; player < MAX_INPUTS; ++player) {
    config.input[player].padtype =
      DEVICE_PAD2B | DEVICE_PAD3B | DEVICE_PAD6B;
  }

  const auto count = configuredDeviceCount(settings);
  for (std::size_t player = 0U; player < count; ++player) {
    if (isCorePad(settings.devices[player])) {
      config.input[player].padtype = padType(settings.devices[player]);
    }
  }

  if (count > 2U) {
    const uint8 multitap = (system_hw & SYSTEM_MD) != 0U
      ? SYSTEM_TEAMPLAYER : SYSTEM_MASTERTAP;
    input.system[0] = multitap;
    input.system[1] = count > 4U ? multitap : NO_SYSTEM;
    return;
  }

  if (count > 0U) {
    const bool genesisPortBLightGun =
      settings.devices[0] == CoreInputDevice::lightGun &&
      (system_hw & SYSTEM_MD) != 0U;
    if (genesisPortBLightGun) {
      input.system[1] = SYSTEM_MENACER;
    } else {
      input.system[0] = portSystem(settings.devices[0], 0U);
    }
  }
  if (count > 1U) {
    input.system[1] = portSystem(settings.devices[1], 1U);
  }
}

void trimUnusedMultitapDevices(const CoreInputSettings& settings) noexcept
{
  const auto count = configuredDeviceCount(settings);
  if (count == 1U) {
    if (settings.devices[0] == CoreInputDevice::pico) {
      input.dev[0] = DEVICE_PICO;
    } else if (settings.devices[0] == CoreInputDevice::terebiOekaki) {
      input.dev[0] = DEVICE_TEREBI;
    }
  }
  if (count <= 2U) {
    return;
  }
  for (std::size_t slot = count; slot < MAX_DEVICES; ++slot) {
    input.dev[slot] = NO_DEVICE;
    input.pad[slot] = 0U;
    input.analog[slot][0] = 0;
    input.analog[slot][1] = 0;
  }
}

bool copyHostPath(
  const std::filesystem::path& path,
  char* destination,
  std::size_t capacity)
{
  const auto value = path.string();
  if (value.size() >= capacity) {
    return false;
  }
  std::memset(destination, 0, capacity);
  std::memcpy(destination, value.data(), value.size());
  return true;
}

void configureFirmware(const CoreFirmwareSettings& settings)
{
  static_cast<void>(copyHostPath(
    settings.genesis, MD_BIOS, GENPLUSGX_HOST_PATH_CAPACITY));
  static_cast<void>(copyHostPath(
    settings.masterSystemUsa, MS_BIOS_US, GENPLUSGX_HOST_PATH_CAPACITY));
  static_cast<void>(copyHostPath(
    settings.masterSystemEurope, MS_BIOS_EU, GENPLUSGX_HOST_PATH_CAPACITY));
  static_cast<void>(copyHostPath(
    settings.masterSystemJapan, MS_BIOS_JP, GENPLUSGX_HOST_PATH_CAPACITY));
  static_cast<void>(copyHostPath(
    settings.gameGear, GG_BIOS, GENPLUSGX_HOST_PATH_CAPACITY));
  static_cast<void>(copyHostPath(
    settings.segaCdUsa, CD_BIOS_US, GENPLUSGX_HOST_PATH_CAPACITY));
  static_cast<void>(copyHostPath(
    settings.segaCdEurope, CD_BIOS_EU, GENPLUSGX_HOST_PATH_CAPACITY));
  static_cast<void>(copyHostPath(
    settings.segaCdJapan, CD_BIOS_JP, GENPLUSGX_HOST_PATH_CAPACITY));

  system_bios = 0U;
  std::memset(boot_rom, 0xFF, sizeof(boot_rom));
  if (!settings.genesis.empty() &&
      load_archive(MD_BIOS, boot_rom, 0x800, nullptr) == 0x800 &&
      std::memcmp(boot_rom + 0x120, "GENESIS OS", 10U) == 0) {
    system_bios |= SYSTEM_MD;
#ifdef LSB_FIRST
    for (std::size_t index = 0U; index < 0x800U; index += 2U) {
      std::swap(boot_rom[index], boot_rom[index + 1U]);
    }
#endif
    for (std::size_t index = 0x800U; index < sizeof(boot_rom); ++index) {
      boot_rom[index] = boot_rom[index & 0x7FFU];
    }
  }

  const bool cartridgeFirmwareConfigured =
    !settings.genesis.empty() || !settings.masterSystemUsa.empty() ||
    !settings.masterSystemEurope.empty() ||
    !settings.masterSystemJapan.empty() || !settings.gameGear.empty();
  // Bit 0 enables boot firmware. Bit 1 keeps the cartridge mapped so firmware
  // can hand control to it; this is the normal standalone-emulator workflow.
  config.bios = cartridgeFirmwareConfigured ? 3U : 0U;
}

CoreDiscRegion discRegion() noexcept
{
  switch (region_code) {
    case REGION_USA:
      return CoreDiscRegion::usa;
    case REGION_EUROPE:
      return CoreDiscRegion::europe;
    case REGION_JAPAN_NTSC:
    case REGION_JAPAN_PAL:
      return CoreDiscRegion::japan;
    default:
      return CoreDiscRegion::unknown;
  }
}

const std::filesystem::path& firmwareForRegion(
  const CoreFirmwareSettings& settings,
  CoreDiscRegion region) noexcept
{
  switch (region) {
    case CoreDiscRegion::usa:
      return settings.segaCdUsa;
    case CoreDiscRegion::europe:
      return settings.segaCdEurope;
    case CoreDiscRegion::japan:
    case CoreDiscRegion::unknown:
      return settings.segaCdJapan;
  }
  return settings.segaCdJapan;
}

const char* discRegionName(CoreDiscRegion region) noexcept
{
  switch (region) {
    case CoreDiscRegion::usa:
      return "USA";
    case CoreDiscRegion::europe:
      return "Europe";
    case CoreDiscRegion::japan:
      return "Japan";
    case CoreDiscRegion::unknown:
      return "unknown";
  }
  return "unknown";
}

std::string lowercaseExtension(const std::filesystem::path& path)
{
  auto extension = path.extension().string();
  std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  return extension;
}

bool requiresDiscImage(const std::filesystem::path& path)
{
  const auto extension = lowercaseExtension(path);
  return extension == ".cue" || extension == ".iso" || extension == ".chd";
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
  CoreSystemSettings systemSettings;
  CoreFirmwareSettings firmwareSettings;
  CoreInputSettings inputSettings;
  std::filesystem::path discPath;
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

  if (const auto validation = validateGameFile(path); !validation) {
    return failure(requiresDiscImage(path) ? CoreError::invalidDiscImage
                                          : CoreError::invalidPath,
      validation.message);
  }
  const std::string nativePath = path.string();

  if (state_ == CoreLifecycleState::loaded) {
    unloadUnchecked();
  }
  configureSystem(private_->systemSettings);
  configureFirmware(private_->firmwareSettings);

  std::vector<char> mutablePath(nativePath.begin(), nativePath.end());
  mutablePath.push_back('\0');
  if (load_rom(mutablePath.data()) == 0) {
    const bool segaCdDetected = system_hw == SYSTEM_MCD;
    const auto detectedRegion = discRegion();
    const auto firmwarePath = firmwareForRegion(
      private_->firmwareSettings, detectedRegion);
    unloadUnchecked();
    if (segaCdDetected) {
      return failure(CoreError::missingFirmware,
        "A readable 128 KiB Sega CD / Mega CD BIOS is required for the detected " +
          std::string{discRegionName(detectedRegion)} +
          " region. Configure a valid user-supplied file in BIOS Settings" +
          (firmwarePath.empty() ? std::string{"."}
                                : std::string{"; the configured file could not be loaded."}));
    }
    return failure(CoreError::loadFailed, "Genesis Plus GX could not load the selected game image.");
  }

  if (requiresDiscImage(path) && system_hw != SYSTEM_MCD) {
    unloadUnchecked();
    return failure(CoreError::invalidDiscImage,
      "The selected file did not contain a supported Sega CD / Mega CD data track.");
  }

  if (audio_init(audioSampleRate_, 0.0) != 0) {
    unloadUnchecked();
    return failure(CoreError::audioInitializationFailed, "Genesis Plus GX could not initialize its audio resamplers.");
  }

  configureInputPorts(private_->inputSettings);
  system_init();
  trimUnusedMultitapDevices(private_->inputSettings);
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
  private_->discPath = system_hw == SYSTEM_MCD && cdd.loaded != 0
    ? path : std::filesystem::path{};
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
  return loadRawState(state, 0U);
}

CoreResult CoreAdapter::loadRawState(
  std::span<const std::uint8_t> state,
  std::uint64_t emulatedFrameNumber)
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
  frameCount_ = emulatedFrameNumber;
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

CoreResult CoreAdapter::applyCheats(
  std::span<const CoreCheatPatch> patches)
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    return owner;
  }
  if (patches.size() > maximumCoreCheatPatches ||
      !std::ranges::all_of(patches, validateCoreCheatPatch)) {
    return failure(
      CoreError::invalidCheats,
      "The cheat patch set exceeds the core limit or contains invalid data.");
  }

  std::array<genplusgx_host_cheat, maximumCoreCheatPatches> hostCheats{};
  for (std::size_t index = 0; index < patches.size(); ++index) {
    const auto& patch = patches[index];
    hostCheats[index] = {
      .address = patch.address,
      .data = patch.data,
      .reference = patch.reference,
      .width = static_cast<std::uint8_t>(patch.width),
      .reference_required = static_cast<std::uint8_t>(
        patch.referenceRequired ? 1U : 0U),
    };
  }
  if (genplusgx_host_set_cheats(hostCheats.data(), patches.size()) == 0) {
    return failure(
      CoreError::invalidCheats,
      "Genesis Plus GX rejected the cheat patch set.");
  }
  return success();
}

CoreResult CoreAdapter::applyFirmwareSettings(
  const CoreFirmwareSettings& settings)
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(false); !owner) {
    return owner;
  }
  if (!validateCoreFirmwareSettings(settings)) {
    return failure(CoreError::invalidSettings,
      "The requested firmware settings contain a path that exceeds the core host boundary.");
  }
  private_->firmwareSettings = settings;
  return success();
}

CoreResult CoreAdapter::firmwareSettings(CoreFirmwareSettings& output) const
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(false); !owner) {
    output = {};
    return owner;
  }
  output = private_->firmwareSettings;
  return success();
}

CoreResult CoreAdapter::applyInputSettings(const CoreInputSettings& settings)
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(false); !owner) {
    return owner;
  }
  if (!validateCoreInputSettings(settings)) {
    return failure(CoreError::invalidSettings,
      "The requested emulated input-device layout is invalid.");
  }
  private_->inputSettings = settings;
  if (state_ == CoreLifecycleState::loaded) {
    configureInputPorts(private_->inputSettings);
    io_init();
    trimUnusedMultitapDevices(private_->inputSettings);
    input_reset();
    private_->pendingInput = {};
    private_->hasPendingInput = false;
  }
  return success();
}

CoreResult CoreAdapter::inputSettings(CoreInputSettings& output) const
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(false); !owner) {
    output = {};
    return owner;
  }
  output = private_->inputSettings;
  return success();
}

CoreResult CoreAdapter::discInfo(CoreDiscInfo& output) const
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    output = {};
    return owner;
  }
  output = {
    .segaCd = system_hw == SYSTEM_MCD,
    .trayOpen = system_hw == SYSTEM_MCD && cdd.status == CD_OPEN,
    .discPresent = system_hw == SYSTEM_MCD && cdd.loaded != 0,
    .trackCount = system_hw == SYSTEM_MCD && cdd.toc.last > 0
      ? static_cast<std::uint32_t>(cdd.toc.last) : 0U,
    .region = system_hw == SYSTEM_MCD ? discRegion() : CoreDiscRegion::unknown,
    .path = private_->discPath,
  };
  return success();
}

CoreResult CoreAdapter::setDiscEjected(bool ejected)
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    return owner;
  }
  if (system_hw != SYSTEM_MCD) {
    return failure(CoreError::notSegaCd,
      "Disc operations require an active Sega CD / Mega CD session.");
  }
  if (ejected) {
    cdd.status = CD_OPEN;
    scd.regs[0x36U >> 1U].byte.h = 0x01U;
  } else if (cdd.status == CD_OPEN) {
    cdd.status = cdd.loaded != 0 ? CD_TOC : NO_DISC;
  }
  return success();
}

CoreResult CoreAdapter::changeDisc(const std::filesystem::path& path)
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    return owner;
  }
  if (system_hw != SYSTEM_MCD) {
    return failure(CoreError::notSegaCd,
      "Disc operations require an active Sega CD / Mega CD session.");
  }
  if (const auto validation = validateDiscImageFile(path); !validation) {
    return failure(CoreError::invalidDiscImage, validation.message);
  }
  const auto nativePath = path.string();

  cdd.status = CD_OPEN;
  scd.regs[0x36U >> 1U].byte.h = 0x01U;
  std::array<char, 0x210U> header{};
  std::vector<char> mutablePath(nativePath.begin(), nativePath.end());
  mutablePath.push_back('\0');
  if (cdd_load(mutablePath.data(), header.data()) <= 0 || cdd.loaded == 0) {
    private_->discPath.clear();
    cdd.status = CD_OPEN;
    return failure(CoreError::invalidDiscImage,
      "Genesis Plus GX could not mount the replacement Sega CD / Mega CD image.");
  }
  private_->discPath = path;
  cdd.status = CD_TOC;
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
  config.psg_preamp = static_cast<int16>(settings.psgLevelPercent);
  config.fm_preamp = static_cast<int16>(settings.fmLevelPercent);
  config.cdda_volume = static_cast<int16>(settings.cddaLevelPercent);
  config.pcm_volume = static_cast<int16>(settings.pcmLevelPercent);
  config.lp_range = static_cast<uint32>(
    (static_cast<std::uint64_t>(settings.lowPassPercent) * 65'536U) / 100U);
  config.lg = static_cast<int16>(settings.equalizerLowPercent);
  config.mg = static_cast<int16>(settings.equalizerMidPercent);
  config.hg = static_cast<int16>(settings.equalizerHighPercent);
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
  const auto psgPanning = (system_hw & SYSTEM_PBC) == SYSTEM_MD
    ? 0xffU : static_cast<unsigned int>(io_reg[6]);
  psg_config(0U, static_cast<unsigned int>(settings.psgLevelPercent),
    psgPanning);

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

CoreResult CoreAdapter::applySystemSettings(const CoreSystemSettings& settings)
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(false); !owner) {
    return owner;
  }
  if (!validateCoreSystemSettings(settings)) {
    return failure(CoreError::invalidSettings,
      "The requested core system settings contain an unsupported value.");
  }
  private_->systemSettings = settings;
  if (state_ != CoreLifecycleState::loaded) {
    configureSystem(settings);
  }
  return success();
}

CoreResult CoreAdapter::systemSettings(CoreSystemSettings& output) const
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(false); !owner) {
    output = {};
    return owner;
  }
  output = private_->systemSettings;
  return success();
}

CoreResult CoreAdapter::debugRequest(
  const CoreDebugRequest& request,
  CoreDebugResponse& response)
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    response = {};
    return owner;
  }

  response = {
    .type = request.type,
    .region = request.region,
    .offset = request.offset,
    .snapshot = {},
    .bytes = {},
    .breakpointHit = {},
  };
  const auto region = static_cast<unsigned int>(request.region);
  constexpr auto maximumRegion = static_cast<unsigned int>(
    CoreDebugMemoryRegion::vdpRegisters);

  switch (request.type) {
    case CoreDebugRequestType::captureSnapshot: {
      auto raw = std::make_unique<genplusgx_debug_snapshot>();
      if (genplusgx_debug_capture(raw.get()) == 0) {
        return failure(
          CoreError::debugUnavailable, "Core debug state is unavailable.");
      }
      auto snapshot = std::make_shared<CoreDebugSnapshot>();
      snapshot->frameNumber = frameCount_;
      snapshot->hardware = raw->hardware;
      snapshot->romSize = raw->rom_size;
      snapshot->m68kActive = raw->m68k_active != 0;
      std::ranges::copy(raw->m68k.data, snapshot->m68k.data.begin());
      std::ranges::copy(raw->m68k.address, snapshot->m68k.address.begin());
      snapshot->m68k.programCounter = raw->m68k.program_counter;
      snapshot->m68k.status = raw->m68k.status;
      snapshot->m68k.userStackPointer = raw->m68k.user_stack_pointer;
      snapshot->m68k.interruptStackPointer =
        raw->m68k.interrupt_stack_pointer;
      snapshot->z80 = {
        .af = raw->z80.af,
        .bc = raw->z80.bc,
        .de = raw->z80.de,
        .hl = raw->z80.hl,
        .afAlternate = raw->z80.af_alternate,
        .bcAlternate = raw->z80.bc_alternate,
        .deAlternate = raw->z80.de_alternate,
        .hlAlternate = raw->z80.hl_alternate,
        .ix = raw->z80.ix,
        .iy = raw->z80.iy,
        .stackPointer = raw->z80.stack_pointer,
        .programCounter = raw->z80.program_counter,
        .interruptVector = raw->z80.interrupt_vector,
        .refresh = raw->z80.refresh,
        .interruptMode = raw->z80.interrupt_mode,
        .interruptFlipFlop1 = raw->z80.interrupt_flip_flop_1 != 0,
        .interruptFlipFlop2 = raw->z80.interrupt_flip_flop_2 != 0,
        .halted = raw->z80.halted != 0,
        .bank = raw->z80.bank,
      };
      std::ranges::copy(
        raw->vdp_registers, snapshot->vdp.registers.begin());
      snapshot->vdp.status = raw->vdp_status;
      snapshot->vdp.dmaLength = raw->dma_length;
      snapshot->vdp.dmaSource = raw->dma_source;
      snapshot->vdp.dmaType = raw->dma_type;
      snapshot->vdp.horizontalCounter = raw->horizontal_counter;
      snapshot->vdp.verticalCounter = raw->vertical_counter;
      snapshot->vdp.pal = raw->pal != 0;
      snapshot->vdp.interlaced = raw->interlaced != 0;
      snapshot->vdp.oddField = raw->odd_field != 0;
      std::ranges::copy(raw->vram, snapshot->vdp.vram.begin());
      std::ranges::copy(raw->cram, snapshot->vdp.cram.begin());
      std::ranges::copy(raw->vsram, snapshot->vdp.vsram.begin());
      std::ranges::copy(
        raw->sprite_table, snapshot->vdp.spriteTable.begin());
      for (std::size_t bank = 0U;
           bank < snapshot->sound.fmRegisters.size(); ++bank) {
        std::ranges::copy(raw->fm_registers[bank],
          snapshot->sound.fmRegisters[bank].begin());
      }
      std::ranges::copy(
        raw->psg_registers, snapshot->sound.psgRegisters.begin());
      std::ranges::copy(
        raw->input_buttons, snapshot->input.buttons.begin());
      for (std::size_t player = 0U;
           player < snapshot->input.analog.size(); ++player) {
        std::ranges::copy(
          raw->input_analog[player], snapshot->input.analog[player].begin());
      }
      std::ranges::copy(raw->m68k_ram, snapshot->m68kRam.begin());
      std::ranges::copy(raw->z80_ram, snapshot->z80Ram.begin());
      response.snapshot = std::move(snapshot);
      return success();
    }
    case CoreDebugRequestType::readMemory: {
      if (region > maximumRegion || request.size == 0U ||
          request.size > maximumDebugTransferBytes) {
        return failure(CoreError::invalidDebugRequest,
          "The debug memory read must target a known region and at most 4096 bytes.");
      }
      response.bytes.resize(request.size);
      if (genplusgx_debug_read_region(region, request.offset,
            response.bytes.data(), response.bytes.size()) == 0) {
        response.bytes.clear();
        return failure(CoreError::invalidDebugRequest,
          "The debug memory read is outside the selected region.");
      }
      return success();
    }
    case CoreDebugRequestType::writeMemory:
      if (region > maximumRegion || request.bytes.empty() ||
          request.bytes.size() > maximumDebugTransferBytes ||
          genplusgx_debug_write_region(region, request.offset,
            request.bytes.data(), request.bytes.size()) == 0) {
        return failure(CoreError::invalidDebugRequest,
          "The debug memory write is empty, too large, or outside the selected region.");
      }
      return success();
    case CoreDebugRequestType::setM68kRegisters: {
      genplusgx_debug_m68k_registers registers{};
      std::ranges::copy(request.m68k.data, registers.data);
      std::ranges::copy(request.m68k.address, registers.address);
      registers.program_counter = request.m68k.programCounter;
      registers.status = request.m68k.status;
      registers.user_stack_pointer = request.m68k.userStackPointer;
      registers.interrupt_stack_pointer = request.m68k.interruptStackPointer;
      if (genplusgx_debug_set_m68k_registers(&registers) == 0) {
        return failure(
          CoreError::invalidDebugRequest, "The 68K registers were rejected.");
      }
      return success();
    }
    case CoreDebugRequestType::setZ80Registers: {
      genplusgx_debug_z80_registers registers{
        .af = request.z80.af,
        .bc = request.z80.bc,
        .de = request.z80.de,
        .hl = request.z80.hl,
        .af_alternate = request.z80.afAlternate,
        .bc_alternate = request.z80.bcAlternate,
        .de_alternate = request.z80.deAlternate,
        .hl_alternate = request.z80.hlAlternate,
        .ix = request.z80.ix,
        .iy = request.z80.iy,
        .stack_pointer = request.z80.stackPointer,
        .program_counter = request.z80.programCounter,
        .interrupt_vector = request.z80.interruptVector,
        .refresh = request.z80.refresh,
        .interrupt_mode = request.z80.interruptMode,
        .interrupt_flip_flop_1 = static_cast<std::uint8_t>(
          request.z80.interruptFlipFlop1 ? 1U : 0U),
        .interrupt_flip_flop_2 = static_cast<std::uint8_t>(
          request.z80.interruptFlipFlop2 ? 1U : 0U),
        .halted = static_cast<std::uint8_t>(
          request.z80.halted ? 1U : 0U),
        .bank = request.z80.bank,
      };
      if (registers.interrupt_mode > 2U ||
          genplusgx_debug_set_z80_registers(&registers) == 0) {
        return failure(
          CoreError::invalidDebugRequest, "The Z80 registers were rejected.");
      }
      return success();
    }
    case CoreDebugRequestType::setVdpRegister:
      if (genplusgx_debug_set_vdp_register(
            request.vdpRegister, request.vdpValue) == 0) {
        return failure(CoreError::invalidDebugRequest,
          "The VDP register index is outside the 32-register file.");
      }
      return success();
    case CoreDebugRequestType::setFrameBreakpoints:
      return failure(CoreError::invalidDebugRequest,
        "Frame breakpoints are owned by the emulation worker.");
  }
  return failure(
    CoreError::invalidDebugRequest, "The debug request type is invalid.");
}

CoreResult CoreAdapter::debugProgramCounters(
  CoreDebugProgramCounters& output) const
{
  std::scoped_lock lock{coreMutex};
  if (const auto owner = requireOwner(true); !owner) {
    output = {};
    return owner;
  }
  genplusgx_debug_program_counters counters{};
  if (genplusgx_debug_get_program_counters(&counters) == 0) {
    output = {};
    return failure(
      CoreError::debugUnavailable, "Core program counters are unavailable.");
  }
  output = {
    .m68kActive = counters.m68k_active != 0,
    .m68k = counters.m68k,
    .z80 = counters.z80,
  };
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

CoreResourceMetrics CoreAdapter::resourceMetrics() const
{
  std::scoped_lock lock{coreMutex};
  if (private_ == nullptr ||
      private_->ownerThread != std::this_thread::get_id()) {
    return {};
  }
  return {
    .framebufferCapacityBytes = private_->framebuffer.capacity(),
    .audioScratchCapacityFrames = private_->audioScratch.capacity() / 2U,
    .stateScratchCapacityBytes = private_->stateScratch.capacity(),
    .stateLoadScratchCapacityBytes = private_->stateLoadScratch.capacity(),
  };
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
  genplusgx_host_clear_cheats();
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
    private_->discPath.clear();
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
