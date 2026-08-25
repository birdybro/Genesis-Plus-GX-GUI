#include "genplusgx/ui/binding_capture_button.h"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QKeySequence>

namespace genplusgx::ui {
namespace {

QString keyboardBindingName(int key)
{
  const auto name = QKeySequence{key}.toString(QKeySequence::NativeText);
  return name.isEmpty() ? QObject::tr("Key %1").arg(key) : name;
}

QString controllerBindingName(SDL_GamepadButton button)
{
  const char* name = SDL_GetGamepadStringForButton(button);
  return name == nullptr
    ? QObject::tr("Button %1").arg(static_cast<int>(button))
    : QString::fromUtf8(name);
}

} // namespace

BindingCaptureButton::BindingCaptureButton(
  BindingCaptureKind kind,
  InputButton input,
  QWidget* parent)
  : QPushButton(parent), kind_(kind), input_(input)
{
  setMinimumWidth(130);
  setAccessibleDescription(tr("Activate, then press the desired input."));
  connect(this, &QPushButton::clicked, this, &BindingCaptureButton::beginCapture);
  updateLabel();
}

void BindingCaptureButton::setKeyboardBinding(int key)
{
  kind_ = BindingCaptureKind::keyboard;
  bindingCode_ = key;
  capturing_ = false;
  updateLabel();
}

void BindingCaptureButton::setControllerBinding(SDL_GamepadButton button)
{
  kind_ = BindingCaptureKind::controller;
  bindingCode_ = static_cast<int>(button);
  capturing_ = false;
  updateLabel();
}

int BindingCaptureButton::bindingCode() const noexcept
{
  return bindingCode_;
}

InputButton BindingCaptureButton::input() const noexcept
{
  return input_;
}

BindingCaptureKind BindingCaptureButton::kind() const noexcept
{
  return kind_;
}

bool BindingCaptureButton::isCapturing() const noexcept
{
  return capturing_;
}

void BindingCaptureButton::beginCapture()
{
  capturing_ = true;
  setText(tr("Press an input…"));
  setAccessibleName(tr("Waiting for input"));
  setFocus(Qt::OtherFocusReason);
}

void BindingCaptureButton::cancelCapture()
{
  if (!capturing_) {
    return;
  }
  capturing_ = false;
  updateLabel();
}

bool BindingCaptureButton::captureControllerButton(SDL_GamepadButton button)
{
  if (!capturing_ || kind_ != BindingCaptureKind::controller || button < 0 ||
      button >= SDL_GAMEPAD_BUTTON_COUNT) {
    return false;
  }
  bindingCode_ = static_cast<int>(button);
  capturing_ = false;
  updateLabel();
  emit controllerCaptured(bindingCode_);
  return true;
}

void BindingCaptureButton::keyPressEvent(QKeyEvent* event)
{
  if (!capturing_) {
    QPushButton::keyPressEvent(event);
    return;
  }
  if (event->isAutoRepeat()) {
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Escape) {
    cancelCapture();
    event->accept();
    return;
  }
  if (kind_ != BindingCaptureKind::keyboard) {
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_unknown) {
    event->ignore();
    return;
  }
  bindingCode_ = event->key();
  capturing_ = false;
  updateLabel();
  emit keyboardCaptured(bindingCode_);
  event->accept();
}

void BindingCaptureButton::focusOutEvent(QFocusEvent* event)
{
  cancelCapture();
  QPushButton::focusOutEvent(event);
}

void BindingCaptureButton::updateLabel()
{
  const auto label = kind_ == BindingCaptureKind::keyboard
    ? keyboardBindingName(bindingCode_)
    : controllerBindingName(static_cast<SDL_GamepadButton>(bindingCode_));
  setText(label);
  setAccessibleName(tr("%1 binding: %2")
      .arg(QString::fromLatin1(kind_ == BindingCaptureKind::keyboard
            ? "Keyboard" : "Controller"), label));
}

} // namespace genplusgx::ui
