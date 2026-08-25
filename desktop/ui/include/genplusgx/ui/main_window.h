#pragma once

#include "genplusgx/input/input_profile.h"
#include "genplusgx/ui/input_configuration_dialog.h"

#include <QMainWindow>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class QAction;
class QLabel;
class QMenu;

namespace genplusgx::video {
class DisplayWidget;
}

namespace genplusgx::ui {

class MainWindow final : public QMainWindow {
public:
  using InputConfigurationSink =
    std::function<void(const input::InputConfiguration&)>;
  using ControllerAssignmentSink =
    std::function<void(std::uint32_t, std::size_t)>;

  explicit MainWindow(QWidget* parent = nullptr);

  void showAboutDialog();
  void showInputConfiguration(
    InputConfigurationTab tab = InputConfigurationTab::bindings);
  void setInputConfiguration(input::InputConfiguration configuration);
  void setConnectedControllers(std::vector<input::ControllerInfo> controllers);
  void setInputConfigurationSink(InputConfigurationSink sink);
  void setControllerAssignmentSink(ControllerAssignmentSink sink);
  [[nodiscard]] bool captureControllerButton(SDL_GamepadButton button);
  [[nodiscard]] video::DisplayWidget* displayWidget() const noexcept;

private:
  QAction* addAction(
    QMenu& menu,
    const QString& text,
    const char* objectName,
    const QKeySequence& shortcut = {});
  void buildMenus();
  void buildStatusBar();
  void createCanvas();
  void setGameActionsEnabled(bool enabled);

  QLabel* gameStatus_{nullptr};
  QLabel* systemStatus_{nullptr};
  QLabel* regionStatus_{nullptr};
  QLabel* fpsStatus_{nullptr};
  QLabel* slotStatus_{nullptr};
  video::DisplayWidget* displayWidget_{nullptr};
  input::InputConfiguration inputConfiguration_{input::defaultInputConfiguration()};
  std::vector<input::ControllerInfo> controllers_;
  InputConfigurationSink inputConfigurationSink_;
  ControllerAssignmentSink controllerAssignmentSink_;
};

} // namespace genplusgx::ui
