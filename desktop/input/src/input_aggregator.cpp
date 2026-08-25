#include "genplusgx/input/input_aggregator.h"

#include <algorithm>
#include <utility>

namespace genplusgx::input {
namespace {

InputButtonSet normalizeOppositeDirections(InputButtonSet buttons) noexcept
{
  if (hasButton(buttons, InputButton::left) &&
      hasButton(buttons, InputButton::right)) {
    buttons = static_cast<InputButtonSet>(
      buttons & ~buttonMask(InputButton::left) & ~buttonMask(InputButton::right));
  }
  if (hasButton(buttons, InputButton::up) &&
      hasButton(buttons, InputButton::down)) {
    buttons = static_cast<InputButtonSet>(
      buttons & ~buttonMask(InputButton::up) & ~buttonMask(InputButton::down));
  }
  return buttons;
}

bool sameLogicalState(const InputSnapshot& left, const InputSnapshot& right) noexcept
{
  return left.players == right.players;
}

} // namespace

void InputAggregator::setSnapshotSink(SnapshotSink sink)
{
  sink_ = std::move(sink);
}

bool InputAggregator::updateKeyboard(const InputSnapshot& snapshot)
{
  return updateSource(keyboard_, keyboardSequence_, snapshot);
}

bool InputAggregator::updateControllers(const InputSnapshot& snapshot)
{
  return updateSource(controllers_, controllerSequence_, snapshot);
}

void InputAggregator::clear()
{
  keyboard_ = {};
  controllers_ = {};
  keyboardSequence_ = 0U;
  controllerSequence_ = 0U;
  rebuildSnapshot();
}

InputSnapshot InputAggregator::snapshot() const noexcept
{
  return snapshot_;
}

bool InputAggregator::updateSource(
  InputSnapshot& destination,
  std::uint64_t& acceptedSequence,
  const InputSnapshot& source)
{
  if (source.sequence <= acceptedSequence) {
    return false;
  }
  acceptedSequence = source.sequence;
  destination = source;
  const auto before = snapshot_;
  rebuildSnapshot();
  return !sameLogicalState(before, snapshot_);
}

void InputAggregator::rebuildSnapshot()
{
  InputSnapshot merged;
  for (std::size_t player = 0U; player < merged.players.size(); ++player) {
    const auto& keyboard = keyboard_.players[player];
    const auto& controller = controllers_.players[player];
    auto& output = merged.players[player];
    output.connected = keyboard.connected || controller.connected;
    output.buttons = normalizeOppositeDirections(
      static_cast<InputButtonSet>(keyboard.buttons | controller.buttons));
    output.analogX = controller.analogX != 0 ? controller.analogX : keyboard.analogX;
    output.analogY = controller.analogY != 0 ? controller.analogY : keyboard.analogY;
  }

  if (sameLogicalState(merged, snapshot_)) {
    return;
  }
  merged.sequence = snapshot_.sequence + 1U;
  snapshot_ = merged;
  if (sink_) {
    sink_(snapshot_);
  }
}

} // namespace genplusgx::input
