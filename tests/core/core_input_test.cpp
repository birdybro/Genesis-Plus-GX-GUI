#include "genplusgx/core_adapter.h"
#include "synthetic_rom.h"

extern "C" {
#include "shared.h"
}

#include <cstdint>
#include <iostream>

namespace {

using genplusgx::InputButton;

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

} // namespace

int main()
{
  const genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".bin"};
  genplusgx::CoreAdapter adapter;
  if (!check(adapter.initialize(), "Input test could not initialize the adapter") ||
      !check(adapter.loadGame(fixture.path()), "Input test could not load the fixture") ||
      !check(input.dev[0] == DEVICE_PAD3B && input.dev[4] == DEVICE_PAD3B,
        "Desktop defaults did not configure two standard gamepads")) {
    return 1;
  }

  genplusgx::InputSnapshot neutral;
  neutral.sequence = 1;
  neutral.players[0].connected = true;
  neutral.players[1].connected = true;
  if (!check(adapter.setInputSnapshot(neutral), "Neutral snapshot was rejected") ||
      !check(input.pad[0] == 0U && adapter.appliedInputSequence() == 0U,
        "Queued input mutated the core before a frame boundary") ||
      !check(adapter.runFrame(true), "Neutral input frame failed") ||
      !check(adapter.appliedInputSequence() == 1U,
        "Neutral snapshot sequence was not applied") ||
      !check(input.pad[0] == 0U && work_ram[9] == 0x7FU,
        "Controller-test ROM did not observe a released 3-button pad")) {
    return 2;
  }

  genplusgx::InputSnapshot pressed = neutral;
  pressed.sequence = 2;
  pressed.players[0].buttons = InputButton::b | InputButton::right;
  pressed.players[0].analogX = 123;
  pressed.players[0].analogY = -456;
  if (!check(adapter.setInputSnapshot(pressed), "Pressed snapshot was rejected") ||
      !check(input.pad[0] == 0U, "Pressed input changed the core between frames") ||
      !check(adapter.runFrame(true), "Pressed input frame failed") ||
      !check(input.pad[0] == (INPUT_B | INPUT_RIGHT),
        "Neutral B/right mapping did not reach core bits") ||
      !check(input.analog[0][0] == 123 && input.analog[0][1] == -456,
        "Neutral analog coordinates did not reach the first active device") ||
      !check(work_ram[9] == 0x67U,
        "Emulated 68000 program did not observe active-low B/right input")) {
    return 3;
  }

  genplusgx::InputSnapshot allButtons = pressed;
  allButtons.sequence = 3;
  allButtons.players[0].buttons =
    InputButton::up | InputButton::down | InputButton::left | InputButton::right |
    InputButton::a | InputButton::b | InputButton::c | InputButton::start |
    InputButton::x | InputButton::y | InputButton::z | InputButton::mode;
  allButtons.players[1].connected = false;
  allButtons.players[1].buttons = InputButton::a | InputButton::start;
  allButtons.players[1].analogX = 999;
  if (!check(adapter.setInputSnapshot(allButtons), "Complete button snapshot was rejected") ||
      !check(adapter.runFrame(true), "Complete button mapping frame failed") ||
      !check(input.pad[0] == 0x0FFFU, "One or more Genesis buttons mapped incorrectly") ||
      !check(input.pad[4] == 0U && input.analog[4][0] == 0,
        "Disconnected second player leaked state into core slot four")) {
    return 4;
  }

  genplusgx::InputSnapshot latest = neutral;
  latest.sequence = 5;
  latest.players[0].buttons = genplusgx::buttonMask(InputButton::c);
  genplusgx::InputSnapshot superseded = neutral;
  superseded.sequence = 4;
  superseded.players[0].buttons = genplusgx::buttonMask(InputButton::a);
  if (!check(adapter.setInputSnapshot(latest), "Newest snapshot was rejected") ||
      !check(adapter.setInputSnapshot(superseded).error ==
          genplusgx::CoreError::staleInputSnapshot,
        "An older input snapshot superseded newer state") ||
      !check(adapter.appliedInputSequence() == 3U,
        "Queued newest snapshot applied before the next frame") ||
      !check(adapter.runFrame(true), "Newest snapshot frame failed") ||
      !check(adapter.appliedInputSequence() == 5U && input.pad[0] == INPUT_C,
        "Newest queued snapshot was not applied at the frame boundary")) {
    return 5;
  }

  return adapter.shutdown() ? 0 : 6;
}
