#include "genplusgx/audio_output.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

namespace genplusgx {
namespace {

constexpr int minimumSampleRate = 8'000;
constexpr int maximumSampleRate = 192'000;
constexpr auto minimumLatency = std::chrono::milliseconds{10};
constexpr auto maximumLatency = std::chrono::milliseconds{500};
constexpr std::size_t callbackScratchFrames = 1'024U;
constexpr int minimumVolumePercent = 0;
constexpr int maximumVolumePercent = 100;

AudioOutputStatus success()
{
  return {};
}

AudioOutputStatus failure(AudioOutputError error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

std::string sdlFailureMessage(std::string prefix)
{
  const char* detail = SDL_GetError();
  if (detail != nullptr && detail[0] != '\0') {
    prefix += ": ";
    prefix += detail;
  }
  return prefix;
}

} // namespace

std::size_t audioRingCapacityFrames(const AudioOutputConfig& config) noexcept
{
  if (config.sampleRate < minimumSampleRate ||
      config.sampleRate > maximumSampleRate ||
      config.latency < minimumLatency || config.latency > maximumLatency ||
      config.volumePercent < minimumVolumePercent ||
      config.volumePercent > maximumVolumePercent) {
    return 0U;
  }
  const auto frames =
    (static_cast<std::uint64_t>(config.sampleRate) *
       static_cast<std::uint64_t>(config.latency.count()) +
     999U) /
    1'000U;
  if (frames > std::numeric_limits<std::size_t>::max()) {
    return 0U;
  }
  return static_cast<std::size_t>(frames);
}

void applyAudioOutputGain(
  std::span<StereoAudioFrame> frames,
  int volumePercent,
  bool muted) noexcept
{
  const int gain = std::clamp(
    volumePercent, minimumVolumePercent, maximumVolumePercent);
  if (muted || gain == 0) {
    std::ranges::fill(frames, StereoAudioFrame{});
    return;
  }
  if (gain == maximumVolumePercent) {
    return;
  }
  for (auto& frame : frames) {
    frame.left = static_cast<std::int16_t>(
      (static_cast<std::int32_t>(frame.left) * gain) / 100);
    frame.right = static_cast<std::int16_t>(
      (static_cast<std::int32_t>(frame.right) * gain) / 100);
  }
}

std::vector<AudioOutputDevice> availableAudioOutputDevices()
{
  const bool ownedSubsystem = (SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0U;
  if (ownedSubsystem && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    return {};
  }
  int count = 0;
  SDL_AudioDeviceID* devices = SDL_GetAudioPlaybackDevices(&count);
  std::vector<AudioOutputDevice> result;
  if (devices != nullptr && count > 0) {
    result.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
      const char* name = SDL_GetAudioDeviceName(devices[index]);
      if (name != nullptr) {
        result.push_back({
          .id = static_cast<std::uint32_t>(devices[index]),
          .name = name,
        });
      }
    }
  }
  SDL_free(devices);
  if (ownedSubsystem) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }
  return result;
}

class AudioOutput::Private final {
public:
  explicit Private(AudioOutputConfig config)
    : config_(config),
      ring_(std::make_shared<StereoAudioRingBuffer>(
        std::max<std::size_t>(audioRingCapacityFrames(config), 1U))),
      volumePercent_(config.volumePercent), muted_(config.muted)
  {
  }

  ~Private()
  {
    static_cast<void>(shutdown());
  }

  AudioOutputStatus initialize()
  {
    std::scoped_lock lock{controlMutex_};
    if (initialized_.load(std::memory_order_acquire)) {
      return failure(
        AudioOutputError::alreadyInitialized,
        "The host audio output is already initialized.");
    }
    if (audioRingCapacityFrames(config_) == 0U) {
      return failure(
        AudioOutputError::invalidConfiguration,
        "Audio sample rate must be 8000-192000 Hz and latency must be 10-500 ms.");
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
      return failure(
        AudioOutputError::subsystemInitializationFailed,
        sdlFailureMessage("SDL could not initialize its audio subsystem"));
    }
    ownsAudioSubsystem_ = true;

    SDL_AudioSpec sourceSpec{};
    sourceSpec.format = SDL_AUDIO_S16;
    sourceSpec.channels = 2;
    sourceSpec.freq = config_.sampleRate;
    const SDL_AudioDeviceID device = config_.deviceId == 0U
      ? SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK
      : static_cast<SDL_AudioDeviceID>(config_.deviceId);
    stream_ = SDL_OpenAudioDeviceStream(
      device, &sourceSpec, &Private::audioCallback, this);
    if (stream_ == nullptr) {
      const auto message = sdlFailureMessage("SDL could not open the audio output");
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
      ownsAudioSubsystem_ = false;
      return failure(AudioOutputError::deviceOpenFailed, message);
    }

    const auto openedDevice = SDL_GetAudioStreamDevice(stream_);
    const char* openedName = openedDevice == 0U
      ? nullptr
      : SDL_GetAudioDeviceName(openedDevice);
    deviceName_ = openedName == nullptr ? "Default playback device" : openedName;
    ring_->clear();
    ring_->resetMetrics();
    resetCallbackMetrics();
    paused_.store(true, std::memory_order_release);
    initialized_.store(true, std::memory_order_release);
    return success();
  }

