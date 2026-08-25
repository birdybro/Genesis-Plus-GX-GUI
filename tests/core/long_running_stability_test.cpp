#include "genplusgx/core_adapter.h"
#include "genplusgx/emulation_worker.h"

#include "synthetic_rom.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

bool check(bool condition, const char* message)
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
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = worker.waitForEvent(100ms);
    if (event && event->operationId == operationId) {
      return event;
    }
  }
  return std::nullopt;
}

bool submitAndSucceed(
  genplusgx::EmulationWorker& worker,
  genplusgx::EmulationCommand command,
  genplusgx::EmulationEvent* output = nullptr)
{
  const auto operationId = command.operationId;
  if (!worker.submit(std::move(command))) {
    return false;
  }
  auto event = waitForOperation(worker, operationId);
  if (!event || !event->succeeded()) {
    return false;
  }
  if (output != nullptr) {
    *output = std::move(*event);
  }
  return true;
}

bool runAcceleratedCoreFrames(
  genplusgx::CoreAdapter& adapter,
  std::uint64_t frameCount,
  std::vector<genplusgx::StereoAudioFrame>& audio,
  std::vector<std::uint16_t>& video,
  std::uint64_t& audioFrames,
  std::uint64_t& audioDigest)
{
  genplusgx::InputSnapshot input;
  input.players[0].connected = true;
  for (std::uint64_t frame = 1U; frame <= frameCount; ++frame) {
    if ((frame % 257U) == 0U) {
      ++input.sequence;
      input.players[0].buttons = (input.sequence & 1U) != 0U
        ? genplusgx::buttonMask(genplusgx::InputButton::b)
        : genplusgx::buttonMask(genplusgx::InputButton::c);
      if (!adapter.setInputSnapshot(input)) {
        return false;
      }
    }
    const bool skipVideo = (frame % 120U) != 0U;
    if (!adapter.runFrame(skipVideo)) {
      return false;
    }

    genplusgx::CoreAudioBatchInfo audioInfo;
    if (!adapter.audioBatchInfo(audioInfo) ||
        audioInfo.frameCount > audio.size() ||
        audioInfo.emulatedFrameNumber != adapter.frameCount() ||
        !adapter.copyAudioFrames(
          std::span<genplusgx::StereoAudioFrame>{audio}.first(audioInfo.frameCount),
          audioInfo)) {
      return false;
    }
    audioFrames += audioInfo.frameCount;
    if (audioInfo.frameCount != 0U) {
      const auto sample = audio[(frame + audioInfo.frameCount) % audioInfo.frameCount];
      audioDigest = (audioDigest * 1'099'511'628'211ULL) ^
        static_cast<std::uint16_t>(sample.left) ^
        (static_cast<std::uint64_t>(static_cast<std::uint16_t>(sample.right)) << 16U);
    }

    if (!skipVideo) {
      genplusgx::CoreVideoFrameInfo videoInfo;
      if (!adapter.copyVideoFrame(video, videoInfo) ||
          videoInfo.frameNumber != adapter.frameCount() ||
          videoInfo.pixelCount() == 0U || videoInfo.pixelCount() > video.size()) {
        return false;
      }
    }
  }
  return true;
}

bool waitForScheduledFrames(
  genplusgx::EmulationWorker& worker,
  std::uint64_t target,
  std::chrono::steady_clock::time_point deadline)
{
  while (std::chrono::steady_clock::now() < deadline) {
    const auto metrics = worker.metrics();
    if (metrics.pacedFrameCount >= target) {
      return true;
    }
    if (metrics.commandQueueDepth > metrics.commandQueueCapacity ||
        metrics.eventQueueDepth > metrics.eventQueueCapacity) {
      return false;
    }
    std::this_thread::sleep_for(5ms);
  }
  return worker.metrics().pacedFrameCount >= target;
}

} // namespace

