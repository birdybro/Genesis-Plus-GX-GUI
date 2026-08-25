#include "genplusgx/input/input_aggregator.h"

#include <cstdlib>
#include <iostream>
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

int main()
{
  genplusgx::input::InputAggregator aggregator;
  std::vector<genplusgx::InputSnapshot> published;
  aggregator.setSnapshotSink(
    [&published](const genplusgx::InputSnapshot& snapshot) {
      published.push_back(snapshot);
    });

  genplusgx::InputSnapshot keyboard;
  keyboard.sequence = 1U;
  keyboard.players[0].connected = true;
  keyboard.players[0].buttons =
    genplusgx::InputButton::left | genplusgx::InputButton::a;
  if (!check(aggregator.updateKeyboard(keyboard), "Keyboard update was not accepted") ||
      !check(published.size() == 1U, "Keyboard state was not published") ||
      !check(aggregator.snapshot().sequence == 1U,
        "Aggregate sequence did not begin at one")) {
    return EXIT_FAILURE;
  }

  genplusgx::InputSnapshot controllers;
  controllers.sequence = 4U;
  controllers.players[0].connected = true;
  controllers.players[0].buttons =
    genplusgx::InputButton::right | genplusgx::InputButton::b;
  controllers.players[0].analogX = 12'000;
  controllers.players[1].connected = true;
  controllers.players[1].buttons = genplusgx::buttonMask(genplusgx::InputButton::start);
  if (!check(aggregator.updateControllers(controllers),
        "Controller update was not accepted") ||
      !check(aggregator.snapshot().sequence == 2U,
        "Aggregate sequence did not advance independently") ||
      !check(aggregator.snapshot().players[0].connected,
        "Merged Player 1 was disconnected") ||
      !check(genplusgx::hasButton(
        aggregator.snapshot().players[0].buttons, genplusgx::InputButton::a),
        "Keyboard button was lost during merge") ||
      !check(genplusgx::hasButton(
        aggregator.snapshot().players[0].buttons, genplusgx::InputButton::b),
        "Controller button was lost during merge") ||
      !check(!genplusgx::hasButton(
        aggregator.snapshot().players[0].buttons, genplusgx::InputButton::left) &&
        !genplusgx::hasButton(
          aggregator.snapshot().players[0].buttons, genplusgx::InputButton::right),
        "Cross-device opposite directions were not neutralized") ||
      !check(aggregator.snapshot().players[0].analogX == 12'000,
        "Controller analog input was not preserved") ||
      !check(aggregator.snapshot().players[1].connected &&
        genplusgx::hasButton(
          aggregator.snapshot().players[1].buttons, genplusgx::InputButton::start),
        "Player 2 controller input was not preserved")) {
    return EXIT_FAILURE;
  }

  const auto publicationCount = published.size();
  if (!check(!aggregator.updateControllers(controllers),
        "A stale source sequence was accepted") ||
      !check(published.size() == publicationCount,
        "A stale source sequence was published")) {
    return EXIT_FAILURE;
  }

  keyboard.sequence = 2U;
  keyboard.players[0].buttons = genplusgx::buttonMask(genplusgx::InputButton::a);
  if (!check(aggregator.updateKeyboard(keyboard),
        "Keyboard direction release was not accepted") ||
      !check(genplusgx::hasButton(
        aggregator.snapshot().players[0].buttons, genplusgx::InputButton::right),
        "Controller direction did not recover after keyboard release")) {
    return EXIT_FAILURE;
  }

  aggregator.clear();
  if (!check(aggregator.snapshot().sequence == 4U,
        "Clear did not publish a monotonic snapshot") ||
      !check(!aggregator.snapshot().players[0].connected &&
        !aggregator.snapshot().players[1].connected,
        "Clear left input devices connected")) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