  AudioOutputStatus pause()
  {
    std::scoped_lock lock{controlMutex_};
    if (!initialized_.load(std::memory_order_acquire) || stream_ == nullptr) {
      return failure(
        AudioOutputError::notInitialized,
        "The host audio output is not initialized.");
    }
    if (!SDL_PauseAudioStreamDevice(stream_)) {
      return failure(
        AudioOutputError::deviceControlFailed,
        sdlFailureMessage("SDL could not pause the audio output"));
    }
    paused_.store(true, std::memory_order_release);
    if (!SDL_ClearAudioStream(stream_)) {
      return failure(
        AudioOutputError::deviceControlFailed,
        sdlFailureMessage("SDL could not clear buffered audio"));
    }
    ring_->clear();
    return success();
  }

  AudioOutputStatus resume()
  {
    std::scoped_lock lock{controlMutex_};
    if (!initialized_.load(std::memory_order_acquire) || stream_ == nullptr) {
      return failure(
        AudioOutputError::notInitialized,
        "The host audio output is not initialized.");
    }
    if (!SDL_ResumeAudioStreamDevice(stream_)) {
      return failure(
        AudioOutputError::deviceControlFailed,
        sdlFailureMessage("SDL could not resume the audio output"));
    }
    paused_.store(false, std::memory_order_release);
    return success();
  }

  AudioOutputStatus shutdown()
  {
    std::scoped_lock lock{controlMutex_};
    if (stream_ != nullptr) {
      static_cast<void>(SDL_PauseAudioStreamDevice(stream_));
      SDL_DestroyAudioStream(stream_);
      stream_ = nullptr;
    }
    if (ownsAudioSubsystem_) {
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
      ownsAudioSubsystem_ = false;
    }
    ring_->clear();
    deviceName_.clear();
    paused_.store(true, std::memory_order_release);
    initialized_.store(false, std::memory_order_release);
    return success();
  }

  AudioOutputStatus setVolumePercent(int volumePercent)
  {
    if (volumePercent < minimumVolumePercent ||
        volumePercent > maximumVolumePercent) {
      return failure(AudioOutputError::invalidConfiguration,
        "Audio volume must be between 0 and 100 percent.");
    }
    volumePercent_.store(volumePercent, std::memory_order_release);
    return success();
  }

  AudioOutputMetrics metrics() const noexcept
  {
    AudioOutputMetrics snapshot;
    for (;;) {
      const auto before = metricsGeneration_.load(std::memory_order_acquire);
      if ((before & 1U) != 0U) {
        continue;
      }
      snapshot.callbackCount = callbackCount_.load(std::memory_order_relaxed);
      snapshot.requestedFrames = requestedFrames_.load(std::memory_order_relaxed);
      snapshot.suppliedFrames = suppliedFrames_.load(std::memory_order_relaxed);
      snapshot.silenceFrames = silenceFrames_.load(std::memory_order_relaxed);
      snapshot.submissionFailures =
        submissionFailures_.load(std::memory_order_relaxed);
      const auto after = metricsGeneration_.load(std::memory_order_acquire);
      if (before == after) {
        break;
      }
    }
    snapshot.ring = ring_->metrics();
    return snapshot;
  }

  std::string deviceName() const
  {
    std::scoped_lock lock{controlMutex_};
    return deviceName_;
  }

  static void SDLCALL audioCallback(
    void* userdata,
    SDL_AudioStream* stream,
    int additionalAmount,
    int)
  {
    if (userdata == nullptr || stream == nullptr || additionalAmount <= 0) {
      return;
    }
    static_cast<Private*>(userdata)->provideAudio(stream, additionalAmount);
  }