int main()
{
  const genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".bin"};
  constexpr std::uint64_t acceleratedFrames = 20'000U;
  constexpr std::size_t maximumAudioFrames = 4'096U;
  std::vector<genplusgx::StereoAudioFrame> audio(maximumAudioFrames);
  std::vector<std::uint16_t> video(
    genplusgx::VideoFrameExchange::maximumSurfacePixels);
  std::uint64_t audioFrames = 0U;
  std::uint64_t audioDigest = 1'469'598'103'934'665'603ULL;

  genplusgx::CoreAdapter adapter;
  if (!check(adapter.initialize(), "Accelerated core initialization failed") ||
      !check(adapter.loadGame(fixture.path()), "Accelerated fixture load failed")) {
    return 1;
  }
  const auto resourceBaseline = adapter.resourceMetrics();
  if (!check(resourceBaseline.framebufferCapacityBytes > 0U &&
          resourceBaseline.audioScratchCapacityFrames >= maximumAudioFrames &&
          resourceBaseline.stateScratchCapacityBytes > 0U &&
          resourceBaseline.stateLoadScratchCapacityBytes > 0U,
        "Core resource instrumentation was incomplete") ||
      !check(runAcceleratedCoreFrames(
          adapter, acceleratedFrames, audio, video, audioFrames, audioDigest),
        "Accelerated long-running frame execution failed") ||
      !check(adapter.frameCount() == acceleratedFrames,
        "Accelerated frame count drifted") ||
      !check(audioFrames > acceleratedFrames * 700U && audioDigest != 0U,
        "Long-running audio output was incomplete") ||
      !check(adapter.resourceMetrics() == resourceBaseline,
        "Core scratch storage grew during steady-state execution")) {
    return 2;
  }

  std::vector<std::uint8_t> state;
  if (!check(adapter.saveRawState(state) && !state.empty(),
        "Long-running state capture failed") ||
      !check(adapter.resourceMetrics() == resourceBaseline,
        "State capture grew fixed core scratch storage") ||
      !check(adapter.unloadGame(), "Accelerated fixture unload failed")) {
    return 3;
  }

  for (std::uint64_t cycle = 0U; cycle < 20U; ++cycle) {
    std::uint64_t cycleAudioFrames = 0U;
    std::uint64_t cycleDigest = cycle + 1U;
    if (!check(adapter.loadGame(fixture.path()), "Lifecycle stress load failed") ||
        !check(runAcceleratedCoreFrames(
          adapter, 25U, audio, video, cycleAudioFrames, cycleDigest),
          "Lifecycle stress frame execution failed") ||
        !check(adapter.resourceMetrics() == resourceBaseline,
          "Core resources grew across lifecycle stress") ||
        !check(adapter.unloadGame(), "Lifecycle stress unload failed")) {
      return 4;
    }
  }
  if (!check(adapter.shutdown(), "Accelerated core shutdown failed")) {
    return 5;
  }

  auto videoExchange = std::make_shared<genplusgx::VideoFrameExchange>();
  auto audioRing = std::make_shared<genplusgx::StereoAudioRingBuffer>(2'048U);
  genplusgx::EmulationWorker worker{
    8U, 8U, 48'000, videoExchange, audioRing};
  if (!check(worker.start(), "Stress worker start failed") ||
      !check(worker.waitForEvent(2s).has_value(),
        "Stress worker start event was missing") ||
      !check(submitAndSucceed(
        worker, genplusgx::EmulationCommand::load(1U, fixture.path())),
        "Stress worker fixture load failed")) {
    return 6;
  }

  for (std::uint64_t sequence = 1U; sequence <= 10'000U; ++sequence) {
    genplusgx::InputSnapshot snapshot;
    snapshot.sequence = sequence;
    snapshot.players[0].connected = true;
    snapshot.players[0].buttons =
      genplusgx::buttonMask(genplusgx::InputButton::a);
    if (!check(worker.submit(genplusgx::EmulationCommand::updateInput(
          10'000U + sequence, snapshot)),
        "Coalescing stress input submission failed")) {
      return 7;
    }
  }
  genplusgx::EmulationEvent completed;
  if (!check(submitAndSucceed(worker,
        genplusgx::EmulationCommand::simple(
          genplusgx::EmulationCommandType::hardReset, 30'001U), &completed),
        "Coalescing stress sentinel failed")) {
    return 8;
  }
  const auto coalesced = worker.metrics();
  if (!check(coalesced.coalescedInputCommands > 0U,
        "Input burst did not exercise command coalescing") ||
      !check(coalesced.commandQueueDepth <= coalesced.commandQueueCapacity &&
          coalesced.eventQueueDepth <= coalesced.eventQueueCapacity &&
          coalesced.commandQueueCapacity == 8U &&
          coalesced.eventQueueCapacity == 9U,
        "Worker queues exceeded their configured bounds")) {
    return 9;
  }

  if (!check(submitAndSucceed(worker,
        genplusgx::EmulationCommand::simple(
          genplusgx::EmulationCommandType::start, 30'002U)),
        "Normal stress pacing did not start")) {
    return 10;
  }
  const auto normalTarget = worker.metrics().pacedFrameCount + 90U;
  if (!check(waitForScheduledFrames(
        worker, normalTarget, std::chrono::steady_clock::now() + 3s),
        "Normal stress pacing did not reach its frame target") ||
      !check(submitAndSucceed(worker,
        genplusgx::EmulationCommand::simple(
          genplusgx::EmulationCommandType::pause, 30'003U)),
        "Normal stress pacing did not pause")) {
    return 11;
  }
  const auto audioMetrics = audioRing->metrics();
  if (!check(audioRing->occupancyFrames() <= audioRing->capacityFrames() &&
          audioMetrics.peakOccupancyFrames <= audioRing->capacityFrames() &&
          audioMetrics.overrunCount > 0U,
        "Sustained playback did not remain inside the bounded audio ring")) {
    return 12;
  }

  audioRing->clear();
  if (!check(submitAndSucceed(
        worker, genplusgx::EmulationCommand::fastForward(30'004U, true)),
        "Stress fast-forward could not enable") ||
      !check(submitAndSucceed(worker,
        genplusgx::EmulationCommand::simple(
          genplusgx::EmulationCommandType::resume, 30'005U)),
        "Stress fast-forward could not resume")) {
    return 13;
  }
  const auto fastTarget = worker.metrics().pacedFrameCount + 600U;
  if (!check(waitForScheduledFrames(
        worker, fastTarget, std::chrono::steady_clock::now() + 5s),
        "Fast-forward stress did not reach 600 scheduled frames") ||
      !check(submitAndSucceed(worker,
        genplusgx::EmulationCommand::simple(
          genplusgx::EmulationCommandType::pause, 30'006U)),
        "Fast-forward stress did not pause")) {
    return 14;
  }
  const auto workerMetrics = worker.metrics();
  const auto videoMetrics = videoExchange->metrics();
  if (!check(workerMetrics.commandQueueDepth <= workerMetrics.commandQueueCapacity &&
          workerMetrics.eventQueueDepth <= workerMetrics.eventQueueCapacity &&
          workerMetrics.replacedFrameEvents > 0U,
        "Sustained emulation grew a worker queue") ||
      !check(audioRing->occupancyFrames() == 0U,
        "Fast-forward accumulated host audio") ||
      !check(videoMetrics.allocatedPixels ==
          genplusgx::VideoFrameExchange::slotCount *
            genplusgx::VideoFrameExchange::maximumSurfacePixels &&
          videoMetrics.publishedFrames >= 690U,
        "Sustained emulation changed the fixed video exchange allocation")) {
    return 15;
  }

  if (!check(submitAndSucceed(worker,
        genplusgx::EmulationCommand::simple(
          genplusgx::EmulationCommandType::unloadGame, 30'007U)),
        "Stress worker unload failed") ||
      !check(worker.stop(), "Stress worker stop failed")) {
    return 16;
  }

  std::uint64_t lifecycleOperation = 40'000U;
  for (int cycle = 0; cycle < 12; ++cycle) {
    if (!check(worker.start(), "Repeated stress worker start failed") ||
        !check(worker.waitForEvent(2s).has_value(),
          "Repeated stress worker start event was missing") ||
        !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::load(
            ++lifecycleOperation, fixture.path())),
          "Repeated stress worker load failed") ||
        !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::frameAdvance,
            ++lifecycleOperation)),
          "Repeated stress frame advance failed") ||
        !check(submitAndSucceed(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::unloadGame,
            ++lifecycleOperation)),
          "Repeated stress worker unload failed") ||
        !check(worker.stop(), "Repeated stress worker stop failed")) {
      return 17;
    }
  }

  genplusgx::CoreAdapter finalLeaseProbe;
  return check(finalLeaseProbe.initialize(),
           "Stress shutdown retained the global core lease") &&
      check(finalLeaseProbe.shutdown(),
        "Final stress core lease shutdown failed")
    ? 0
    : 18;
}
