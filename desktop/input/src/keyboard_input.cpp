#include "genplusgx/input/keyboard_input.h"

#include <QEvent>
#include <QKeyEvent>

#include <algorithm>
#include <array>
#include <utility>

namespace genplusgx::input {
namespace {

constexpr std::array defaultBindings{
  KeyboardBinding{Qt::Key_Up, InputButton::up},
  KeyboardBinding{Qt::Key_Down, InputButton::down},
  KeyboardBinding{Qt::Key_Left, InputButton::left},
  KeyboardBinding{Qt::Key_Right, InputButton::right},
  KeyboardBinding{Qt::Key_Z, InputButton::a},
  KeyboardBinding{Qt::Key_X, InputButton::b},
  KeyboardBinding{Qt::Key_C, InputButton::c},
  KeyboardBinding{Qt::Key_Return, InputButton::start},
  KeyboardBinding{Qt::Key_A, InputButton::x},
  KeyboardBinding{Qt::Key_S, InputButton::y},
  KeyboardBinding{Qt::Key_D, InputButton::z},
  KeyboardBinding{Qt::Key_Shift, InputButton::mode},
};

bool isHorizontal(InputButton button) noexcept
{
  return button == InputButton::left || button == InputButton::right;
}

bool isVertical(InputButton button) noexcept
{
  return button == InputButton::up || button == InputButton::down;
}

} // namespace

std::vector<KeyboardBinding> defaultGenesisKeyboardBindings()
{
  return {defaultBindings.begin(), defaultBindings.end()};
}

KeyboardInput::KeyboardInput(QObject* parent)
  : QObject(parent), bindings_(defaultGenesisKeyboardBindings())
{
  pressedKeys_.reserve(bindings_.size());
  snapshot_.sequence = 1U;
  snapshot_.players[0].connected = true;
}

KeyboardInput::~KeyboardInput()
{
  sink_ = {};
  detach();
}

void KeyboardInput::attach(QObject& target)
{
  if (target_ == &target) {
    return;
  }
  detach();
  target_ = &target;
  target.installEventFilter(this);
}

void KeyboardInput::detach()
{
  if (target_) {
    target_->removeEventFilter(this);
    target_.clear();
  }
  releaseAll();
}

void KeyboardInput::setSnapshotSink(SnapshotSink sink)
{
  sink_ = std::move(sink);
}

bool KeyboardInput::pressKey(int key, bool autoRepeat)
{
  const auto* binding = bindingForKey(key);
  if (binding == nullptr) {
    return false;
  }
  if (autoRepeat ||
      std::find(pressedKeys_.begin(), pressedKeys_.end(), key) != pressedKeys_.end()) {
    return true;
  }
  pressedKeys_.push_back(key);
  if (isHorizontal(binding->button)) {
    lastHorizontalKey_ = key;
  }
  if (isVertical(binding->button)) {
    lastVerticalKey_ = key;
  }
  publishChangedSnapshot();
  return true;
}

bool KeyboardInput::releaseKey(int key, bool autoRepeat)
{
  if (!isKeyBound(key)) {
    return false;
  }
  if (autoRepeat) {
    return true;
  }
  const auto found = std::find(pressedKeys_.begin(), pressedKeys_.end(), key);
  if (found == pressedKeys_.end()) {
    return true;
  }
  pressedKeys_.erase(found);
  if (lastHorizontalKey_ == key) {
    lastHorizontalKey_ = 0;
    for (auto iterator = pressedKeys_.rbegin(); iterator != pressedKeys_.rend(); ++iterator) {
      const auto* binding = bindingForKey(*iterator);
      if (binding != nullptr && isHorizontal(binding->button)) {
        lastHorizontalKey_ = *iterator;
        break;
      }
    }
  }
  if (lastVerticalKey_ == key) {
    lastVerticalKey_ = 0;
    for (auto iterator = pressedKeys_.rbegin(); iterator != pressedKeys_.rend(); ++iterator) {
      const auto* binding = bindingForKey(*iterator);
      if (binding != nullptr && isVertical(binding->button)) {
        lastVerticalKey_ = *iterator;
        break;
      }
    }
  }
  publishChangedSnapshot();
  return true;
}

void KeyboardInput::releaseAll()
{
  if (pressedKeys_.empty()) {
    return;
  }
  pressedKeys_.clear();
  lastHorizontalKey_ = 0;
  lastVerticalKey_ = 0;
  publishChangedSnapshot();
}

InputSnapshot KeyboardInput::snapshot() const noexcept
{
  return snapshot_;
}

std::optional<int> KeyboardInput::keyForButton(InputButton button) const noexcept
{
  const auto found = std::find_if(bindings_.begin(), bindings_.end(),
    [button](const KeyboardBinding& binding) { return binding.button == button; });
  if (found == bindings_.end()) {
    return std::nullopt;
  }
  return found->key;
}

bool KeyboardInput::isKeyBound(int key) const noexcept
{
  return bindingForKey(key) != nullptr;
}

bool KeyboardInput::eventFilter(QObject* watched, QEvent* event)
{
  if (watched != target_) {
    return QObject::eventFilter(watched, event);
  }
  if (event->type() == QEvent::FocusOut ||
      event->type() == QEvent::WindowDeactivate ||
      event->type() == QEvent::Hide) {
    releaseAll();
    return QObject::eventFilter(watched, event);
  }
  if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease) {
    return QObject::eventFilter(watched, event);
  }

  const auto* keyEvent = static_cast<QKeyEvent*>(event);
  const bool pressed = event->type() == QEvent::KeyPress;
  if (pressed &&
      keyEvent->modifiers().testAnyFlags(
        Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
    return QObject::eventFilter(watched, event);
  }
  const bool handled = pressed
    ? pressKey(keyEvent->key(), keyEvent->isAutoRepeat())
    : releaseKey(keyEvent->key(), keyEvent->isAutoRepeat());
  if (handled) {
    event->accept();
  }
  return handled;
}

const KeyboardBinding* KeyboardInput::bindingForKey(int key) const noexcept
{
  const auto found = std::find_if(bindings_.begin(), bindings_.end(),
    [key](const KeyboardBinding& binding) { return binding.key == key; });
  return found == bindings_.end() ? nullptr : &*found;
}

void KeyboardInput::publishChangedSnapshot()
{
  rebuildButtons();
  ++snapshot_.sequence;
  if (sink_) {
    sink_(snapshot_);
  }
}

void KeyboardInput::rebuildButtons() noexcept
{
  InputButtonSet buttons = 0U;
  for (const auto key : pressedKeys_) {
    const auto* binding = bindingForKey(key);
    if (binding == nullptr) {
      continue;
    }
    if (isHorizontal(binding->button) && key != lastHorizontalKey_) {
      continue;
    }
    if (isVertical(binding->button) && key != lastVerticalKey_) {
      continue;
    }
    buttons = static_cast<InputButtonSet>(buttons | buttonMask(binding->button));
  }
  snapshot_.players[0].buttons = buttons;
}

} // namespace genplusgx::input
