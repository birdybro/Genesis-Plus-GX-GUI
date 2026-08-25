#pragma once

#include "genplusgx/input_snapshot.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace genplusgx::input {

struct ControllerBinding final {
  SDL_GamepadButton button{SDL_GAMEPAD_BUTTON_INVALID};
  InputButton input{InputButton::a};

  friend bool operator==(const ControllerBinding&, const ControllerBinding&) = default;
};

struct ControllerInfo final {
  std::uint32_t instanceId{0U};
  std::string name;
  std::size_t player{0U};

  friend bool operator==(const ControllerInfo&, const ControllerInfo&) = default;
};

enum class ControllerInputError {
  none,
  alreadyInitialized,
  subsystemInitializationFailed,
  wrongThread,
};

struct ControllerInputStatus final {
  ControllerInputError error{ControllerInputError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == ControllerInputError::none;
  }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

[[nodiscard]] std::vector<ControllerBinding> defaultGenesisControllerBindings();

class ControllerInput final {
public:
  using SnapshotSink = std::function<void(const InputSnapshot&)>;
  using ConnectionSink = std::function<void(const ControllerInfo&, bool connected)>;

  ControllerInput();
  ~ControllerInput();

  ControllerInput(const ControllerInput&) = delete;
  ControllerInput& operator=(const ControllerInput&) = delete;
  ControllerInput(ControllerInput&&) = delete;
  ControllerInput& operator=(ControllerInput&&) = delete;

  [[nodiscard]] ControllerInputStatus initialize();
  [[nodiscard]] ControllerInputStatus shutdown();
  [[nodiscard]] bool isInitialized() const noexcept;

  void setSnapshotSink(SnapshotSink sink);
  void setConnectionSink(ConnectionSink sink);

  // Pumps only SDL's gamepad event range and leaves unrelated events queued.
  [[nodiscard]] std::size_t pollEvents();
  // Public to permit deterministic event injection without physical hardware.
  [[nodiscard]] bool processEvent(const SDL_Event& event);

  [[nodiscard]] std::vector<ControllerInfo> controllers() const;
  [[nodiscard]] InputSnapshot snapshot() const noexcept;
  [[nodiscard]] bool assignPlayer(std::uint32_t instanceId, std::size_t player);

  void setDeadzone(std::int16_t deadzone);
  [[nodiscard]] std::int16_t deadzone() const noexcept;

private:
  class Private;
  std::unique_ptr<Private> private_;
};

} // namespace genplusgx::input
