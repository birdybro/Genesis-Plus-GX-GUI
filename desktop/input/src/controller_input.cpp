#include "genplusgx/input/controller_input.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <thread>
#include <utility>

namespace genplusgx::input {
namespace {

constexpr std::int16_t defaultDeadzone = 8'000;
constexpr std::size_t maximumEventsPerPoll = 256U;

ControllerInputStatus success()
{
  return {};
}

ControllerInputStatus failure(ControllerInputError error, std::string message)
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

std::optional<InputButton> mappedButton(SDL_GamepadButton button) noexcept
{
  switch (button) {
  case SDL_GAMEPAD_BUTTON_DPAD_UP:
    return InputButton::up;
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    return InputButton::down;
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    return InputButton::left;
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
    return InputButton::right;
  case SDL_GAMEPAD_BUTTON_WEST:
    return InputButton::a;
  case SDL_GAMEPAD_BUTTON_SOUTH:
    return InputButton::b;
  case SDL_GAMEPAD_BUTTON_EAST:
    return InputButton::c;
  case SDL_GAMEPAD_BUTTON_NORTH:
    return InputButton::x;
  case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
    return InputButton::y;
  case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
    return InputButton::z;
  case SDL_GAMEPAD_BUTTON_BACK:
    return InputButton::mode;
  case SDL_GAMEPAD_BUTTON_START:
    return InputButton::start;
  default:
    return std::nullopt;
  }
}

} // namespace

std::vector<ControllerBinding> defaultGenesisControllerBindings()
{
  std::vector<ControllerBinding> result;
  result.reserve(12U);
  for (int value = 0; value < SDL_GAMEPAD_BUTTON_COUNT; ++value) {
    const auto button = static_cast<SDL_GamepadButton>(value);
    if (const auto mapped = mappedButton(button)) {
      result.push_back({button, *mapped});
    }
  }
  return result;
}

class ControllerInput::Private final {
public:
  struct Device final {
    SDL_Gamepad* handle{nullptr};
    ControllerInfo info;
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons{};
    std::array<std::uint64_t, SDL_GAMEPAD_BUTTON_COUNT> pressOrder{};
    std::int16_t leftX{0};
    std::int16_t leftY{0};
  };

  using DeviceIterator = std::vector<Device>::iterator;
  using ConstDeviceIterator = std::vector<Device>::const_iterator;

  ~Private()
  {
    snapshotSink_ = {};
    connectionSink_ = {};
    static_cast<void>(shutdown());
  }

  ControllerInputStatus initialize()
  {
    if (initialized_) {
      return failure(
        ControllerInputError::alreadyInitialized,
        "The controller input service is already initialized.");
    }
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
      return failure(
        ControllerInputError::subsystemInitializationFailed,
        sdlFailureMessage("SDL could not initialize its gamepad subsystem"));
    }
    initialized_ = true;
    ownerThread_ = std::this_thread::get_id();

