#pragma once

#include "genplusgx/input/input_profile.h"
#include "genplusgx/ui/dialog_service.h"
#include "genplusgx/ui/input_configuration_dialog.h"

#include <QMainWindow>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <memory>
#include <vector>

class QAction;
class QDragEnterEvent;
class QDropEvent;
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
  using GameLoadSink = std::function<void(const std::filesystem::path&)>;
  using GameCloseSink = std::function<void()>;

  explicit MainWindow(QWidget* parent = nullptr);

  void showAboutDialog();
  void showInputConfiguration(
    InputConfigurationTab tab = InputConfigurationTab::bindings);
  void setInputConfiguration(input::InputConfiguration configuration);
  void setConnectedControllers(std::vector<input::ControllerInfo> controllers);
  void setInputConfigurationSink(InputConfigurationSink sink);
  void setControllerAssignmentSink(ControllerAssignmentSink sink);
  void setDialogService(std::shared_ptr<DialogService> service);
  void setGameLoadSink(GameLoadSink sink);
  void setGameCloseSink(GameCloseSink sink);
  [[nodiscard]] bool requestGameLoad(const std::filesystem::path& path);
  void setGameLoading(const std::filesystem::path& path);
  void setGameLoaded(const std::filesystem::path& path);
  void setNoGameLoaded();
  void showGameLoadError(
    const std::filesystem::path& path,
    const std::string& detail,
    bool gameWasUnloaded = true);
  void setFullscreen(bool enabled);
  [[nodiscard]] bool isGameLoaded() const noexcept;
  [[nodiscard]] bool isGameLoading() const noexcept;
  [[nodiscard]] const std::filesystem::path& loadedGamePath() const noexcept;
  [[nodiscard]] bool captureControllerButton(SDL_GamepadButton button);
  [[nodiscard]] video::DisplayWidget* displayWidget() const noexcept;

protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

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
  void chooseGame();
  void closeGame();
  void presentGameLoadError(
    const std::filesystem::path& path,
    const std::string& detail);

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
  std::shared_ptr<DialogService> dialogService_;
  GameLoadSink gameLoadSink_;
  GameCloseSink gameCloseSink_;
  std::filesystem::path loadedGamePath_;
  std::filesystem::path pendingGamePath_;
  bool gameLoading_{false};
};

} // namespace genplusgx::ui
