#include "genplusgx/achievements/achievement_runtime.h"

#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
#include <rc_client.h>
#include <rc_error.h>
#include <rc_hash.h>
#endif

#include <QFile>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace genplusgx::achievements {
namespace {

#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
constexpr std::size_t maximumAchievementEntries = 256U;
constexpr std::size_t maximumTitleBytes = 256U;
constexpr std::size_t maximumDescriptionBytes = 1'024U;
constexpr std::size_t maximumStatusBytes = 2'048U;
constexpr std::size_t maximumUrlBytes = 8U * 1'024U;

std::string pathUtf8(const std::filesystem::path& path)
{
  const auto text = path.u8string();
  return {reinterpret_cast<const char*>(text.data()), text.size()};
}

AchievementBucket convertBucket(std::uint8_t bucket) noexcept
{
  switch (bucket) {
    case RC_CLIENT_ACHIEVEMENT_BUCKET_LOCKED:
      return AchievementBucket::locked;
    case RC_CLIENT_ACHIEVEMENT_BUCKET_UNLOCKED:
    case RC_CLIENT_ACHIEVEMENT_BUCKET_RECENTLY_UNLOCKED:
      return AchievementBucket::unlocked;
    case RC_CLIENT_ACHIEVEMENT_BUCKET_UNSUPPORTED:
      return AchievementBucket::unsupported;
    case RC_CLIENT_ACHIEVEMENT_BUCKET_UNOFFICIAL:
      return AchievementBucket::unofficial;
    case RC_CLIENT_ACHIEVEMENT_BUCKET_ACTIVE_CHALLENGE:
      return AchievementBucket::activeChallenge;
    case RC_CLIENT_ACHIEVEMENT_BUCKET_ALMOST_THERE:
      return AchievementBucket::almostThere;
    case RC_CLIENT_ACHIEVEMENT_BUCKET_UNSYNCED:
      return AchievementBucket::unsynced;
    default:
      return AchievementBucket::unknown;
  }
}

std::string safeString(const char* value, std::size_t maximumBytes)
{
  if (value == nullptr) {
    return {};
  }
  const std::string_view view{value};
  return std::string{view.substr(0U, maximumBytes)};
}
#endif

} // namespace

class Runtime::Private final {
public:
  Private(std::shared_ptr<ServerBridge> bridgeValue, MemoryReader reader)
      : bridge(std::move(bridgeValue)), memoryReader(std::move(reader))
  {
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
    client = rc_client_create(&Private::readMemory, &Private::serverCall);
    if (client == nullptr) {
      current.state = ConnectionState::error;
      current.detail = "The RetroAchievements runtime could not be created.";
      return;
    }
    rc_client_set_userdata(client, this);
    rc_client_set_event_handler(client, &Private::handleClientEvent);
    rc_client_set_allow_background_memory_reads(client, 0);

    rc_hash_callbacks_t callbacks{};
    callbacks.filereader.open = &Private::openFile;
    callbacks.filereader.seek = &Private::seekFile;
    callbacks.filereader.tell = &Private::tellFile;
    callbacks.filereader.read = &Private::readFile;
    callbacks.filereader.close = &Private::closeFile;
    rc_hash_get_default_cdreader(&callbacks.cdreader);
    rc_client_set_hash_callbacks(client, &callbacks);
#else
    current.state = ConnectionState::disabled;
    current.detail = "This build does not include RetroAchievements support.";
#endif
  }

