#pragma once

#include "genplusgx/input_snapshot.h"

#include <QObject>
#include <QPointer>

#include <functional>
#include <optional>
#include <vector>

namespace genplusgx::input {

struct KeyboardBinding final {
  int key{0};
  InputButton button{InputButton::a};

  friend bool operator==(const KeyboardBinding&, const KeyboardBinding&) = default;
};

[[nodiscard]] std::vector<KeyboardBinding> defaultGenesisKeyboardBindings();

class KeyboardInput final : public QObject {
public:
  using SnapshotSink = std::function<void(const InputSnapshot&)>;

  explicit KeyboardInput(QObject* parent = nullptr);
  ~KeyboardInput() override;

  KeyboardInput(const KeyboardInput&) = delete;
  KeyboardInput& operator=(const KeyboardInput&) = delete;

  void attach(QObject& target);
  void detach();
  void setSnapshotSink(SnapshotSink sink);

  [[nodiscard]] bool pressKey(int key, bool autoRepeat = false);
  [[nodiscard]] bool releaseKey(int key, bool autoRepeat = false);
  void releaseAll();

  [[nodiscard]] InputSnapshot snapshot() const noexcept;
  [[nodiscard]] std::optional<int> keyForButton(InputButton button) const noexcept;
  [[nodiscard]] bool isKeyBound(int key) const noexcept;

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  [[nodiscard]] const KeyboardBinding* bindingForKey(int key) const noexcept;
  void publishChangedSnapshot();
  void rebuildButtons() noexcept;

  std::vector<KeyboardBinding> bindings_;
  std::vector<int> pressedKeys_;
  QPointer<QObject> target_;
  SnapshotSink sink_;
  InputSnapshot snapshot_;
  int lastHorizontalKey_{0};
  int lastVerticalKey_{0};
};

} // namespace genplusgx::input
