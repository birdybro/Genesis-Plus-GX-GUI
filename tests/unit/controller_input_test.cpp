#include "genplusgx/input/controller_input.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

bool check(bool condition, const std::string& message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

SDL_JoystickID attachVirtualGamepad(const char* name)
{
  SDL_VirtualJoystickDesc description{};
  SDL_INIT_INTERFACE(&description);
  description.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  description.vendor_id = 0x1209U;
  description.product_id = 0x0001U;
  description.naxes = SDL_GAMEPAD_AXIS_COUNT;
  description.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  description.name = name;
  for (unsigned index = 0U; index < SDL_GAMEPAD_BUTTON_COUNT; ++index) {
    description.button_mask |= (1U << index);
  }
  for (unsigned index = 0U; index < SDL_GAMEPAD_AXIS_COUNT; ++index) {
    description.axis_mask |= (1U << index);
  }
  return SDL_AttachVirtualJoystick(&description);
}

SDL_Event deviceEvent(SDL_EventType type, SDL_JoystickID instanceId)
{
  SDL_Event event{};
  event.type = type;
  event.gdevice.which = instanceId;
  return event;
}

SDL_Event buttonEvent(
  SDL_EventType type,
  SDL_JoystickID instanceId,
  SDL_GamepadButton button)
{
  SDL_Event event{};
  event.type = type;
  event.gbutton.which = instanceId;
  event.gbutton.button = static_cast<Uint8>(button);
  event.gbutton.down = type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  return event;
}

SDL_Event axisEvent(
  SDL_JoystickID instanceId,
  SDL_GamepadAxis axis,
  Sint16 value)
{
  SDL_Event event{};
  event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
  event.gaxis.which = instanceId;
  event.gaxis.axis = static_cast<Uint8>(axis);
  event.gaxis.value = value;
  return event;
}

bool hasController(
  const genplusgx::input::ControllerInput& input,
  SDL_JoystickID instanceId)
{
  const auto controllers = input.controllers();
  return std::any_of(controllers.begin(), controllers.end(),
    [instanceId](const auto& controller) {
      return controller.instanceId == static_cast<std::uint32_t>(instanceId);
    });
}

} // namespace