    int count = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&count);
    if (gamepads != nullptr) {
      for (int index = 0; index < count; ++index) {
        static_cast<void>(open(gamepads[index]));
      }
      SDL_free(gamepads);
    }
    return success();
  }

  ControllerInputStatus shutdown()
  {
    if (initialized_ && !isOwnerThread()) {
      return failure(
        ControllerInputError::wrongThread,
        "Controller input must be shut down on its initializing thread.");
    }

    while (!devices_.empty()) {
      remove(devices_.back().info.instanceId);
    }
    if (initialized_) {
      SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
      initialized_ = false;
      ownerThread_ = {};
    }
    return success();
  }

  bool open(SDL_JoystickID instanceId)
  {
    if (!initialized_ || !isOwnerThread() || find(instanceId) != devices_.end() ||
        devices_.size() >= InputSnapshot::maximumPlayers) {
      return false;
    }
    SDL_Gamepad* handle = SDL_OpenGamepad(instanceId);
    if (handle == nullptr) {
      return false;
    }

    const auto player = firstAvailablePlayer();
    if (!player) {
      SDL_CloseGamepad(handle);
      return false;
    }
    const char* name = SDL_GetGamepadName(handle);
    Device device;
    device.handle = handle;
    device.info.instanceId = static_cast<std::uint32_t>(instanceId);
    device.info.name = name == nullptr ? "Unnamed SDL gamepad" : name;
    device.info.player = *player;
    readCurrentState(device);
    devices_.push_back(std::move(device));
    rebuildSnapshot();
    if (connectionSink_) {
      connectionSink_(devices_.back().info, true);
    }
    return true;
  }

  bool remove(std::uint32_t instanceId)
  {
    const auto found = find(static_cast<SDL_JoystickID>(instanceId));
    if (found == devices_.end()) {
      return false;
    }
    const auto info = found->info;
    if (found->handle != nullptr) {
      SDL_CloseGamepad(found->handle);
    }
    devices_.erase(found);
    rebuildSnapshot();
    if (connectionSink_) {
      connectionSink_(info, false);
    }
    return true;
  }

  bool processEvent(const SDL_Event& event)
  {
    if (!initialized_ || !isOwnerThread()) {
      return false;
    }
    switch (event.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
      static_cast<void>(open(event.gdevice.which));
      return true;
    case SDL_EVENT_GAMEPAD_REMOVED:
      static_cast<void>(remove(static_cast<std::uint32_t>(event.gdevice.which)));
      return true;
    case SDL_EVENT_GAMEPAD_REMAPPED: {
      const auto found = find(event.gdevice.which);
      if (found != devices_.end()) {
        const char* name = SDL_GetGamepadName(found->handle);
        if (name != nullptr) {
          found->info.name = name;
        }
        readCurrentState(*found);
        rebuildSnapshot();
      }
      return true;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
      return applyButton(event.gbutton);
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
      return applyAxis(event.gaxis);
    default:
      return false;
    }
  }

  std::size_t pollEvents()
  {
    if (!initialized_ || !isOwnerThread()) {
      return 0U;
    }
    SDL_PumpEvents();
    std::size_t handled = 0U;
    SDL_Event event{};
    while (handled < maximumEventsPerPoll) {
      const int available = SDL_PeepEvents(
        &event,
        1,
        SDL_GETEVENT,
        SDL_EVENT_GAMEPAD_AXIS_MOTION,
        SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED);
      if (available <= 0) {
        break;
      }
      static_cast<void>(processEvent(event));
      ++handled;
    }
    return handled;
  }

  bool assignPlayer(std::uint32_t instanceId, std::size_t player)
  {
    if (!initialized_ || !isOwnerThread() || player >= InputSnapshot::maximumPlayers) {
      return false;
    }
    const auto found = find(static_cast<SDL_JoystickID>(instanceId));
    if (found == devices_.end()) {
      return false;
    }
    if (found->info.player == player) {
      return true;
    }

    const auto previousPlayer = found->info.player;
    const auto occupied = std::find_if(devices_.begin(), devices_.end(),
      [player](const Device& device) { return device.info.player == player; });
    if (occupied != devices_.end()) {
      occupied->info.player = previousPlayer;
    }
    found->info.player = player;
    rebuildSnapshot();
    return true;
  }

  void setDeadzone(std::int16_t deadzone)
  {
    if (deadzone < 0) {
      deadzone = 0;
    }
    deadzone_ = deadzone;
    for (auto& device : devices_) {
      readCurrentState(device);
    }
    rebuildSnapshot();
  }

  std::vector<ControllerInfo> controllers() const
  {
    std::vector<ControllerInfo> result;
    result.reserve(devices_.size());
    for (const auto& device : devices_) {
      result.push_back(device.info);
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
      return left.player < right.player;
    });
    return result;
  }

  void rebuildSnapshot()
  {
    InputSnapshot next;
    for (const auto& device : devices_) {
      auto& output = next.players[device.info.player];
      output.connected = true;
      output.analogX = applyDeadzone(device.leftX);
      output.analogY = applyDeadzone(device.leftY);

      for (int value = 0; value < SDL_GAMEPAD_BUTTON_COUNT; ++value) {
        if (!device.buttons[static_cast<std::size_t>(value)]) {
          continue;
        }
        if (const auto mapped = mappedButton(static_cast<SDL_GamepadButton>(value))) {
          output.buttons = static_cast<InputButtonSet>(
            output.buttons | buttonMask(*mapped));
        }
      }
      normalizeDigitalDirections(device, output.buttons);
      if (!hasDigitalHorizontal(output.buttons)) {
        if (output.analogX < 0) {
          output.buttons = output.buttons | InputButton::left;
        } else if (output.analogX > 0) {
          output.buttons = output.buttons | InputButton::right;
        }
      }
      if (!hasDigitalVertical(output.buttons)) {
        if (output.analogY < 0) {
          output.buttons = output.buttons | InputButton::up;
        } else if (output.analogY > 0) {
          output.buttons = output.buttons | InputButton::down;
        }
      }
    }
    if (next.players == snapshot_.players) {
      return;
    }
    next.sequence = snapshot_.sequence + 1U;
    snapshot_ = next;
    if (snapshotSink_) {
      snapshotSink_(snapshot_);
    }
  }

  void readCurrentState(Device& device)
  {
    for (int value = 0; value < SDL_GAMEPAD_BUTTON_COUNT; ++value) {
      device.buttons[static_cast<std::size_t>(value)] = SDL_GetGamepadButton(
        device.handle, static_cast<SDL_GamepadButton>(value));
    }
    device.leftX = SDL_GetGamepadAxis(device.handle, SDL_GAMEPAD_AXIS_LEFTX);
    device.leftY = SDL_GetGamepadAxis(device.handle, SDL_GAMEPAD_AXIS_LEFTY);
  }

  bool applyButton(const SDL_GamepadButtonEvent& event)
  {
    const auto found = find(event.which);
    if (found == devices_.end() || event.button >= SDL_GAMEPAD_BUTTON_COUNT) {
      return true;
    }
    const auto index = static_cast<std::size_t>(event.button);
    const bool down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.down;
    if (found->buttons[index] == down) {
      return true;
    }
    found->buttons[index] = down;
    if (down) {
      found->pressOrder[index] = ++inputOrder_;
    }
    rebuildSnapshot();
    return true;
  }

  bool applyAxis(const SDL_GamepadAxisEvent& event)
  {
    const auto found = find(event.which);
    if (found == devices_.end()) {
      return true;
    }
    if (event.axis == SDL_GAMEPAD_AXIS_LEFTX) {
      found->leftX = event.value;
    } else if (event.axis == SDL_GAMEPAD_AXIS_LEFTY) {
      found->leftY = event.value;
    } else {
      return true;
    }
    rebuildSnapshot();
    return true;
  }

  std::int16_t applyDeadzone(std::int16_t value) const noexcept
  {
    const auto magnitude = std::abs(static_cast<int>(value));
    return magnitude <= static_cast<int>(deadzone_) ? 0 : value;
  }

  static bool hasDigitalHorizontal(InputButtonSet buttons) noexcept
  {
    return hasButton(buttons, InputButton::left) ||
      hasButton(buttons, InputButton::right);
  }

  static bool hasDigitalVertical(InputButtonSet buttons) noexcept
  {
    return hasButton(buttons, InputButton::up) ||
      hasButton(buttons, InputButton::down);
  }

  static void normalizePair(
    const Device& device,
    InputButton negative,
    SDL_GamepadButton negativeSdl,
    InputButton positive,
    SDL_GamepadButton positiveSdl,
    InputButtonSet& buttons) noexcept
  {
    if (!hasButton(buttons, negative) || !hasButton(buttons, positive)) {
      return;
    }
    const auto negativeOrder =
      device.pressOrder[static_cast<std::size_t>(negativeSdl)];
    const auto positiveOrder =
      device.pressOrder[static_cast<std::size_t>(positiveSdl)];
    const auto remove = negativeOrder > positiveOrder ? positive : negative;
    buttons = static_cast<InputButtonSet>(buttons & ~buttonMask(remove));
  }

  static void normalizeDigitalDirections(
    const Device& device,
    InputButtonSet& buttons) noexcept
  {
    normalizePair(device,
      InputButton::left,
      SDL_GAMEPAD_BUTTON_DPAD_LEFT,
      InputButton::right,
      SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
      buttons);
    normalizePair(device,
      InputButton::up,
      SDL_GAMEPAD_BUTTON_DPAD_UP,
      InputButton::down,
      SDL_GAMEPAD_BUTTON_DPAD_DOWN,
      buttons);
  }

  std::optional<std::size_t> firstAvailablePlayer() const noexcept
  {
    for (std::size_t player = 0U; player < InputSnapshot::maximumPlayers; ++player) {
      const bool occupied = std::any_of(devices_.begin(), devices_.end(),
        [player](const Device& device) { return device.info.player == player; });
      if (!occupied) {
        return player;
      }
    }
    return std::nullopt;
  }

  DeviceIterator find(SDL_JoystickID instanceId)
  {
    return std::find_if(devices_.begin(), devices_.end(),
      [instanceId](const Device& device) {
        return device.info.instanceId == static_cast<std::uint32_t>(instanceId);
      });
  }

  ConstDeviceIterator find(SDL_JoystickID instanceId) const
  {
    return std::find_if(devices_.cbegin(), devices_.cend(),
      [instanceId](const Device& device) {
        return device.info.instanceId == static_cast<std::uint32_t>(instanceId);
      });
  }

  bool isOwnerThread() const noexcept
  {
    return ownerThread_ == std::this_thread::get_id();
  }

  std::vector<Device> devices_;
  SnapshotSink snapshotSink_;
  ConnectionSink connectionSink_;
  InputSnapshot snapshot_;
  std::thread::id ownerThread_;
  std::int16_t deadzone_{defaultDeadzone};
  std::uint64_t inputOrder_{0U};
  bool initialized_{false};
};

