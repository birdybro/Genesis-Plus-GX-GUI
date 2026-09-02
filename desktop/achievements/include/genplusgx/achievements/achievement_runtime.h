#pragma once

#include "genplusgx/achievements/achievement_bridge.h"
#include "genplusgx/achievements/achievement_settings.h"
#include "genplusgx/achievements/achievement_types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace genplusgx::achievements {

using MemoryReader = std::function<std::uint32_t(
  std::uint32_t, std::span<std::uint8_t>)>;

class Runtime final {
public:
  static constexpr std::size_t maximumPendingEvents = 64U;
  static constexpr std::size_t maximumPendingRequests = 32U;
  static constexpr std::size_t maximumProgressBytes = 4U * 1024U * 1024U;

  Runtime(std::shared_ptr<ServerBridge> bridge, MemoryReader memoryReader);
  ~Runtime();

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;
  Runtime(Runtime&&) = delete;
  Runtime& operator=(Runtime&&) = delete;

  void configure(Settings settings);
  void loginWithPassword(std::string username, std::string password);
  void loginWithToken(std::string username, std::string token);
  void logout();
  void loadGame(std::uint32_t consoleId, const std::filesystem::path& path);
  void changeMedia(const std::filesystem::path& path);
  void unloadGame();
  void reset();
  void doFrame();
  void idle();
  void processServerResponses();

  [[nodiscard]] std::vector<std::uint8_t> serializeProgress();
  [[nodiscard]] bool deserializeProgress(std::span<const std::uint8_t> data);
  [[nodiscard]] Snapshot snapshot();
  [[nodiscard]] std::vector<Event> takeEvents();
  [[nodiscard]] bool enabled() const noexcept;
  [[nodiscard]] bool authenticated() const noexcept;
  [[nodiscard]] bool gameActive() const noexcept;
  [[nodiscard]] bool gameIdentificationPending() const noexcept;
  [[nodiscard]] bool hardcoreActive() const noexcept;
  [[nodiscard]] bool pauseAllowed(std::uint32_t* framesRemaining = nullptr);

private:
  class Private;
  std::unique_ptr<Private> private_;
};

} // namespace genplusgx::achievements