int main()
{
  // Keep this test independent from controller hardware attached to a developer host.
  static_cast<void>(SDL_SetHint(
    SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT, "0x1209/0x0001"));

  const auto bindings = genplusgx::input::defaultGenesisControllerBindings();
  std::set<SDL_GamepadButton> physicalButtons;
  std::set<genplusgx::InputButton> logicalButtons;
  for (const auto& binding : bindings) {
    physicalButtons.insert(binding.button);
    logicalButtons.insert(binding.input);
  }
  constexpr std::array expectedBindings{
    genplusgx::input::ControllerBinding{
      SDL_GAMEPAD_BUTTON_DPAD_UP, genplusgx::InputButton::up},
    genplusgx::input::ControllerBinding{
      SDL_GAMEPAD_BUTTON_DPAD_DOWN, genplusgx::InputButton::down},
    genplusgx::input::ControllerBinding{
      SDL_GAMEPAD_BUTTON_DPAD_LEFT, genplusgx::InputButton::left},
    genplusgx::input::ControllerBinding{
      SDL_GAMEPAD_BUTTON_DPAD_RIGHT, genplusgx::InputButton::right},
    genplusgx::input::ControllerBinding{
      SDL_GAMEPAD_BUTTON_WEST, genplusgx::InputButton::a},
    genplusgx::input::ControllerBinding{
      SDL_GAMEPAD_BUTTON_SOUTH, genplusgx::InputButton::b},
    genplusgx::input::ControllerBinding{
      SDL_GAMEPAD_BUTTON_EAST, genplusgx::InputButton::c},
    genplusgx::input::ControllerBinding{
      SDL_GAMEPAD_BUTTON_NORTH, genplusgx::InputButton::x},
    genplusgx::input::ControllerBinding{
      SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, genplusgx::InputButton::y},
    genplusgx::input::ControllerBinding{
      SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, genplusgx::InputButton::z},
    genplusgx::input::ControllerBinding{
      SDL_GAMEPAD_BUTTON_BACK, genplusgx::InputButton::mode},
    genplusgx::input::ControllerBinding{
      SDL_GAMEPAD_BUTTON_START, genplusgx::InputButton::start},
  };
  if (!check(bindings.size() == 12U, "Default mapping is not a complete six-button pad") ||
      !check(physicalButtons.size() == bindings.size(),
        "Default mapping contains duplicate SDL buttons") ||
      !check(logicalButtons.size() == bindings.size(),
        "Default mapping contains duplicate logical buttons")) {
    return EXIT_FAILURE;
  }
  for (const auto& expected : expectedBindings) {
    if (!check(std::find(bindings.begin(), bindings.end(), expected) != bindings.end(),
        "Default positional gamepad mapping is incorrect")) {
      return EXIT_FAILURE;
    }
  }

  genplusgx::input::ControllerInput input;
  std::vector<genplusgx::InputSnapshot> published;
  std::vector<std::pair<genplusgx::input::ControllerInfo, bool>> connections;
  input.setSnapshotSink(
    [&published](const genplusgx::InputSnapshot& snapshot) {
      published.push_back(snapshot);
    });
  input.setConnectionSink(
    [&connections](const auto& controller, bool connected) {
      connections.emplace_back(controller, connected);
    });

  if (!check(input.initialize(), "SDL gamepad subsystem did not initialize") ||
      !check(input.isInitialized(), "Controller service did not report initialized") ||
      !check(input.initialize().error ==
          genplusgx::input::ControllerInputError::alreadyInitialized,
        "Repeated initialization did not return its typed error")) {
    return EXIT_FAILURE;
  }
  genplusgx::input::ControllerInputStatus wrongThreadStatus;
  std::thread wrongThread([&input, &wrongThreadStatus] {
    wrongThreadStatus = input.shutdown();
  });
  wrongThread.join();
  if (!check(wrongThreadStatus.error ==
        genplusgx::input::ControllerInputError::wrongThread,
      "Cross-thread shutdown was not rejected") ||
      !check(input.isInitialized(),
        "Cross-thread shutdown changed controller lifecycle state")) {
    static_cast<void>(input.shutdown());
    return EXIT_FAILURE;
  }

  const SDL_JoystickID first = attachVirtualGamepad("Genesis Test Pad One");
  if (!check(first != 0U, std::string{"Virtual controller creation failed: "} + SDL_GetError()) ||
      !check(SDL_IsGamepad(first), "Virtual joystick was not recognized as an SDL gamepad")) {
    static_cast<void>(input.shutdown());
    return EXIT_FAILURE;
  }

  // Exercise the production pump for discovery; it retrieves only the gamepad range.
  SDL_Event unrelated{};
  unrelated.type = SDL_EVENT_USER;
  static_cast<void>(SDL_PushEvent(&unrelated));
  for (int attempt = 0; attempt < 4 && !hasController(input, first); ++attempt) {
    static_cast<void>(input.pollEvents());
  }
  if (!check(hasController(input, first), "Hot-plug discovery did not open the controller") ||
      !check(input.controllers().front().player == 0U,
        "First controller was not assigned to Player 1") ||
      !check(input.controllers().front().name.find("Genesis Test Pad One") != std::string::npos,
        "Controller name was not retained") ||
      !check(input.snapshot().players[0].connected,
        "Player 1 was not connected in the controller snapshot")) {
    static_cast<void>(input.shutdown());
    static_cast<void>(SDL_DetachVirtualJoystick(first));
    return EXIT_FAILURE;
  }
  SDL_Event preserved{};
  if (!check(SDL_PeepEvents(
        &preserved, 1, SDL_GETEVENT, SDL_EVENT_USER, SDL_EVENT_USER) == 1,
        "Controller polling consumed an unrelated SDL event")) {
    static_cast<void>(input.shutdown());
    static_cast<void>(SDL_DetachVirtualJoystick(first));
    return EXIT_FAILURE;
  }

  const auto firstId = static_cast<std::uint32_t>(first);
  if (!check(input.processEvent(buttonEvent(
        SDL_EVENT_GAMEPAD_BUTTON_DOWN, first, SDL_GAMEPAD_BUTTON_WEST)),
        "Synthetic button-down event was rejected") ||
      !check(genplusgx::hasButton(
        input.snapshot().players[0].buttons, genplusgx::InputButton::a),
        "West face button did not map to Genesis A") ||
      !check(input.processEvent(buttonEvent(
        SDL_EVENT_GAMEPAD_BUTTON_DOWN, first, SDL_GAMEPAD_BUTTON_SOUTH)),
        "Second synthetic button-down event was rejected") ||
      !check(genplusgx::hasButton(
        input.snapshot().players[0].buttons, genplusgx::InputButton::b),
        "South face button did not map to Genesis B") ||
      !check(input.processEvent(buttonEvent(
        SDL_EVENT_GAMEPAD_BUTTON_UP, first, SDL_GAMEPAD_BUTTON_WEST)),
        "Synthetic button-up event was rejected") ||
      !check(!genplusgx::hasButton(
        input.snapshot().players[0].buttons, genplusgx::InputButton::a),
        "Released controller button remained active")) {
    static_cast<void>(input.shutdown());
    static_cast<void>(SDL_DetachVirtualJoystick(first));
    return EXIT_FAILURE;
  }

  static_cast<void>(input.processEvent(buttonEvent(
    SDL_EVENT_GAMEPAD_BUTTON_DOWN, first, SDL_GAMEPAD_BUTTON_DPAD_LEFT)));
  static_cast<void>(input.processEvent(buttonEvent(
    SDL_EVENT_GAMEPAD_BUTTON_DOWN, first, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)));
  if (!check(!genplusgx::hasButton(
        input.snapshot().players[0].buttons, genplusgx::InputButton::left) &&
      genplusgx::hasButton(
        input.snapshot().players[0].buttons, genplusgx::InputButton::right),
      "Digital opposite directions did not use last-input priority")) {
    static_cast<void>(input.shutdown());
    static_cast<void>(SDL_DetachVirtualJoystick(first));
    return EXIT_FAILURE;
  }
  static_cast<void>(input.processEvent(buttonEvent(
    SDL_EVENT_GAMEPAD_BUTTON_UP, first, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)));
  if (!check(genplusgx::hasButton(
        input.snapshot().players[0].buttons, genplusgx::InputButton::left),
      "Held digital direction did not recover after opposite release")) {
    static_cast<void>(input.shutdown());
    static_cast<void>(SDL_DetachVirtualJoystick(first));
    return EXIT_FAILURE;
  }

  input.setDeadzone(8'000);
  static_cast<void>(input.processEvent(axisEvent(first, SDL_GAMEPAD_AXIS_LEFTX, 7'999)));
  if (!check(input.snapshot().players[0].analogX == 0,
      "Value inside the analog deadzone was not neutral")) {
    static_cast<void>(input.shutdown());
    static_cast<void>(SDL_DetachVirtualJoystick(first));
    return EXIT_FAILURE;
  }
  static_cast<void>(input.processEvent(buttonEvent(
    SDL_EVENT_GAMEPAD_BUTTON_UP, first, SDL_GAMEPAD_BUTTON_DPAD_LEFT)));
  static_cast<void>(input.processEvent(axisEvent(first, SDL_GAMEPAD_AXIS_LEFTX, 20'000)));
  if (!check(input.snapshot().players[0].analogX == 20'000 &&
      genplusgx::hasButton(
        input.snapshot().players[0].buttons, genplusgx::InputButton::right),
      "Left stick did not provide analog and digital direction state")) {
    static_cast<void>(input.shutdown());
    static_cast<void>(SDL_DetachVirtualJoystick(first));
    return EXIT_FAILURE;
  }

  auto customAxes = genplusgx::input::defaultGenesisControllerAxisBindings();
  customAxes[1].input = genplusgx::InputButton::c;
  if (!check(input.setAxisBindings(customAxes),
        "Valid controller axis mapping was rejected") ||
      !check(input.axisBindings() == customAxes,
        "Controller axis mapping was not retained") ||
      !check(genplusgx::hasButton(
        input.snapshot().players[0].buttons, genplusgx::InputButton::c) &&
        !genplusgx::hasButton(
          input.snapshot().players[0].buttons, genplusgx::InputButton::right),
        "Live controller axis remapping was not applied")) {
    static_cast<void>(input.shutdown());
    static_cast<void>(SDL_DetachVirtualJoystick(first));
    return EXIT_FAILURE;
  }
  auto invalidAxes = customAxes;
  invalidAxes.push_back(customAxes.front());
  if (!check(!input.setAxisBindings(invalidAxes),
      "Duplicate physical axis direction was accepted")) {
    static_cast<void>(input.shutdown());
    static_cast<void>(SDL_DetachVirtualJoystick(first));
    return EXIT_FAILURE;
  }

  int capturedButtons = 0;
  input.setCaptureSink([&capturedButtons](SDL_GamepadButton button) {
    ++capturedButtons;
    return button == SDL_GAMEPAD_BUTTON_NORTH;
  });
  static_cast<void>(input.processEvent(buttonEvent(
    SDL_EVENT_GAMEPAD_BUTTON_DOWN, first, SDL_GAMEPAD_BUTTON_NORTH)));
  if (!check(capturedButtons == 1,
        "Controller capture seam did not receive the button press") ||
      !check(!genplusgx::hasButton(
        input.snapshot().players[0].buttons, genplusgx::InputButton::x),
        "Captured controller button leaked into gameplay input")) {
    static_cast<void>(input.shutdown());
    static_cast<void>(SDL_DetachVirtualJoystick(first));
    return EXIT_FAILURE;
  }
  input.setCaptureSink({});

  const SDL_JoystickID second = attachVirtualGamepad("Genesis Test Pad Two");
  if (!check(second != 0U, "Second virtual controller creation failed") ||
      !check(input.processEvent(deviceEvent(SDL_EVENT_GAMEPAD_ADDED, second)),
        "Synthetic controller-add event was rejected") ||
      !check(input.controllers().size() == 2U &&
        input.controllers()[1].player == 1U,
        "Second controller was not assigned to Player 2") ||
      !check(input.assignPlayer(firstId, 1U), "Controller reassignment failed") ||
      !check(input.controllers()[0].instanceId == static_cast<std::uint32_t>(second) &&
        input.controllers()[1].instanceId == firstId,
        "Occupied player reassignment did not swap controllers")) {
    static_cast<void>(input.shutdown());
    static_cast<void>(SDL_DetachVirtualJoystick(first));
    if (second != 0U) {
      static_cast<void>(SDL_DetachVirtualJoystick(second));
    }
    return EXIT_FAILURE;
  }

  if (!check(input.processEvent(deviceEvent(SDL_EVENT_GAMEPAD_REMOVED, first)),
        "Synthetic controller-remove event was rejected") ||
      !check(!hasController(input, first), "Removed controller remained registered") ||
      !check(!input.snapshot().players[1].connected,
        "Removed controller's assigned player remained connected") ||
      !check(connections.size() >= 3U && !connections.back().second,
        "Disconnect notification was not published") ||
      !check(!input.assignPlayer(firstId, 0U),
        "Unknown controller assignment unexpectedly succeeded")) {
    static_cast<void>(input.shutdown());
    static_cast<void>(SDL_DetachVirtualJoystick(first));
    static_cast<void>(SDL_DetachVirtualJoystick(second));
    return EXIT_FAILURE;
  }

  static_cast<void>(SDL_DetachVirtualJoystick(first));
  static_cast<void>(input.processEvent(deviceEvent(SDL_EVENT_GAMEPAD_REMOVED, second)));
  static_cast<void>(SDL_DetachVirtualJoystick(second));
  input.setSnapshotSink({});
  input.setConnectionSink({});
  if (!check(input.shutdown(), "Controller service did not shut down cleanly") ||
      !check(!input.isInitialized(), "Controller service remained initialized") ||
      !check(input.controllers().empty(), "Shutdown left controller records behind") ||
      !check(!published.empty(), "Controller state changes were never published")) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
