#include "genplusgx/input_snapshot.h"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>

int main()
{
  using genplusgx::InputButton;
  constexpr std::array allButtons{
    InputButton::up, InputButton::down, InputButton::left, InputButton::right,
    InputButton::a, InputButton::b, InputButton::c, InputButton::start,
    InputButton::x, InputButton::y, InputButton::z, InputButton::mode};

  std::uint16_t combined = 0;
  for (const auto button : allButtons) {
    const auto mask = genplusgx::buttonMask(button);
    if (std::popcount(mask) != 1 || (combined & mask) != 0U) {
      std::cerr << "Logical input buttons are not unique single-bit values\n";
      return 1;
    }
    combined = static_cast<std::uint16_t>(combined | mask);
  }
  if (combined != 0x0FFFU) {
    std::cerr << "Logical input set does not cover all twelve Genesis buttons\n";
    return 2;
  }

  const auto faceButtons = InputButton::a | InputButton::b | InputButton::c;
  if (!genplusgx::hasButton(faceButtons, InputButton::a) ||
      !genplusgx::hasButton(faceButtons, InputButton::b) ||
      !genplusgx::hasButton(faceButtons, InputButton::c) ||
      genplusgx::hasButton(faceButtons, InputButton::start)) {
    std::cerr << "Logical input composition or membership failed\n";
    return 3;
  }

  genplusgx::InputSnapshot snapshot;
  if (snapshot.players.size() != genplusgx::InputSnapshot::maximumPlayers ||
      snapshot.sequence != 0U) {
    std::cerr << "Neutral snapshot defaults were invalid\n";
    return 4;
  }
  for (const auto& player : snapshot.players) {
    if (player.connected || player.buttons != 0U || player.analogX != 0 ||
        player.analogY != 0) {
      std::cerr << "Neutral player default was not disconnected and released\n";
      return 5;
    }
  }

  return 0;
}
