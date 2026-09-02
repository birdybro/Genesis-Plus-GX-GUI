#include "genplusgx/emulation_worker.h"
#include "synthetic_rom.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <thread>
#include <utility>

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
  genplusgx::EmulationWorker& worker, std::uint64_t operationId)
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

std::optional<genplusgx::EmulationEvent> waitForType(
  genplusgx::EmulationWorker& worker, genplusgx::EmulationEventType type)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = worker.waitForEvent(100ms);
    if (event && event->type == type) {
      return event;
    }
  }
  return std::nullopt;
}

std::optional<genplusgx::EmulationEvent> submit(
  genplusgx::EmulationWorker& worker, genplusgx::EmulationCommand command)
{
  const auto operationId = command.operationId;
  if (!worker.submit(std::move(command))) {
    return std::nullopt;
  }
  return waitForOperation(worker, operationId);
}

} // namespace

int main()
{
  const genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".bin"};
  genplusgx::EmulationWorker worker;
  if (!check(worker.start(), "Movie workflow worker could not start") ||
      !check(waitForType(worker,
        genplusgx::EmulationEventType::workerStarted).has_value(),
        "Movie workflow worker did not become ready")) {
    return 1;
  }
  auto loaded = submit(worker, genplusgx::EmulationCommand::load(1U, fixture.path()));
  if (!check(loaded && loaded->succeeded(),
        "Movie workflow fixture could not load")) {
    return 1;
  }

  const genplusgx::movies::MovieDescriptor descriptor{
    .gameSha256 = std::string(64U, 'a'),
    .settingsSha256 = std::string(64U, 'b'),
    .coreVersion = "movie-test-core",
  };
  genplusgx::InputSnapshot right;
  right.sequence = 1U;
  right.players[0].connected = true;
  right.players[0].buttons = genplusgx::buttonMask(genplusgx::InputButton::right);
  auto inputEvent = submit(
    worker, genplusgx::EmulationCommand::updateInput(2U, right));
  auto started = submit(worker,
    genplusgx::EmulationCommand::startMovieRecordingSession(
      3U, descriptor, {
        .author = "Generated test",
        .notes = "Synthetic deterministic workflow",
        .rerecordCount = 0U,
      }));
  if (!check(inputEvent && inputEvent->succeeded() && started &&
        started->type == genplusgx::EmulationEventType::movieRecordingStarted &&
        worker.metrics().movieRecording,
        "Input movie recording did not start")) {
    return 1;
  }
  auto blocked = submit(worker, genplusgx::EmulationCommand::simple(
    genplusgx::EmulationCommandType::hardReset, 4U));
  if (!check(blocked && !blocked->succeeded() &&
        blocked->coreError == genplusgx::CoreError::invalidStatePayload,
        "State-changing command was accepted during movie recording")) {
    return 1;
  }
  auto frameOne = submit(worker, genplusgx::EmulationCommand::simple(
    genplusgx::EmulationCommandType::frameAdvance, 5U));
  genplusgx::InputSnapshot action = right;
  action.sequence = 2U;
  action.players[0].buttons = genplusgx::InputButton::a |
    genplusgx::InputButton::start;
  auto changed = submit(
    worker, genplusgx::EmulationCommand::updateInput(6U, action));
  auto frameTwo = submit(worker, genplusgx::EmulationCommand::simple(
    genplusgx::EmulationCommandType::frameAdvance, 7U));
  auto stopped = submit(worker, genplusgx::EmulationCommand::simple(
    genplusgx::EmulationCommandType::stopMovieRecording, 8U));
  if (!check(frameOne && frameOne->succeeded() && changed && changed->succeeded() &&
        frameTwo && frameTwo->succeeded() && stopped && stopped->succeeded() &&
        stopped->type == genplusgx::EmulationEventType::movieRecordingFinished &&
        stopped->movie.valid() && stopped->movie.frames.size() == 2U &&
        stopped->movie.frames[0].players[0].buttons ==
          right.players[0].buttons &&
        stopped->movie.frames[1].players[0].buttons ==
          action.players[0].buttons && !worker.metrics().movieRecording,
        "Frame-boundary input recording was incorrect")) {
    return 1;
  }
  auto recordedMovie = stopped->movie;
  auto finalRecorded = submit(worker, genplusgx::EmulationCommand::simple(
    genplusgx::EmulationCommandType::captureState, 9U));
  if (!check(finalRecorded && finalRecorded->succeeded() &&
        !finalRecorded->rawState.empty(),
        "Recorded continuation state could not be captured")) {
    return 1;
  }

  auto wrongDescriptor = descriptor;
  wrongDescriptor.gameSha256 = std::string(64U, 'c');
  auto incompatible = submit(worker,
    genplusgx::EmulationCommand::startMoviePlaybackSession(
      10U, recordedMovie, wrongDescriptor));
  if (!check(incompatible && !incompatible->succeeded() &&
        incompatible->coreError == genplusgx::CoreError::invalidStatePayload,
        "Wrong-game movie playback was accepted")) {
    return 1;
  }
  auto playback = submit(worker,
    genplusgx::EmulationCommand::startMoviePlaybackSession(
      11U, recordedMovie, descriptor));
  if (!check(playback && playback->succeeded() &&
        playback->type == genplusgx::EmulationEventType::moviePlaybackStarted,
        "Compatible input movie playback did not start")) {
    return 1;
  }
  const auto finished = waitForType(
    worker, genplusgx::EmulationEventType::moviePlaybackFinished);
  if (!check(finished && finished->movieFrame == 2U &&
        finished->movieFrameCount == 2U &&
        worker.state() == genplusgx::EmulationWorkerState::paused &&
        !worker.metrics().moviePlayback,
        "Movie playback did not stop exactly at the timeline boundary")) {
    return 1;
  }
  auto finalPlayback = submit(worker, genplusgx::EmulationCommand::simple(
    genplusgx::EmulationCommandType::captureState, 12U));
  if (!check(finalPlayback && finalPlayback->succeeded() &&
        finalPlayback->rawState == finalRecorded->rawState,
        "Recorded and replayed core states were not deterministic")) {
    return 1;
  }
  auto postMovieInput = action;
  postMovieInput.sequence = 3U;
  postMovieInput.players[0].buttons =
    genplusgx::buttonMask(genplusgx::InputButton::b);
  const auto resumedInput = submit(
    worker, genplusgx::EmulationCommand::updateInput(13U, postMovieInput));
  if (!check(resumedInput && resumedInput->succeeded() &&
        resumedInput->appliedInputSequence > postMovieInput.sequence,
        "Live input did not resume monotonically after movie playback") ||
      !check(worker.stop(), "Movie workflow worker did not stop cleanly")) {
    return 1;
  }
  return 0;
}
