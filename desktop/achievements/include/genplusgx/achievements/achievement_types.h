#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace genplusgx::achievements {

enum class ConnectionState : std::uint8_t {
  disabled,
  signedOut,
  signingIn,
  signedIn,
  loadingGame,
  active,
  offline,
  error,
};

enum class AchievementBucket : std::uint8_t {
  locked,
  unlocked,
  unsupported,
  unofficial,
  activeChallenge,
  almostThere,
  unsynced,
  unknown,
};

struct Achievement final {
  std::uint32_t id{0U};
  std::string title;
  std::string description;
  std::string measuredProgress;
  std::string badgeUrl;
  std::uint32_t points{0U};
  float measuredPercent{0.0F};
  AchievementBucket bucket{AchievementBucket::unknown};
  bool unlockedSoftcore{false};
  bool unlockedHardcore{false};

  [[nodiscard]] bool operator==(const Achievement&) const = default;
};

struct Snapshot final {
  ConnectionState state{ConnectionState::disabled};
  std::string username;
  std::string displayName;
  std::string gameTitle;
  std::string richPresence;
  std::string detail;
  std::uint32_t userScore{0U};
  std::uint32_t userSoftcoreScore{0U};
  std::uint32_t gameId{0U};
  std::uint32_t unlockedCount{0U};
  std::uint32_t achievementCount{0U};
  std::uint32_t unlockedPoints{0U};
  std::uint32_t totalPoints{0U};
  bool enabled{false};
  bool authenticated{false};
  bool gameLoaded{false};
  bool hardcore{false};
  bool disconnected{false};
  std::vector<Achievement> achievements;

  [[nodiscard]] bool operator==(const Snapshot&) const = default;
};

enum class EventType : std::uint8_t {
  snapshotChanged,
  loginSucceeded,
  loginFailed,
  gameLoaded,
  gameLoadFailed,
  achievementUnlocked,
  leaderboardStarted,
  leaderboardFailed,
  leaderboardSubmitted,
  gameCompleted,
  disconnected,
  reconnected,
  resetRequested,
  serverError,
};

struct Event final {
  EventType type{EventType::snapshotChanged};
  Snapshot snapshot;
  std::uint32_t relatedId{0U};
  std::string title;
  std::string detail;
  // Present only on loginSucceeded. Callers must move it immediately into the
  // platform credential store and must never log or persist it as plain text.
  std::string sessionToken;
};

[[nodiscard]] const char* connectionStateName(ConnectionState state) noexcept;

} // namespace genplusgx::achievements
