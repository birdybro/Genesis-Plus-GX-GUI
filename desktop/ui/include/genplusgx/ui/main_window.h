#pragma once

#include "genplusgx/input/input_profile.h"
#include "genplusgx/core_system_settings.h"
#include "genplusgx/settings/audio_settings.h"
#include "genplusgx/settings/video_settings.h"
#include "genplusgx/ui/dialog_service.h"
#include "genplusgx/ui/input_configuration_dialog.h"

#include <QMainWindow>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <memory>
#include <string>
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

enum class StateSlotViewState {
  empty,
  available,
  invalid,
};

struct StateSlotView final {
  std::uint32_t slot{0};
  StateSlotViewState state{StateSlotViewState::empty};
  std::chrono::system_clock::time_point timestamp{};
  std::uint64_t emulatedFrameNumber{0};
  std::string detail;
};

enum class StateUiOperation {
  save,
  load,
  remove,
};

class MainWindow final : public QMainWindow {
public:
  using InputConfigurationSink =
    std::function<void(const input::InputConfiguration&)>;
  using ControllerAssignmentSink =
    std::function<void(std::uint32_t, std::size_t)>;
  using GameLoadSink = std::function<void(const std::filesystem::path&)>;
  using GameCloseSink = std::function<void()>;
  using ClearRecentGamesSink = std::function<void()>;
  using StateOperationSink =
    std::function<void(StateUiOperation, std::uint32_t)>;
  using VideoSettingsSink =
    std::function<void(const settings::VideoSettings&)>;
  using AudioSettingsSink =
    std::function<void(const settings::AudioSettings&)>;
  using SystemSettingsSink = std::function<void(const CoreSystemSettings&)>;

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
  void setClearRecentGamesSink(ClearRecentGamesSink sink);
  void setStateOperationSink(StateOperationSink sink);
  void setVideoSettings(settings::VideoSettings settings);
  void setVideoSettingsSink(VideoSettingsSink sink);
  [[nodiscard]] const settings::VideoSettings& videoSettings() const noexcept;
  void showVideoSettings();
  void setAudioSettings(settings::AudioSettings settings);
  void setAvailableAudioDevices(std::vector<std::string> devices);
  void setAudioSettingsSink(AudioSettingsSink sink);
  [[nodiscard]] const settings::AudioSettings& audioSettings() const noexcept;
  void showAudioSettings();
  void setSystemSettings(CoreSystemSettings settings);
  void setSystemSettingsSink(SystemSettingsSink sink);
  [[nodiscard]] const CoreSystemSettings& systemSettings() const noexcept;
  void showSystemSettings();
  void setRecentGames(std::vector<std::filesystem::path> paths);
  void setStateSessionReady(bool ready);
  void setStateOperationBusy(bool busy);
  void setStateSlotViews(std::array<StateSlotView, 10> views);
  void setSelectedStateSlot(std::uint32_t slot);
  [[nodiscard]] std::uint32_t selectedStateSlot() const noexcept;
  void showStateOperationSuccess(StateUiOperation operation, std::uint32_t slot);
  void showStateOperationError(StateUiOperation operation, const std::string& detail);
  [[nodiscard]] bool requestGameLoad(const std::filesystem::path& path);
  void setGameLoading(const std::filesystem::path& path);
  void setGameLoaded(const std::filesystem::path& path);
  void setNoGameLoaded();
  void showGameLoadError(
    const std::filesystem::path& path,
    const std::string& detail,
    bool gameWasUnloaded = true);
  void showGameCloseError(const std::string& detail);
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
  void requestStateOperation(StateUiOperation operation);
  void updateStateActions();
  void updateStateSlotPresentation();
  void presentGameLoadError(
    const std::filesystem::path& path,
    const std::string& detail);
  void applyVideoSettings(
    const settings::VideoSettings& settings,
    bool notifySink);
  void updateVideoActionChecks();
  void applyAudioSettings(
    const settings::AudioSettings& settings,
    bool notifySink);
  void updateAudioActionChecks();

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
  ClearRecentGamesSink clearRecentGamesSink_;
  StateOperationSink stateOperationSink_;
  settings::VideoSettings videoSettings_{settings::defaultVideoSettings()};
  VideoSettingsSink videoSettingsSink_;
  settings::AudioSettings audioSettings_{settings::defaultAudioSettings()};
  std::vector<std::string> availableAudioDevices_;
  AudioSettingsSink audioSettingsSink_;
  CoreSystemSettings systemSettings_;
  SystemSettingsSink systemSettingsSink_;
  std::array<StateSlotView, 10> stateSlotViews_{};
  std::filesystem::path loadedGamePath_;
  std::filesystem::path pendingGamePath_;
  bool hasRecentGames_{false};
  bool gameLoading_{false};
  bool stateSessionReady_{false};
  bool stateOperationBusy_{false};
  std::uint32_t selectedStateSlot_{0};
};

} // namespace genplusgx::ui
