#include "genplusgx/emulation_worker.h"
#include "synthetic_rom.h"

extern "C" {
#include "teamplayer.h"
}

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace std::chrono_literals;

bool check(bool condition, const std::string& message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::optional<genplusgx::EmulationEvent> waitForOperation(
  genplusgx::EmulationWorker& worker,
  std::uint64_t operationId)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = worker.waitForEvent(100ms);
    if (event && event->operationId == operationId) {
      return event;
    }
  }
  return std::nullopt;
}

std::optional<genplusgx::EmulationEvent> waitForFrame(
  genplusgx::EmulationWorker& worker,
  bool rewinding)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = worker.waitForEvent(100ms);
    if (event && event->type == genplusgx::EmulationEventType::frameCompleted &&
        event->rewinding == rewinding) {
      return event;
    }
  }
  return std::nullopt;
}

bool waitForRewindAvailability(genplusgx::EmulationWorker& worker)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (worker.metrics().rewindAvailable) {
      return true;
    }
    static_cast<void>(worker.waitForEvent(50ms));
  }
  return worker.metrics().rewindAvailable;
}

bool submitAndSucceed(
  genplusgx::EmulationWorker& worker,
  genplusgx::EmulationCommand command,
  genplusgx::EmulationEvent& event)
{
  const auto operationId = command.operationId;
  if (!worker.submit(std::move(command))) {
    return false;
  }
  auto result = waitForOperation(worker, operationId);
  if (!result || !result->succeeded()) {
    return false;
  }
  event = std::move(*result);
  return true;
}

struct Baseline final {
  std::vector<std::uint8_t> firstState;
  std::uint64_t firstVideoHash{0U};
  std::uint64_t secondVideoHash{0U};
  std::uint64_t thirdVideoHash{0U};
  std::uint64_t firstAudioHash{0U};
  std::size_t firstAudioFrames{0U};
};

bool copyAdapterFrame(
  genplusgx::CoreAdapter& adapter,
  std::vector<std::uint16_t>& pixels,
  std::uint64_t& videoHash,
  std::uint64_t* audioHash = nullptr,
  std::size_t* audioFrames = nullptr)
{
  genplusgx::CoreVideoFrameInfo video;
  if (!adapter.copyVideoFrame(pixels, video)) {
    return false;
  }
  videoHash = genplusgx::hashVideoFrame(
    std::span<const std::uint16_t>{pixels}.first(video.pixelCount()));
  genplusgx::CoreAudioBatchInfo audio;
  if (!adapter.audioBatchInfo(audio)) {
    return false;
  }
  std::vector<genplusgx::StereoAudioFrame> samples(audio.frameCount);
  if (!adapter.copyAudioFrames(samples, audio)) {
    return false;
  }
  if (audioHash != nullptr) {
    *audioHash = genplusgx::hashAudioFrames(samples);
  }
  if (audioFrames != nullptr) {
    *audioFrames = samples.size();
  }
  return true;
}

std::optional<Baseline> baselineFor(
  const std::filesystem::path& path,
  const genplusgx::InputSnapshot& input)
{
  genplusgx::CoreAdapter adapter;
  if (!adapter.initialize() || !adapter.loadGame(path) ||
      !adapter.setInputSnapshot(input)) {
    return std::nullopt;
  }
  Baseline baseline;
  std::vector<std::uint16_t> pixels(genplusgx::maximumCoreSurfacePixels);
  if (!adapter.runFrame(false) ||
      !copyAdapterFrame(adapter, pixels, baseline.firstVideoHash,
        &baseline.firstAudioHash, &baseline.firstAudioFrames) ||
      !adapter.saveRawState(baseline.firstState) ||
      !adapter.runFrame(false) ||
      !copyAdapterFrame(adapter, pixels, baseline.secondVideoHash) ||
      !adapter.runFrame(false) ||
      !copyAdapterFrame(adapter, pixels, baseline.thirdVideoHash) ||
      !adapter.shutdown()) {
    return std::nullopt;
  }
  return baseline;
}

class CaptureProbe final : public genplusgx::EmulationCaptureSink {
public:
  [[nodiscard]] bool active() const noexcept override { return true; }