ControllerInput::ControllerInput()
  : private_(std::make_unique<Private>())
{
}

ControllerInput::~ControllerInput() = default;

ControllerInputStatus ControllerInput::initialize()
{
  return private_->initialize();
}

ControllerInputStatus ControllerInput::shutdown()
{
  return private_->shutdown();
}

bool ControllerInput::isInitialized() const noexcept
{
  return private_->initialized_;
}

void ControllerInput::setSnapshotSink(SnapshotSink sink)
{
  private_->snapshotSink_ = std::move(sink);
}

void ControllerInput::setConnectionSink(ConnectionSink sink)
{
  private_->connectionSink_ = std::move(sink);
}

std::size_t ControllerInput::pollEvents()
{
  return private_->pollEvents();
}

bool ControllerInput::processEvent(const SDL_Event& event)
{
  return private_->processEvent(event);
}

std::vector<ControllerInfo> ControllerInput::controllers() const
{
  return private_->controllers();
}

InputSnapshot ControllerInput::snapshot() const noexcept
{
  return private_->snapshot_;
}

bool ControllerInput::assignPlayer(std::uint32_t instanceId, std::size_t player)
{
  return private_->assignPlayer(instanceId, player);
}

void ControllerInput::setDeadzone(std::int16_t deadzone)
{
  private_->setDeadzone(deadzone);
}

std::int16_t ControllerInput::deadzone() const noexcept
{
  return private_->deadzone_;
}

} // namespace genplusgx::input
