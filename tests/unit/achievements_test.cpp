#include "genplusgx/achievements/achievement_bridge.h"
#include "genplusgx/achievements/achievement_credentials.h"
#include "genplusgx/achievements/achievement_network_client.h"
#include "genplusgx/achievements/achievement_runtime.h"
#include "genplusgx/achievements/achievement_settings.h"
#include "genplusgx/persistence.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

} // namespace

int main(int argc, char** argv)
{
  QCoreApplication application{argc, argv};
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "Could not create achievement test root")) {
    return 1;
  }

  const auto settingsPath = std::filesystem::path{
    temporary.path().toStdString()} / "achievements.json";
  genplusgx::achievements::SettingsStore store{settingsPath};
  const auto defaults = store.load();
  const genplusgx::achievements::Settings expectedDefaults{};
  if (!check(defaults.status && defaults.settings == expectedDefaults,
        "Missing achievement settings did not use safe defaults") ||
      !check(!expectedDefaults.enabled && expectedDefaults.hardcore,
        "Achievement defaults did not opt out while preferring Hardcore") ||
      !check(genplusgx::achievements::validUsername("Player_1-Test.ok"),
        "A provider-compatible username was rejected") ||
      !check(!genplusgx::achievements::validUsername("bad user"),
        "A username containing unsupported characters was accepted")) {
    return 2;
  }

  const genplusgx::achievements::Settings settings{
    .enabled = true,
    .hardcore = false,
    .unofficial = true,
    .encore = true,
    .notifications = false,
    .username = "Player_1",
  };
  if (!check(store.save(settings), "Achievement settings save failed")) {
    return 3;
  }
  const auto roundTrip = store.load();
  const auto encoded = genplusgx::readFileBounded(
    settingsPath, genplusgx::achievements::SettingsStore::maximumFileBytes);
  const std::string serialized{encoded.data.begin(), encoded.data.end()};
  if (!check(roundTrip.status && roundTrip.settings == settings,
        "Achievement settings did not round-trip") ||
      !check(serialized.find("password") == std::string::npos &&
          serialized.find("token") == std::string::npos,
        "Achievement settings persisted credential material") ||
      !check(genplusgx::achievements::CredentialStore::keyForUsername(
          "Player_1") == "retroachievements-token-player_1",
        "Credential key normalization is unstable")) {
    return 4;
  }

  if (!check(genplusgx::achievements::NetworkClient::providerUrlAllowed(
        "https://retroachievements.org/dorequest.php") &&
      genplusgx::achievements::NetworkClient::providerUrlAllowed(
        "https://media.retroachievements.org/Badge/1.png") &&
      !genplusgx::achievements::NetworkClient::providerUrlAllowed(
        "http://retroachievements.org/dorequest.php") &&
      !genplusgx::achievements::NetworkClient::providerUrlAllowed(
        "https://retroachievements.org.evil.invalid/") &&
      !genplusgx::achievements::NetworkClient::providerUrlAllowed(
        "https://user@retroachievements.org/"),
      "RetroAchievements URL allowlist accepted an unsafe endpoint")) {
    return 5;
  }

  auto bridge = std::make_shared<genplusgx::achievements::ServerBridge>(2U);
  if (!check(bridge->submitRequest({1U, "https://retroachievements.org/a", {}, {}}) &&
      bridge->submitRequest({2U, "https://retroachievements.org/b", {}, {}}) &&
      !bridge->submitRequest({3U, "https://retroachievements.org/c", {}, {}}) &&
      bridge->metrics().requestDepth == 2U &&
      bridge->metrics().rejectedRequests == 1U,
      "Achievement request bridge was not bounded")) {
    return 6;
  }
  bridge->clear();
  if (!check(!bridge->submitRequest({4U, "https://retroachievements.org/a",
        {}, std::string(1'025U, 'x')}),
      "Achievement bridge accepted an oversized content type")) {
    return 6;
  }
  bridge->clear();

  genplusgx::achievements::NetworkClient network{bridge, "unit-test/1"};
  if (!check(bridge->submitRequest({5U, "http://retroachievements.org/a", {}, {}}),
        "Could not queue the offline transport test request")) {
    return 7;
  }
  network.pump();
  const auto failedResponse = bridge->takeResponse();
  const auto networkMetrics = network.metrics();
  if (!check(failedResponse.has_value() && failedResponse->id == 5U &&
        failedResponse->httpStatusCode == -1 &&
        networkMetrics.activeRequests == 0U &&
        networkMetrics.failedRequests == 1U &&
        !networkMetrics.lastError.empty(),
      "Unsafe/offline achievement requests did not fail deterministically")) {
    return 8;
  }

  genplusgx::achievements::Runtime runtime{bridge,
    [](std::uint32_t, std::span<std::uint8_t> output) {
      std::ranges::fill(output, 0U);
      return static_cast<std::uint32_t>(output.size());
    }};
  runtime.configure(settings);
  runtime.loginWithPassword("Player_1", "Pa$$word");
  auto request = bridge->takeRequest();
  if (!check(request.has_value() && request->id != 0U &&
        request->url.starts_with("https://retroachievements.org/") &&
        request->postData.find("r=login2") != std::string::npos &&
        request->postData.find("Pa%24%24word") != std::string::npos,
      "Password login did not produce the expected bounded provider request")) {
    return 9;
  }
  if (!check(bridge->submitResponse({
        .id = request->id,
        .httpStatusCode = 200,
        .body = "{\"Success\":true,\"User\":\"Player_1\","
          "\"AvatarUrl\":\"https://media.retroachievements.org/UserPic/PLAYER.png\","
          "\"Token\":\"ApiToken\",\"Score\":123,\"SoftcoreScore\":7,"
          "\"Messages\":0}",
      }), "Mock login response was rejected")) {
    return 10;
  }
  runtime.processServerResponses();
  auto events = runtime.takeEvents();
  const auto login = std::ranges::find_if(events, [](const auto& event) {
    return event.type == genplusgx::achievements::EventType::loginSucceeded;
  });
  if (!check(login != events.end() && login->sessionToken == "ApiToken" &&
        login->snapshot.authenticated && login->snapshot.userScore == 123U &&
        !runtime.hardcoreActive(),
      "Mock provider login did not establish the expected runtime session")) {
    return 11;
  }
  runtime.logout();
  if (!check(!runtime.authenticated() && !runtime.gameActive(),
        "Achievement logout retained runtime account or game state")) {
    return 12;
  }

  return 0;
}