  ~Private()
  {
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
    if (client != nullptr) {
      rc_client_destroy(client);
      client = nullptr;
    }
    pending.clear();
#endif
    if (bridge) {
      bridge->clear();
    }
  }

#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  struct PendingCall final {
    rc_client_server_callback_t callback{nullptr};
    void* callbackData{nullptr};
  };

  static std::uint32_t RC_CCONV readMemory(std::uint32_t address,
    std::uint8_t* output,
    std::uint32_t bytes,
    rc_client_t* clientValue)
  {
    auto* self = static_cast<Private*>(rc_client_get_userdata(clientValue));
    if (self == nullptr || output == nullptr || bytes == 0U ||
        !self->memoryReader) {
      return 0U;
    }
    return self->memoryReader(address, {output, bytes});
  }

  static void RC_CCONV serverCall(const rc_api_request_t* request,
    rc_client_server_callback_t callback,
    void* callbackData,
    rc_client_t* clientValue)
  {
    auto* self = static_cast<Private*>(rc_client_get_userdata(clientValue));
    if (self == nullptr || request == nullptr || callback == nullptr) {
      return;
    }
    self->queueServerCall(*request, callback, callbackData);
  }

  void queueServerCall(const rc_api_request_t& request,
    rc_client_server_callback_t callback,
    void* callbackData)
  {
    if (!bridge || pending.size() >= Runtime::maximumPendingRequests) {
      const rc_api_server_response_t response{
        .body = nullptr,
        .body_length = 0U,
        .http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR,
      };
      callback(&response, callbackData);
      return;
    }
    const auto id = nextRequestId++;
    ServerRequest bridged{
      .id = id,
      .url = safeString(request.url, maximumUrlBytes + 1U),
      .postData = safeString(request.post_data, maximumServerBodyBytes + 1U),
      .contentType = safeString(request.content_type, 1'025U),
    };
    if (!bridge->submitRequest(std::move(bridged))) {
      const rc_api_server_response_t response{
        .body = nullptr,
        .body_length = 0U,
        .http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR,
      };
      callback(&response, callbackData);
      return;
    }
    pending.emplace(id, PendingCall{callback, callbackData});
  }

  static void RC_CCONV loginCallback(int result,
    const char* errorMessage,
    rc_client_t*,
    void* userdata)
  {
    auto* self = static_cast<Private*>(userdata);
    if (self == nullptr) {
      return;
    }
    if (result != RC_OK) {
      self->current.state = ConnectionState::signedOut;
      self->current.authenticated = false;
      self->current.detail = safeString(errorMessage != nullptr
        ? errorMessage : rc_error_str(result), maximumStatusBytes);
      self->pushEvent(EventType::loginFailed, 0U, {}, self->current.detail);
      return;
    }
    self->current.state = ConnectionState::signedIn;
    self->current.authenticated = true;
    self->current.detail.clear();
    self->refreshSnapshot();
    Event event;
    event.type = EventType::loginSucceeded;
    event.snapshot = self->current;
    if (const auto* user = rc_client_get_user_info(self->client); user != nullptr) {
      event.sessionToken = safeString(user->token, maximumStatusBytes);
    }
    self->appendEvent(std::move(event));
  }

  static void RC_CCONV gameCallback(int result,
    const char* errorMessage,
    rc_client_t*,
    void* userdata)
  {
    auto* self = static_cast<Private*>(userdata);
    if (self == nullptr) {
      return;
    }
    if (result != RC_OK) {
      self->gameRequested = false;
      self->current.state = self->current.authenticated
        ? ConnectionState::signedIn : ConnectionState::signedOut;
      self->current.gameLoaded = false;
      self->current.detail = safeString(errorMessage != nullptr
        ? errorMessage : rc_error_str(result), maximumStatusBytes);
      self->pushEvent(EventType::gameLoadFailed, 0U, {}, self->current.detail);
      return;
    }
    self->current.state = ConnectionState::active;
    self->current.gameLoaded = true;
    self->current.detail.clear();
    self->refreshSnapshot();
    self->pushEvent(EventType::gameLoaded);
  }

  static void RC_CCONV mediaCallback(int result,
    const char* errorMessage,
    rc_client_t*,
    void* userdata)
  {
    auto* self = static_cast<Private*>(userdata);
    if (self != nullptr && result != RC_OK) {
      self->current.detail = safeString(errorMessage != nullptr
        ? errorMessage : rc_error_str(result), maximumStatusBytes);
      self->pushEvent(EventType::serverError, 0U, {}, self->current.detail);
    }
  }

  static void RC_CCONV handleClientEvent(
    const rc_client_event_t* event,
    rc_client_t* clientValue)
  {
    auto* self = static_cast<Private*>(rc_client_get_userdata(clientValue));
    if (self == nullptr || event == nullptr) {
      return;
    }
    switch (event->type) {
      case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
        self->refreshSnapshot();
        self->pushEvent(EventType::achievementUnlocked,
          event->achievement != nullptr ? event->achievement->id : 0U,
          event->achievement != nullptr
            ? safeString(event->achievement->title, maximumTitleBytes)
            : std::string{});
        break;
      case RC_CLIENT_EVENT_LEADERBOARD_STARTED:
        self->pushEvent(EventType::leaderboardStarted,
          event->leaderboard != nullptr ? event->leaderboard->id : 0U,
          event->leaderboard != nullptr
            ? safeString(event->leaderboard->title, maximumTitleBytes)
            : std::string{});
        break;
      case RC_CLIENT_EVENT_LEADERBOARD_FAILED:
        self->pushEvent(EventType::leaderboardFailed,
          event->leaderboard != nullptr ? event->leaderboard->id : 0U,
          event->leaderboard != nullptr
            ? safeString(event->leaderboard->title, maximumTitleBytes)
            : std::string{});
        break;
      case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED:
        self->pushEvent(EventType::leaderboardSubmitted,
          event->leaderboard != nullptr ? event->leaderboard->id : 0U,
          event->leaderboard != nullptr
            ? safeString(event->leaderboard->title, maximumTitleBytes)
            : std::string{});
        break;
      case RC_CLIENT_EVENT_GAME_COMPLETED:
      case RC_CLIENT_EVENT_SUBSET_COMPLETED:
        self->refreshSnapshot();
        self->pushEvent(EventType::gameCompleted);
        break;
      case RC_CLIENT_EVENT_DISCONNECTED:
        self->current.disconnected = true;
        self->current.state = ConnectionState::offline;
        self->pushEvent(EventType::disconnected);
        break;
      case RC_CLIENT_EVENT_RECONNECTED:
        self->current.disconnected = false;
        self->current.state = self->current.gameLoaded
          ? ConnectionState::active : ConnectionState::signedIn;
        self->pushEvent(EventType::reconnected);
        break;
      case RC_CLIENT_EVENT_RESET:
        self->pushEvent(EventType::resetRequested);
        break;
      case RC_CLIENT_EVENT_SERVER_ERROR:
        self->pushEvent(EventType::serverError,
          event->server_error != nullptr ? event->server_error->related_id : 0U,
          {}, event->server_error != nullptr
            ? safeString(event->server_error->error_message, maximumStatusBytes)
            : std::string{});
        break;
      default:
        break;
    }
  }

  static void* RC_CCONV openFile(const char* path)
  {
    if (path == nullptr) {
      return nullptr;
    }
    auto file = std::make_unique<QFile>(QString::fromUtf8(path));
    if (!file->open(QIODevice::ReadOnly)) {
      return nullptr;
    }
    return file.release();
  }

  static void RC_CCONV seekFile(void* handle, std::int64_t offset, int origin)
  {
    auto* file = static_cast<QFile*>(handle);
    if (file == nullptr) {
      return;
    }
    qint64 position = static_cast<qint64>(offset);
    if (origin == SEEK_CUR) {
      position += file->pos();
    } else if (origin == SEEK_END) {
      position += file->size();
    }
    static_cast<void>(file->seek(position));
  }

  static std::int64_t RC_CCONV tellFile(void* handle)
  {
    const auto* file = static_cast<QFile*>(handle);
    return file == nullptr ? -1 : file->pos();
  }

  static std::size_t RC_CCONV readFile(
    void* handle, void* output, std::size_t requested)
  {
    auto* file = static_cast<QFile*>(handle);
    if (file == nullptr || output == nullptr ||
        requested > static_cast<std::size_t>(std::numeric_limits<qint64>::max())) {
      return 0U;
    }
    const auto count = file->read(
      static_cast<char*>(output), static_cast<qint64>(requested));
    return count < 0 ? 0U : static_cast<std::size_t>(count);
  }

  static void RC_CCONV closeFile(void* handle)
  {
    delete static_cast<QFile*>(handle);
  }
#endif

  void appendEvent(Event event)
  {
    if (events.size() >= Runtime::maximumPendingEvents) {
      events.erase(events.begin());
    }
    events.push_back(std::move(event));
  }

  void pushEvent(EventType type,
    std::uint32_t relatedId = 0U,
    std::string title = {},
    std::string detail = {})
  {
    Event event{
      .type = type,
      .snapshot = buildSnapshot(),
      .relatedId = relatedId,
      .title = std::move(title),
      .detail = std::move(detail),
      .sessionToken = {},
    };
    appendEvent(std::move(event));
  }

  Snapshot buildSnapshot()
  {
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
    if (client == nullptr) {
      return current;
    }
    if (const auto* user = rc_client_get_user_info(client); user != nullptr) {
      current.authenticated = true;
      current.username = safeString(user->username, maximumUsernameBytes);
      current.displayName = safeString(user->display_name, maximumTitleBytes);
      current.userScore = user->score;
      current.userSoftcoreScore = user->score_softcore;
    }
    if (const auto* game = rc_client_get_game_info(client);
        game != nullptr && rc_client_is_game_loaded(client)) {
      current.gameLoaded = true;
      current.gameId = game->id;
      current.gameTitle = safeString(game->title, maximumTitleBytes);
      rc_client_user_game_summary_t summary{};
      rc_client_get_user_game_summary(client, &summary);
      current.unlockedCount = summary.num_unlocked_achievements;
      current.achievementCount = summary.num_core_achievements;
      current.unlockedPoints = summary.points_unlocked;
      current.totalPoints = summary.points_core;

      std::array<char, 512U> presence{};
      if (rc_client_has_rich_presence(client)) {
        rc_client_get_rich_presence_message(
          client, presence.data(), presence.size());
        current.richPresence = presence.data();
      } else {
        current.richPresence.clear();
      }

      current.achievements.clear();
      auto* list = rc_client_create_achievement_list(client,
        RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE_AND_UNOFFICIAL,
        RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
      if (list != nullptr) {
        for (std::uint32_t bucketIndex = 0U;
             bucketIndex < list->num_buckets; ++bucketIndex) {
          const auto& bucket = list->buckets[bucketIndex];
          for (std::uint32_t index = 0U;
               index < bucket.num_achievements; ++index) {
            if (current.achievements.size() >= maximumAchievementEntries) {
              break;
            }
            const auto* item = bucket.achievements[index];
            if (item == nullptr) {
              continue;
            }
            current.achievements.push_back(Achievement{
              .id = item->id,
              .title = safeString(item->title, maximumTitleBytes),
              .description = safeString(
                item->description, maximumDescriptionBytes),
              .measuredProgress = safeString(item->measured_progress, 128U),
              .badgeUrl = safeString(item->badge_url, maximumUrlBytes),
              .points = item->points,
              .measuredPercent = item->measured_percent,
              .bucket = convertBucket(bucket.bucket_type),
              .unlockedSoftcore =
                (item->unlocked & RC_CLIENT_ACHIEVEMENT_UNLOCKED_SOFTCORE) != 0,
              .unlockedHardcore =
                (item->unlocked & RC_CLIENT_ACHIEVEMENT_UNLOCKED_HARDCORE) != 0,
            });
          }
        }
        rc_client_destroy_achievement_list(list);
      }
    }
    current.hardcore = settings.enabled && current.authenticated &&
      gameRequested && rc_client_get_hardcore_enabled(client) != 0;
#endif
    return current;
  }

  void refreshSnapshot() { current = buildSnapshot(); }

  std::shared_ptr<ServerBridge> bridge;
  MemoryReader memoryReader;
  Settings settings;
  Snapshot current;
  std::vector<Event> events;
  std::string pendingPath;
  std::uint64_t frameCounter{0U};
  bool gameRequested{false};
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  rc_client_t* client{nullptr};
  std::unordered_map<std::uint64_t, PendingCall> pending;
  std::uint64_t nextRequestId{1U};
#endif
};

Runtime::Runtime(std::shared_ptr<ServerBridge> bridge, MemoryReader memoryReader)
    : private_(std::make_unique<Private>(std::move(bridge), std::move(memoryReader)))
{
}

Runtime::~Runtime() = default;

void Runtime::configure(Settings settings)
{
  if (!validateSettings(settings)) {
    private_->current.state = ConnectionState::error;
    private_->current.detail = "The achievements settings are invalid.";
    private_->pushEvent(EventType::serverError, 0U, {}, private_->current.detail);
    return;
  }
  private_->settings = std::move(settings);
  private_->current.enabled = private_->settings.enabled;
  private_->current.username = private_->settings.username;
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  if (private_->client == nullptr) {
    return;
  }
  rc_client_set_unofficial_enabled(
    private_->client, private_->settings.unofficial ? 1 : 0);
  rc_client_set_encore_mode_enabled(
    private_->client, private_->settings.encore ? 1 : 0);
  rc_client_set_hardcore_enabled(
    private_->client, private_->settings.hardcore ? 1 : 0);
  if (!private_->settings.enabled) {
    rc_client_unload_game(private_->client);
    rc_client_logout(private_->client);
    private_->pending.clear();
    private_->gameRequested = false;
    private_->current = {};
    private_->current.state = ConnectionState::disabled;
  } else if (!private_->current.authenticated) {
    private_->current.state = ConnectionState::signedOut;
  }
#else
  private_->current.enabled = false;
  private_->current.state = ConnectionState::disabled;
#endif
  private_->refreshSnapshot();
  private_->pushEvent(EventType::snapshotChanged);
}

void Runtime::loginWithPassword(std::string username, std::string password)
{
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  if (!private_->settings.enabled || !validUsername(username) || password.empty() ||
      private_->client == nullptr) {
    private_->current.detail = "Enter a valid username and password after enabling achievements.";
    private_->pushEvent(EventType::loginFailed, 0U, {}, private_->current.detail);
    std::fill(password.begin(), password.end(), '\0');
    return;
  }
  private_->current.state = ConnectionState::signingIn;
  private_->current.username = username;
  private_->pushEvent(EventType::snapshotChanged);
  rc_client_begin_login_with_password(private_->client,
    username.c_str(), password.c_str(), &Private::loginCallback, private_.get());
  std::fill(password.begin(), password.end(), '\0');
#else
  static_cast<void>(username);
  std::fill(password.begin(), password.end(), '\0');
#endif
}

void Runtime::loginWithToken(std::string username, std::string token)
{
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  if (!private_->settings.enabled || !validUsername(username) || token.empty() ||
      private_->client == nullptr) {
    std::fill(token.begin(), token.end(), '\0');
    return;
  }
  private_->current.state = ConnectionState::signingIn;
  private_->current.username = username;
  private_->pushEvent(EventType::snapshotChanged);
  rc_client_begin_login_with_token(private_->client,
    username.c_str(), token.c_str(), &Private::loginCallback, private_.get());
  std::fill(token.begin(), token.end(), '\0');
#else
  static_cast<void>(username);
  std::fill(token.begin(), token.end(), '\0');
#endif
}

void Runtime::logout()
{
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  if (private_->client != nullptr) {
    rc_client_unload_game(private_->client);
    rc_client_logout(private_->client);
  }
  private_->pending.clear();
#endif
  private_->gameRequested = false;
  private_->current = {};
  private_->current.state = private_->settings.enabled
    ? ConnectionState::signedOut : ConnectionState::disabled;
  private_->current.username = private_->settings.username;
  private_->current.enabled = private_->settings.enabled;
  private_->pushEvent(EventType::snapshotChanged);
}

void Runtime::loadGame(std::uint32_t consoleId, const std::filesystem::path& path)
{
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  if (!private_->settings.enabled || private_->client == nullptr ||
      consoleId == 0U || path.empty()) {
    return;
  }
  private_->pendingPath = pathUtf8(path);
  private_->gameRequested = true;
  private_->current.state = ConnectionState::loadingGame;
  private_->current.gameLoaded = false;
  private_->current.detail.clear();
  private_->pushEvent(EventType::snapshotChanged);
  rc_client_begin_identify_and_load_game(private_->client, consoleId,
    private_->pendingPath.c_str(), nullptr, 0U,
    &Private::gameCallback, private_.get());
#else
  static_cast<void>(consoleId);
  static_cast<void>(path);
#endif
}

void Runtime::changeMedia(const std::filesystem::path& path)
{
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  if (private_->client == nullptr || !private_->current.gameLoaded || path.empty()) {
    return;
  }
  private_->pendingPath = pathUtf8(path);
  rc_client_begin_identify_and_change_media(private_->client,
    private_->pendingPath.c_str(), nullptr, 0U,
    &Private::mediaCallback, private_.get());
#else
  static_cast<void>(path);
#endif
}

void Runtime::unloadGame()
{
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  if (private_->client != nullptr) {
    rc_client_unload_game(private_->client);
  }
#endif
  private_->gameRequested = false;
  private_->current.gameLoaded = false;
  private_->current.gameId = 0U;
  private_->current.gameTitle.clear();
  private_->current.richPresence.clear();
  private_->current.achievements.clear();
  private_->current.state = private_->current.authenticated
    ? ConnectionState::signedIn
    : (private_->settings.enabled
        ? ConnectionState::signedOut : ConnectionState::disabled);
  private_->pushEvent(EventType::snapshotChanged);
}

void Runtime::reset()
{
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  if (private_->client != nullptr && private_->gameRequested) {
    rc_client_reset(private_->client);
  }
#endif
}

void Runtime::doFrame()
{
  processServerResponses();
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  if (private_->client != nullptr && private_->settings.enabled &&
      private_->current.gameLoaded) {
    rc_client_do_frame(private_->client);
    ++private_->frameCounter;
    if ((private_->frameCounter % 60U) == 0U) {
      private_->refreshSnapshot();
      private_->pushEvent(EventType::snapshotChanged);
    }
  }
#endif
}

void Runtime::idle()
{
  processServerResponses();
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  if (private_->client != nullptr && private_->settings.enabled) {
    rc_client_idle(private_->client);
  }
#endif
}

void Runtime::processServerResponses()
{
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  if (!private_->bridge) {
    return;
  }
  while (auto response = private_->bridge->takeResponse()) {
    const auto found = private_->pending.find(response->id);
    if (found == private_->pending.end()) {
      continue;
    }
    const auto callback = found->second;
    private_->pending.erase(found);
    const rc_api_server_response_t serverResponse{
      .body = response->body.empty() ? nullptr : response->body.data(),
      .body_length = response->body.size(),
      .http_status_code = response->httpStatusCode,
    };
    callback.callback(&serverResponse, callback.callbackData);
  }
#endif
}

std::vector<std::uint8_t> Runtime::serializeProgress()
{
  std::vector<std::uint8_t> output;
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  if (private_->client == nullptr || !private_->current.gameLoaded) {
    return output;
  }
  const auto size = rc_client_progress_size(private_->client);
  if (size == 0U || size > maximumProgressBytes) {
    return output;
  }
  output.resize(size);
  if (rc_client_serialize_progress_sized(
        private_->client, output.data(), output.size()) != RC_OK) {
    output.clear();
  }
#endif
  return output;
}

bool Runtime::deserializeProgress(std::span<const std::uint8_t> data)
{
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  if (hardcoreActive() || private_->client == nullptr ||
      !private_->current.gameLoaded || data.empty() ||
      data.size() > maximumProgressBytes) {
    return false;
  }
  return rc_client_deserialize_progress_sized(
    private_->client, data.data(), data.size()) == RC_OK;
#else
  static_cast<void>(data);
  return false;
#endif
}

Snapshot Runtime::snapshot()
{
  private_->refreshSnapshot();
  return private_->current;
}

std::vector<Event> Runtime::takeEvents()
{
  auto events = std::move(private_->events);
  private_->events.clear();
  return events;
}

bool Runtime::enabled() const noexcept
{
  return private_->settings.enabled;
}

bool Runtime::authenticated() const noexcept
{
  return private_->current.authenticated;
}

bool Runtime::gameActive() const noexcept
{
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  return private_->client != nullptr && private_->settings.enabled &&
    private_->current.gameLoaded;
#else
  return false;
#endif
}

bool Runtime::gameIdentificationPending() const noexcept
{
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  return private_->client != nullptr && private_->settings.enabled &&
    private_->gameRequested && !private_->current.gameLoaded &&
    private_->current.state == ConnectionState::loadingGame;
#else
  return false;
#endif
}

bool Runtime::hardcoreActive() const noexcept
{
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  return private_->client != nullptr && private_->settings.enabled &&
    private_->current.authenticated && private_->current.gameLoaded &&
    rc_client_get_hardcore_enabled(private_->client) != 0;
#else
  return false;
#endif
}

bool Runtime::pauseAllowed(std::uint32_t* framesRemaining)
{
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  if (!hardcoreActive()) {
    if (framesRemaining != nullptr) {
      *framesRemaining = 0U;
    }
    return true;
  }
  return rc_client_can_pause(private_->client, framesRemaining) != 0;
#else
  if (framesRemaining != nullptr) {
    *framesRemaining = 0U;
  }
  return true;
#endif
}

} // namespace genplusgx::achievements
