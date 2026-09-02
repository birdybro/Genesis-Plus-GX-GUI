#include "genplusgx/emulation_worker.h"
#include "genplusgx/achievements/achievement_bridge.h"
#include "genplusgx/achievements/achievement_types.h"
#include "synthetic_rom.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>

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

std::optional<genplusgx::achievements::ServerRequest> waitForRequest(
  const std::shared_ptr<genplusgx::achievements::ServerBridge>& bridge,
  std::string_view marker)
{
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto request = bridge->takeRequest()) {
      if (request->postData.find(marker) != std::string::npos) {
        return request;
      }
    }
    std::this_thread::sleep_for(10ms);
  }
  return std::nullopt;
}

bool waitForHardcore(genplusgx::EmulationWorker& worker)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    while (worker.pollEvent()) {
    }
    if (worker.metrics().achievementsHardcore) {
      return true;
    }
    std::this_thread::sleep_for(20ms);
  }
  return false;
}

} // namespace

int main()
{
  const genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".bin"};
  auto bridge = std::make_shared<genplusgx::achievements::ServerBridge>();
  genplusgx::EmulationWorker worker{
    64U, 64U, 48'000, {}, {}, {}, {}, {}, bridge};
  if (!check(worker.start(), "Achievement worker did not start")) {
    return 1;
  }
  bool workerStarted = false;
  const auto startDeadline = std::chrono::steady_clock::now() + 2s;
  while (!workerStarted && std::chrono::steady_clock::now() < startDeadline) {
    const auto event = worker.waitForEvent(100ms);
    workerStarted = event &&
      event->type == genplusgx::EmulationEventType::workerStarted;
  }
  if (!check(workerStarted, "Achievement worker did not publish startup")) {
    return 1;
  }

  genplusgx::achievements::Settings settings;
  settings.enabled = true;
  settings.hardcore = true;
  settings.username = "WorkerPlayer";
  if (!check(worker.submit(
        genplusgx::EmulationCommand::updateAchievementSettings(1U, settings)),
      "Achievement settings command was rejected") ||
      !check(waitForOperation(worker, 1U)->succeeded(),
        "Achievement settings command failed") ||
      !check(worker.submit(genplusgx::EmulationCommand::load(2U, fixture.path())),
        "Synthetic game load was rejected") ||
      !check(waitForOperation(worker, 2U)->succeeded(),
        "Synthetic game load failed")) {
    return 2;
  }
  if (!check(!bridge->takeRequest().has_value(),
      "Signed-out game loading unexpectedly contacted the provider")) {
    return 2;
  }

  if (!check(worker.submit(
        genplusgx::EmulationCommand::achievementPasswordLogin(
          3U, "WorkerPlayer", "Pa$$word")),
      "Achievement login command was rejected") ||
      !check(waitForOperation(worker, 3U)->succeeded(),
        "Achievement login command failed")) {
    return 3;
  }
  auto request = waitForRequest(bridge, "r=login2");
  if (!check(request.has_value() &&
        request->postData.find("r=login2") != std::string::npos,
      "Worker did not bridge the provider login request") ||
      !check(bridge->submitResponse({
        .id = request->id,
        .httpStatusCode = 200,
        .body = "{\"Success\":true,\"User\":\"WorkerPlayer\","
          "\"AvatarUrl\":\"https://media.retroachievements.org/UserPic/W.png\","
          "\"Token\":\"WorkerToken\",\"Score\":1,"
          "\"SoftcoreScore\":0,\"Messages\":0}",
      }), "Worker mock login response was rejected")) {
    return 4;
  }

  auto gameRequest = waitForRequest(bridge, "r=achievementsets");
  if (!check(gameRequest.has_value(),
        "Login did not start generated-ROM identification") ||
      !check(!worker.metrics().achievementsHardcore,
        "Hardcore activated before the provider recognized the game") ||
      !check(worker.submit(
        genplusgx::EmulationCommand::startNetplaySession(4U, {})),
        "Pending-identification netplay check could not be queued")) {
    return 4;
  }
  const auto netplayDuringIdentification = waitForOperation(worker, 4U);
  if (!check(netplayDuringIdentification &&
        !netplayDuringIdentification->succeeded() &&
        netplayDuringIdentification->message.find("achievements") !=
          std::string::npos,
      "Netplay was not rejected during achievement identification")) {
    return 4;
  }
  if (!check(bridge->submitResponse({
        .id = gameRequest->id,
        .httpStatusCode = 200,
        .body = "{\"Success\":true,\"GameId\":1234,"
          "\"Title\":\"Synthetic Worker Game\",\"ConsoleId\":1,"
          "\"ImageIconUrl\":\"\",\"RichPresenceGameId\":1234,"
          "\"RichPresencePatch\":\"\",\"Sets\":[{"
          "\"AchievementSetId\":1111,\"GameId\":1234,"
          "\"Title\":null,\"Type\":\"core\",\"ImageIconUrl\":\"\","
          "\"Achievements\":[],\"Leaderboards\":[]}]}",
      }), "Worker mock game-set response was rejected")) {
    return 4;
  }
  auto sessionRequest = waitForRequest(bridge, "r=startsession");
  if (!check(sessionRequest.has_value(),
        "Recognized game did not start a provider session") ||
      !check(bridge->submitResponse({
        .id = sessionRequest->id,
        .httpStatusCode = 200,
        .body = "{\"Success\":true,\"Unlocks\":[],"
          "\"HardcoreUnlocks\":[]}",
      }), "Worker mock session response was rejected") ||
      !check(waitForHardcore(worker),
        "Recognized game did not activate worker-owned Hardcore enforcement")) {
    return 4;
  }

  const auto assertBlocked = [&worker](genplusgx::EmulationCommand command) {
    const auto operationId = command.operationId;
    if (!worker.submit(std::move(command))) {
      return false;
    }
    const auto event = waitForOperation(worker, operationId);
    return event && !event->succeeded() &&
      event->message.find("Hardcore Mode") != std::string::npos;
  };
  if (!check(assertBlocked(genplusgx::EmulationCommand::simple(
        genplusgx::EmulationCommandType::captureState, 10U)),
      "Hardcore accepted save-state capture") ||
      !check(assertBlocked(genplusgx::EmulationCommand::simple(
        genplusgx::EmulationCommandType::frameAdvance, 11U)),
      "Hardcore accepted frame advance") ||
      !check(assertBlocked(genplusgx::EmulationCommand::slowMotion(12U, true)),
        "Hardcore accepted slow motion") ||
      !check(assertBlocked(genplusgx::EmulationCommand::rewinding(13U, true)),
        "Hardcore accepted rewind") ||
      !check(assertBlocked(genplusgx::EmulationCommand::updateSpeedSettings(
        14U, {.normalPercent = 75U, .slowMotionPercent = 50U,
          .fastForwardPercent = 400U})),
        "Hardcore accepted a below-real-time normal speed")) {
    return 5;
  }
  if (!check(worker.submit(genplusgx::EmulationCommand::updateSpeedSettings(
        15U, {.normalPercent = 125U, .slowMotionPercent = 50U,
          .fastForwardPercent = 400U})),
        "Hardcore accelerated speed command was not queued") ||
      !check(waitForOperation(worker, 15U)->succeeded(),
        "Hardcore incorrectly rejected an accelerated normal speed") ||
      !check(worker.submit(genplusgx::EmulationCommand::fastForward(16U, true)),
        "Hardcore fast-forward command was not queued")) {
    return 5;
  }
  const auto fastForward = waitForOperation(worker, 16U);
  if (!check(fastForward && fastForward->succeeded(),
        "Hardcore incorrectly rejected allowed fast-forward")) {
    return 5;
  }

  if (!check(worker.submit(genplusgx::EmulationCommand::simple(
        genplusgx::EmulationCommandType::achievementLogout, 20U)) &&
      waitForOperation(worker, 20U)->succeeded(),
      "Achievement logout did not complete") ||
      !check(worker.stop(), "Achievement worker did not stop cleanly")) {
    return 6;
  }
  return 0;
}