  [[nodiscard]] bool submitFrame(
    const genplusgx::CoreVideoFrameInfo& video,
    std::span<const std::uint16_t> pixels,
    const genplusgx::CoreAudioBatchInfo& audio,
    std::span<const genplusgx::StereoAudioFrame> samples) noexcept override
  {
    if (pixels.size() != video.pixelCount() ||
        samples.size() != audio.frameCount) {
      invalid.store(true, std::memory_order_release);
      return false;
    }
    frameNumber.store(video.frameNumber, std::memory_order_release);
    videoHash.store(genplusgx::hashVideoFrame(pixels), std::memory_order_release);
    audioHash.store(genplusgx::hashAudioFrames(samples), std::memory_order_release);
    calls.fetch_add(1U, std::memory_order_acq_rel);
    return true;
  }

  std::atomic_bool invalid{false};
  std::atomic_uint64_t calls{0U};
  std::atomic_uint64_t frameNumber{0U};
  std::atomic_uint64_t videoHash{0U};
  std::atomic_uint64_t audioHash{0U};
};

} // namespace

int main()
{
  using namespace genplusgx;

  const test::TemporaryFixture cartridge{
    test::makeGenesisRamMarkerRom(), ".bin"};
  InputSnapshot input;
  input.sequence = 7U;
  input.players[0].connected = true;
  input.players[0].buttons = buttonMask(InputButton::b);
  const auto baseline = baselineFor(cartridge.path(), input);
  if (!check(baseline.has_value(),
        "Could not construct an authoritative run-ahead baseline")) {
    return EXIT_FAILURE;
  }

  {
    CoreAdapter multitapAdapter;
    CoreInputSettings multitapInput;
    multitapInput.devices.fill(CoreInputDevice::none);
    for (std::size_t player = 0U; player < 5U; ++player) {
      multitapInput.devices[player] = CoreInputDevice::pad6Button;
    }
    CoreRollbackState rollback;
    if (!check(multitapAdapter.initialize() &&
          multitapAdapter.loadGame(cartridge.path()) &&
          multitapAdapter.applyInputSettings(multitapInput),
          "The Team Player rollback fixture could not initialize")) {
      return EXIT_FAILURE;
    }
    teamplayer_1_write(0x20U, 0x60U);
    if (!check(multitapAdapter.saveRollbackState(rollback),
          "The Team Player handshake could not be snapshotted")) {
      return EXIT_FAILURE;
    }
    const auto expectedRead = teamplayer_1_read();
    teamplayer_1_write(0x00U, 0x60U);
    const auto mutatedRead = teamplayer_1_read();
    if (!check(mutatedRead != expectedRead,
          "The Team Player mutation did not alter its private handshake") ||
        !check(multitapAdapter.restoreRollbackState(rollback) &&
            teamplayer_1_read() == expectedRead,
          "Run-ahead rollback did not restore the Team Player handshake") ||
        !check(multitapAdapter.shutdown(),
          "The Team Player rollback fixture did not shut down")) {
      return EXIT_FAILURE;
    }
  }

  auto capture = std::make_shared<CaptureProbe>();
  EmulationWorker worker{64U, 64U, 48'000, {}, {}, {}, capture};
  EmulationEvent event;
  if (!check(worker.start(), "Run-ahead worker did not start") ||
      !check(worker.waitForEvent(2s).has_value(),
        "Run-ahead worker start event was absent") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::updateRunAheadSettings(
            1U, {.enabled = true, .frames = 2U}), event),
        "Run-ahead settings were rejected") ||
      !check(event.runAheadEnabled && !event.runAheadSupported &&
          !event.runAheadActive,
        "An idle worker claimed active run-ahead") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::load(2U, cartridge.path()), event),
        "Run-ahead cartridge did not load") ||
      !check(event.runAheadEnabled && event.runAheadSupported &&
          event.runAheadActive && !event.runAheadVerified &&
          event.runAheadFrames == 2U,
        "Loaded cartridge did not expose pending run-ahead verification") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::updateInput(3U, input), event),
        "Run-ahead input snapshot was rejected")) {
    return EXIT_FAILURE;
  }

  worker.audioFrames()->clear();
  if (!check(submitAndSucceed(worker,
          EmulationCommand::simple(EmulationCommandType::frameAdvance, 4U),
          event),
        "Run-ahead frame advance failed") ||
      !check(event.frameNumber == 1U && event.appliedInputSequence == 7U &&
          event.runAheadActive && event.runAheadVerified,
        "Run-ahead changed the authoritative frame/input timeline") ||
      !check(capture->calls.load(std::memory_order_acquire) == 1U &&
          !capture->invalid.load(std::memory_order_acquire) &&
          capture->frameNumber.load(std::memory_order_acquire) == 1U &&
          capture->videoHash.load(std::memory_order_acquire) ==
            baseline->thirdVideoHash &&
          capture->audioHash.load(std::memory_order_acquire) ==
            baseline->firstAudioHash,
        "Speculative frames leaked or the capture tap did not receive one composed frame")) {
    return EXIT_FAILURE;
  }

  std::vector<std::uint16_t> pixels(maximumCoreSurfacePixels);
  CoreVideoFrameInfo video;
  std::uint64_t generation = 0U;
  const auto audioFrameCount = worker.audioFrames()->occupancyFrames();
  std::vector<StereoAudioFrame> audio(audioFrameCount);
  const auto audioRead = worker.audioFrames()->read(audio);
  if (!check(worker.videoFrames()->copyLatest(pixels, video, generation) &&
          video.frameNumber == 1U &&
          hashVideoFrame(
            std::span<const std::uint16_t>{pixels}.first(video.pixelCount())) ==
            baseline->thirdVideoHash,
        "The displayed frame was not the final speculative framebuffer") ||
      !check(audioFrameCount == baseline->firstAudioFrames &&
          audioRead.providedFrames == audioFrameCount &&
          hashAudioFrames(audio) == baseline->firstAudioHash,
        "Speculative audio replaced or accumulated with authoritative audio") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::simple(EmulationCommandType::captureState, 5U),
          event), "Authoritative state capture failed after run-ahead") ||
      !check(event.rawState == baseline->firstState,
        "Run-ahead advanced the authoritative core state more than one frame")) {
    return EXIT_FAILURE;
  }

  const auto metrics = worker.metrics();
  if (!check(metrics.runAheadEnabled && metrics.runAheadSupported &&
          metrics.runAheadActive && metrics.runAheadVerified &&
          metrics.runAheadFrames == 2U &&
          metrics.runAheadSpeculativeFrames == 2U &&
          metrics.runAheadRollbacks == 1U &&
          metrics.runAheadDeterminismFailures == 0U,
        "Run-ahead metrics did not report bounded verified speculation")) {
    return EXIT_FAILURE;
  }

  constexpr StereoAudioFrame bufferedSentinel{123, -123};
  if (!check(worker.audioFrames()->write({&bufferedSentinel, 1U}).acceptedFrames ==
          1U, "Authoritative audio sentinel could not be buffered") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::simple(EmulationCommandType::frameAdvance, 51U),
          event), "Second verified run-ahead frame failed")) {
    return EXIT_FAILURE;
  }
  std::vector<StereoAudioFrame> retainedAudio(
    worker.audioFrames()->occupancyFrames());
  const auto retainedRead = worker.audioFrames()->read(retainedAudio);
  if (!check(retainedRead.providedFrames == retainedAudio.size() &&
          retainedAudio.size() > 1U && retainedAudio.front() == bufferedSentinel,
        "Speculative rollback cleared previously buffered authoritative audio")) {
    return EXIT_FAILURE;
  }

  CoreInputSettings specializedInput;
  specializedInput.devices.fill(CoreInputDevice::none);
  specializedInput.devices[0] = CoreInputDevice::segaMouse;
  const auto beforeSpecializedInput =
    worker.metrics().runAheadSpeculativeFrames;
  if (!check(submitAndSucceed(worker,
          EmulationCommand::updateInputSettings(52U, specializedInput), event) &&
          !event.runAheadSupported && !event.runAheadActive,
        "A specialized unsnapshotted input did not suspend run-ahead") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::simple(EmulationCommandType::frameAdvance, 53U),
          event), "The specialized-input authoritative frame failed") ||
      !check(worker.metrics().runAheadSpeculativeFrames ==
          beforeSpecializedInput,
        "A specialized input executed unsupported speculative frames") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::updateInputSettings(54U, CoreInputSettings{}), event) &&
          event.runAheadSupported && event.runAheadActive &&
          !event.runAheadVerified,
        "Standard controller input did not reactivate pending run-ahead") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::simple(EmulationCommandType::frameAdvance, 55U),
          event) && event.runAheadVerified,
        "Standard controller input did not reverify run-ahead (active=" +
          std::to_string(event.runAheadActive) + ", supported=" +
          std::to_string(event.runAheadSupported) + ", failures=" +
          std::to_string(worker.metrics().runAheadDeterminismFailures) +
          ", metricsVerified=" +
          std::to_string(worker.metrics().runAheadVerified) + ")")) {
    return EXIT_FAILURE;
  }

  const auto beforeFastForward = worker.metrics().runAheadSpeculativeFrames;
  if (!check(submitAndSucceed(worker,
          EmulationCommand::fastForward(6U, true), event) &&
          !event.runAheadActive,
        "Fast-forward did not suspend run-ahead") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::simple(EmulationCommandType::frameAdvance, 7U),
          event), "Suspended run-ahead frame failed") ||
      !check(worker.metrics().runAheadSpeculativeFrames == beforeFastForward,
        "Fast-forward executed speculative run-ahead frames") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::fastForward(8U, false), event) &&
          event.runAheadActive,
        "Normal mode did not reactivate verified run-ahead")) {
    return EXIT_FAILURE;
  }

  const auto beforeSlowMotion = worker.metrics().runAheadSpeculativeFrames;
  if (!check(submitAndSucceed(worker,
          EmulationCommand::slowMotion(81U, true), event) &&
          !event.runAheadActive,
        "Slow motion did not suspend run-ahead") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::simple(EmulationCommandType::frameAdvance, 82U),
          event), "Slow-motion authoritative frame failed") ||
      !check(worker.metrics().runAheadSpeculativeFrames == beforeSlowMotion,
        "Slow motion executed speculative run-ahead frames") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::slowMotion(83U, false), event) &&
          event.runAheadActive,
        "Normal mode did not reactivate run-ahead after slow motion") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::updateRewindSettings(84U, {
            .enabled = true,
            .captureIntervalFrames = 1U,
            .memoryLimitBytes = 16U * 1024U * 1024U,
          }), event), "A bounded rewind history could not be configured") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::simple(EmulationCommandType::resume, 85U), event),
        "Run-ahead worker could not resume for rewind interaction") ||
      !check(waitForRewindAvailability(worker),
        "Forward run-ahead history was unavailable before rewind") ||
      !check(worker.metrics().runAheadActive &&
          worker.metrics().runAheadVerified,
        "Run-ahead did not remain deterministic while populating rewind "
        "history (failures=" + std::to_string(
          worker.metrics().runAheadDeterminismFailures) + ")") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::rewinding(86U, true), event) &&
          event.rewinding && !event.runAheadActive,
        "Rewind did not suspend run-ahead")) {
    return EXIT_FAILURE;
  }
  const auto beforeRewindFrame = worker.metrics().runAheadSpeculativeFrames;
  if (!check(waitForFrame(worker, true).has_value(),
        "A reverse frame was absent during the run-ahead interaction") ||
      !check(worker.metrics().runAheadSpeculativeFrames == beforeRewindFrame,
        "Rewind executed speculative run-ahead frames") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::rewinding(87U, false), event),
        "Forward mode could not be restored after rewind") ||
      !check(event.runAheadActive,
        "Forward mode did not reactivate run-ahead after rewind "
        "(enabled=" + std::to_string(event.runAheadEnabled) +
        ", supported=" + std::to_string(event.runAheadSupported) +
        ", rewinding=" + std::to_string(event.rewinding) +
        ", verified=" + std::to_string(event.runAheadVerified) + ")") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::simple(EmulationCommandType::pause, 88U), event),
        "Run-ahead worker could not pause after rewind interaction")) {
    return EXIT_FAILURE;
  }

  if (!check(worker.submit(EmulationCommand::updateRunAheadSettings(
          9U, {.enabled = true, .frames = maximumRunAheadFrames + 1U})),
        "Invalid run-ahead command could not reach typed validation")) {
    return EXIT_FAILURE;
  }
  const auto invalid = waitForOperation(worker, 9U);
  if (!check(invalid && !invalid->succeeded() &&
          invalid->coreError == CoreError::invalidStatePayload &&
          worker.metrics().runAheadFrames == 2U,
        "Invalid run-ahead settings changed the active configuration") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::updateRewindSettings(89U, {
            .enabled = false,
            .captureIntervalFrames = 1U,
            .memoryLimitBytes = 16U * 1024U * 1024U,
          }), event), "Rewind could not be disabled before run-ahead stress") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::updateRunAheadSettings(
            90U, {.enabled = true, .frames = maximumRunAheadFrames}), event),
        "Maximum bounded run-ahead could not be configured") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::simple(EmulationCommandType::frameAdvance, 91U),
          event) && event.runAheadVerified,
        "Maximum run-ahead did not pass determinism verification")) {
    return EXIT_FAILURE;
  }

  constexpr std::uint64_t stressFrames = 120U;
  const auto beforeStress = worker.metrics();
  const auto capturesBeforeStress = capture->calls.load(std::memory_order_acquire);
  for (std::uint64_t index = 0U; index < stressFrames; ++index) {
    worker.audioFrames()->clear();
    if (!submitAndSucceed(worker,
          EmulationCommand::simple(
            EmulationCommandType::frameAdvance, 1'000U + index), event) ||
        !event.runAheadActive || !event.runAheadVerified) {
      std::cerr << "Run-ahead stress frame " << index << " failed\n";
      return EXIT_FAILURE;
    }
  }
  const auto afterStress = worker.metrics();
  if (!check(afterStress.runAheadSpeculativeFrames ==
          beforeStress.runAheadSpeculativeFrames +
            (stressFrames * maximumRunAheadFrames) &&
          afterStress.runAheadRollbacks ==
            beforeStress.runAheadRollbacks + stressFrames &&
          afterStress.runAheadDeterminismFailures == 0U,
        "Long-running run-ahead counters exposed speculative leakage") ||
      !check(afterStress.runAheadStateBytes > 0U &&
          afterStress.runAheadStateBytes <=
            afterStress.runAheadStateCapacityBytes &&
          afterStress.runAheadStateCapacityBytes ==
            beforeStress.runAheadStateCapacityBytes &&
          afterStress.runAheadStateCapacityBytes <= 4U * 1024U * 1024U,
        "Run-ahead rollback memory was unbounded or grew during stress") ||
      !check(capture->calls.load(std::memory_order_acquire) ==
          capturesBeforeStress + stressFrames,
        "Speculative frames leaked into capture during stress") ||
      !check(worker.audioFrames()->occupancyFrames() <=
          worker.audioFrames()->capacityFrames(),
        "Run-ahead allowed unbounded authoritative audio") ||
      !check(submitAndSucceed(worker,
          EmulationCommand::updateRunAheadSettings(
            10U, {.enabled = false, .frames = 1U}), event),
        "Run-ahead could not be disabled") ||
      !check(!event.runAheadEnabled && !event.runAheadActive,
        "Disabled run-ahead remained active") ||
      !check(worker.stop(), "Run-ahead cartridge worker did not stop")) {
    return EXIT_FAILURE;
  }

  const test::TemporaryFixture bios{test::makeSegaCdBios(), ".bin"};
  const test::TemporaryFixture disc{test::makeSegaCdDiscImage(), ".iso"};
  CoreFirmwareSettings firmware;
  firmware.segaCdUsa = bios.path();
  EmulationWorker cdWorker;
  if (!check(cdWorker.start(), "Sega CD run-ahead worker did not start") ||
      !check(cdWorker.waitForEvent(2s).has_value(),
        "Sega CD worker start event was absent") ||
      !check(submitAndSucceed(cdWorker,
          EmulationCommand::updateFirmwareSettings(11U, firmware), event),
        "Synthetic Sega CD firmware was rejected") ||
      !check(submitAndSucceed(cdWorker,
          EmulationCommand::updateRunAheadSettings(
            12U, {.enabled = true, .frames = 2U}), event),
        "Sega CD run-ahead preference was rejected") ||
      !check(submitAndSucceed(cdWorker,
          EmulationCommand::load(13U, disc.path()), event),
        "Synthetic Sega CD disc did not load") ||
      !check(event.hardware == 0x84U && event.runAheadEnabled &&
          !event.runAheadSupported && !event.runAheadActive,
        "Sega CD did not suspend run-ahead safely") ||
      !check(submitAndSucceed(cdWorker,
          EmulationCommand::simple(EmulationCommandType::frameAdvance, 14U),
          event), "Sega CD authoritative frame failed") ||
      !check(cdWorker.metrics().runAheadSpeculativeFrames == 0U,
        "Sega CD executed unsupported speculative frames") ||
      !check(cdWorker.stop(), "Sega CD run-ahead worker did not stop")) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
