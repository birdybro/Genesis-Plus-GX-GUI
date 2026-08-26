#pragma once

#include "genplusgx/input_snapshot.h"

#include <SDL3/SDL_gamepad.h>

#include <QPushButton>

namespace genplusgx::ui {

enum class BindingCaptureKind {
  keyboard,
  controller,
  hotkey,
};

class BindingCaptureButton final : public QPushButton {
  Q_OBJECT

public:
  BindingCaptureButton(
    BindingCaptureKind kind,
    InputButton input,
    QWidget* parent = nullptr);

  void setKeyboardBinding(int key);
  void setControllerBinding(SDL_GamepadButton button);
  void setHotkeyBinding(int keyCombination);
  [[nodiscard]] int bindingCode() const noexcept;
  [[nodiscard]] InputButton input() const noexcept;
  [[nodiscard]] BindingCaptureKind kind() const noexcept;
  [[nodiscard]] bool isCapturing() const noexcept;

  void beginCapture();
  void cancelCapture();
  [[nodiscard]] bool captureControllerButton(SDL_GamepadButton button);

signals:
  void keyboardCaptured(int key);
  void controllerCaptured(int button);
  void hotkeyCaptured(int keyCombination);

protected:
  void keyPressEvent(QKeyEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;

private:
  void updateLabel();

  BindingCaptureKind kind_;
  InputButton input_;
  int bindingCode_{0};
  bool capturing_{false};
};

} // namespace genplusgx::ui