  void provideAudio(SDL_AudioStream* stream, int additionalAmount) noexcept
  {
    constexpr std::size_t bytesPerFrame = sizeof(StereoAudioFrame);
    auto framesRemaining =
      (static_cast<std::size_t>(additionalAmount) + bytesPerFrame - 1U) /
      bytesPerFrame;
    metricsGeneration_.fetch_add(1U, std::memory_order_acq_rel);
    callbackCount_.fetch_add(1U, std::memory_order_relaxed);
    requestedFrames_.fetch_add(framesRemaining, std::memory_order_relaxed);

    while (framesRemaining > 0U) {
      const auto chunkFrames = std::min(framesRemaining, callbackScratch_.size());
      auto chunk = std::span<StereoAudioFrame>{callbackScratch_}.first(chunkFrames);
      const auto read = ring_->read(chunk);
      std::fill(chunk.begin() + static_cast<std::ptrdiff_t>(read.providedFrames),
        chunk.end(), StereoAudioFrame{});
      applyAudioOutputGain(chunk,
        volumePercent_.load(std::memory_order_acquire),
        muted_.load(std::memory_order_acquire));
      suppliedFrames_.fetch_add(read.providedFrames, std::memory_order_relaxed);
      silenceFrames_.fetch_add(read.missingFrames, std::memory_order_relaxed);
      const auto byteCount = static_cast<int>(chunkFrames * bytesPerFrame);
      if (!SDL_PutAudioStreamData(stream, chunk.data(), byteCount)) {
        submissionFailures_.fetch_add(1U, std::memory_order_relaxed);
        metricsGeneration_.fetch_add(1U, std::memory_order_release);
        return;
      }
      framesRemaining -= chunkFrames;
    }
    metricsGeneration_.fetch_add(1U, std::memory_order_release);
  }

  void resetCallbackMetrics() noexcept
  {
    metricsGeneration_.fetch_add(1U, std::memory_order_acq_rel);
    callbackCount_.store(0U, std::memory_order_relaxed);
    requestedFrames_.store(0U, std::memory_order_relaxed);
    suppliedFrames_.store(0U, std::memory_order_relaxed);
    silenceFrames_.store(0U, std::memory_order_relaxed);
    submissionFailures_.store(0U, std::memory_order_relaxed);
    metricsGeneration_.fetch_add(1U, std::memory_order_release);
  }

  AudioOutputConfig config_;
  std::shared_ptr<StereoAudioRingBuffer> ring_;
  mutable std::mutex controlMutex_;
  SDL_AudioStream* stream_{nullptr};
  std::string deviceName_;
  std::array<StereoAudioFrame, callbackScratchFrames> callbackScratch_{};
  std::atomic<bool> initialized_{false};
  std::atomic<bool> paused_{true};
  std::atomic<int> volumePercent_{100};
  std::atomic<bool> muted_{false};
  std::atomic<std::uint64_t> metricsGeneration_{0};
  std::atomic<std::uint64_t> callbackCount_{0};
  std::atomic<std::uint64_t> requestedFrames_{0};
  std::atomic<std::uint64_t> suppliedFrames_{0};
  std::atomic<std::uint64_t> silenceFrames_{0};
  std::atomic<std::uint64_t> submissionFailures_{0};
  bool ownsAudioSubsystem_{false};
};

AudioOutput::AudioOutput(AudioOutputConfig config)
  : private_(std::make_unique<Private>(config))
{
}

AudioOutput::~AudioOutput() = default;

AudioOutputStatus AudioOutput::initialize()
{
  return private_->initialize();
}

AudioOutputStatus AudioOutput::pause()
{
  return private_->pause();
}

AudioOutputStatus AudioOutput::resume()
{
  return private_->resume();
}

AudioOutputStatus AudioOutput::shutdown()
{
  return private_->shutdown();
}

AudioOutputStatus AudioOutput::setVolumePercent(int volumePercent)
{
  return private_->setVolumePercent(volumePercent);
}

void AudioOutput::setMuted(bool muted) noexcept
{
  private_->muted_.store(muted, std::memory_order_release);
}

bool AudioOutput::isInitialized() const noexcept
{
  return private_->initialized_.load(std::memory_order_acquire);
}

bool AudioOutput::isPaused() const noexcept
{
  return private_->paused_.load(std::memory_order_acquire);
}

AudioOutputConfig AudioOutput::config() const noexcept
{
  return private_->config_;
}

std::string AudioOutput::deviceName() const
{
  return private_->deviceName();
}

int AudioOutput::volumePercent() const noexcept
{
  return private_->volumePercent_.load(std::memory_order_acquire);
}

bool AudioOutput::isMuted() const noexcept
{
  return private_->muted_.load(std::memory_order_acquire);
}

AudioOutputMetrics AudioOutput::metrics() const noexcept
{
  return private_->metrics();
}

std::shared_ptr<StereoAudioRingBuffer> AudioOutput::ringBuffer() const noexcept
{
  return private_->ring_;
}

} // namespace genplusgx
