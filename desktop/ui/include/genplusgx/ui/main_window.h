#pragma once

#include "genplusgx/core_adapter.h"
#include "genplusgx/cheats/cheat_manager.h"
#include "genplusgx/diagnostics/diagnostics.h"
#include "genplusgx/input/input_profile.h"
#include "genplusgx/library/game_metadata.h"
#include "genplusgx/library/game_library_database.h"
#include "genplusgx/library/game_library_scanner.h"
#include "genplusgx/core_system_settings.h"
#include "genplusgx/platform/bios_manager.h"
#include "genplusgx/settings/appearance_settings.h"
#include "genplusgx/settings/audio_settings.h"
#include "genplusgx/settings/per_game_settings.h"
#include "genplusgx/settings/screenshot_settings.h"
#include "genplusgx/settings/video_settings.h"
#include "genplusgx/ui/dialog_service.h"
#include "genplusgx/ui/input_configuration_dialog.h"
#include "genplusgx/ui/game_library_dialog.h"
#include "genplusgx/ui/settings_dialog.h"

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
class QEvent;
class QLabel;
class QMenu;
class QObject;

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

enum class DiscUiOperation {
  change,
  setEjected,
};

enum class EmulationUiOperation {
  pause,
  resume,
  hardReset,
  softReset,
  frameAdvance,
  setFastForward,
};

class MainWindow final : public QMainWindow {
public:
  using InputConfigurationSink =
    std::function<PersistenceStatus(const input::InputConfiguration&)>;
  using ControllerAssignmentSink =
    std::function<PersistenceStatus(std::uint32_t, std::size_t)>;
  using GameLoadSink = std::function<void(const std::filesystem::path&)>;
  using GameCloseSink = std::function<void()>;
  using ClearRecentGamesSink = std::function<PersistenceStatus()>;
  using StateOperationSink =
    std::function<void(StateUiOperation, std::uint32_t)>;
  using EmulationControlSink =
    std::function<bool(EmulationUiOperation, bool)>;
  using VideoSettingsSink =
    std::function<PersistenceStatus(const settings::VideoSettings&)>;
  using AudioSettingsSink =
    std::function<void(const settings::AudioSettings&)>;
  using SystemSettingsSink =
    std::function<PersistenceStatus(const CoreSystemSettings&)>;
  using BiosConfigurationSink = std::function<PersistenceStatus(
    const platform::BiosConfiguration&)>;
  using DiscOperationSink = std::function<void(
    DiscUiOperation, const std::filesystem::path&, bool)>;
  using GameInformationRequestSink =
    std::function<void(const std::filesystem::path&)>;
  using ScreenshotSink = std::function<void(
    CoreVideoFrameInfo,
    std::vector<std::uint16_t>)>;
  using ScreenshotSettingsSink = std::function<PersistenceStatus(
    const settings::ScreenshotSettings&)>;
  using CheatConfigurationSink = std::function<PersistenceStatus(
    const cheats::CheatConfiguration&)>;
  using PerGameSettingsSink = std::function<PersistenceStatus(
    const settings::PerGameSettings&)>;
  using AppearanceSettingsSink = std::function<PersistenceStatus(
    const settings::AppearanceSettings&)>;
  using DiagnosticsSnapshotProvider =
    std::function<diagnostics::DiagnosticsSnapshot()>;

  explicit MainWindow(QWidget* parent = nullptr);

  void showAboutDialog();
  void showUserGuide();
  void showKeyboardShortcuts();
  void showInputConfiguration(
    InputConfigurationTab tab = InputConfigurationTab::bindings);
  void setInputConfiguration(input::InputConfiguration configuration);
  void setConnectedControllers(std::vector<input::ControllerInfo> controllers);
  void setInputConfigurationSink(InputConfigurationSink sink);
  void setControllerAssignmentSink(ControllerAssignmentSink sink);
  void setDialogService(std::shared_ptr<DialogService> service);
  void setAppearanceSettings(settings::AppearanceSettings settings);
  void setAppearanceSettingsSink(AppearanceSettingsSink sink);
  [[nodiscard]] const settings::AppearanceSettings&
    appearanceSettings() const noexcept;
  void showAppearanceSettings();
  void setApplicationPaths(ApplicationPaths paths);
  void showSettings(SettingsPage page = SettingsPage::general);
  void setDiagnosticsSnapshotProvider(DiagnosticsSnapshotProvider provider);
  void showDiagnostics();
  void setGameLoadSink(GameLoadSink sink);
  void setGameCloseSink(GameCloseSink sink);
  void setClearRecentGamesSink(ClearRecentGamesSink sink);
  void setStateOperationSink(StateOperationSink sink);
  void setEmulationControlSink(EmulationControlSink sink);
  void setEmulationControlState(bool paused, bool fastForward);
  void setVideoSettings(settings::VideoSettings settings);
  void setVideoSettingsSink(VideoSettingsSink sink);
  [[nodiscard]] const settings::VideoSettings& videoSettings() const noexcept;
  void showVideoSettings();
  void setAudioSettings(settings::AudioSettings settings);
  void setAvailableAudioDevices(std::vector<std::string> devices);
  void setAudioSettingsSink(AudioSettingsSink sink);
  [[nodiscard]] const settings::AudioSettings& audioSettings() const noexcept;
  void showAudioSettings();
  void showAudioSettingsError(const std::string& detail);
  void showAudioOutputError(const std::string& detail);
  void showEmulationRuntimeError(const std::string& detail);
  void showStartupIssues(std::vector<std::string> issues);
  void setSystemSettings(CoreSystemSettings settings);
  void setSystemSettingsSink(SystemSettingsSink sink);
  [[nodiscard]] const CoreSystemSettings& systemSettings() const noexcept;
  void showSystemSettings();
  void setBiosSnapshot(platform::BiosSnapshot snapshot);
  void setBiosConfigurationSink(BiosConfigurationSink sink);
  [[nodiscard]] const platform::BiosSnapshot& biosSnapshot() const noexcept;
  void showBiosSettings();
  void setDiscOperationSink(DiscOperationSink sink);
  void setSegaCdSession(
    bool enabled,
    std::string region,
    std::filesystem::path discPath,
    bool ejected,
    bool discPresent);
  void setDiscOperationBusy(bool busy);
  void showDiscOperationSuccess(DiscUiOperation operation);
  void showDiscOperationError(
    DiscUiOperation operation,
    const std::string& detail);
  void setGameInformationRequestSink(GameInformationRequestSink sink);
  void setGameInformationBusy(bool busy);
  void showGameInformation(const library::GameMetadata& metadata);
  void showGameInformationError(const std::string& detail);
  void setGameLibraryActions(GameLibraryActions actions);
  void setGameLibrarySnapshot(
    std::vector<library::LibraryDirectory> directories,
    std::vector<library::LibraryGame> games);
  void setGameLibraryAvailable(bool available, const std::string& detail = {});
  void showGameLibrary();
  void showGameLibraryScanStarted(
    std::int64_t directoryId,
    const std::filesystem::path& path);
  void showGameLibraryScanProgress(
    std::int64_t directoryId,
    const library::GameLibraryScanSummary& summary);
  void showGameLibraryScanCompleted(
    std::int64_t directoryId,
    const library::GameLibraryScanSummary& summary);
  void showGameLibraryScanFailed(
    std::int64_t directoryId,
    const std::string& detail);
  void showGameLibraryError(const std::string& detail);
  void setScreenshotSink(ScreenshotSink sink);
  void setScreenshotBusy(bool busy);
  void showScreenshotSaved(const std::filesystem::path& path);
  void showScreenshotError(const std::string& detail);
  void setScreenshotSettings(
    settings::ScreenshotSettings settings,
    std::filesystem::path defaultDirectory);
  void setScreenshotSettingsSink(ScreenshotSettingsSink sink);
  [[nodiscard]] const settings::ScreenshotSettings&
    screenshotSettings() const noexcept;
  void showScreenshotSettings();
  void setCheatSession(
    cheats::CheatSystem system,
    cheats::CheatConfiguration configuration);
  void clearCheatSession();
  void setCheatConfigurationSink(CheatConfigurationSink sink);
  void showCheats();
  void showCheatError(const std::string& detail);
  void setPerGameSettingsSession(
    settings::PerGameSettings overrides,
    settings::GlobalGameSettings global);
  void clearPerGameSettingsSession();
  void setPerGameSettingsSink(PerGameSettingsSink sink);
  void showPerGameSettings();
  void showPerGameSettingsError(const std::string& detail);
  void setRecentGames(std::vector<std::filesystem::path> paths);
  void showRecentGamesError(const std::string& detail);
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
  void setGameRuntimeIdentity(std::string system, std::string region);
  void setMeasuredFrameRate(double framesPerSecond);
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
  [[nodiscard]] bool presentLatestFrame();
  [[nodiscard]] video::DisplayWidget* displayWidget() const noexcept;

protected:
  [[nodiscard]] bool eventFilter(QObject* watched, QEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

private:
  QAction* addAction(
    QMenu& menu,
    const QString& text,
    const char* objectName,
    const QKeySequence& shortcut = {});
  void buildMenus();
  void applyHotkeyShortcuts();
  void buildStatusBar();
  void createCanvas();
  void setGameActionsEnabled(bool enabled);
  void chooseGame();
  void closeGame();
  void chooseDisc();
  void requestDiscEjected(bool ejected);
  void updateDiscActions();
  void requestGameInformation();
  void updateGameInformationAction();
  void requestStateOperation(StateUiOperation operation);
  bool requestEmulationControl(
    EmulationUiOperation operation,
    bool enabled = false);
  void setFastForwardHeld(bool held);
  void updateEmulationControls();
  void updateStateActions();
  void updateStateSlotPresentation();
  void requestScreenshot();
  void updateScreenshotAction();
  void updateCheatAction();
  void updatePerGameSettingsAction();
  void presentGameLoadError(
    const std::filesystem::path& path,
    const std::string& detail);
  bool applyVideoSettings(
    const settings::VideoSettings& settings,
    bool notifySink);
  void updateVideoActionChecks();
  void applyAudioSettings(
    const settings::AudioSettings& settings,
    bool notifySink);
  void updateAudioActionChecks();
  [[nodiscard]] SettingsOverview settingsOverview() const;
  void refreshSettingsDialog();

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
  EmulationControlSink emulationControlSink_;
  settings::VideoSettings videoSettings_{settings::defaultVideoSettings()};
  VideoSettingsSink videoSettingsSink_;
  settings::AudioSettings audioSettings_{settings::defaultAudioSettings()};
  std::vector<std::string> availableAudioDevices_;
  AudioSettingsSink audioSettingsSink_;
  CoreSystemSettings systemSettings_;
  SystemSettingsSink systemSettingsSink_;
  platform::BiosSnapshot biosSnapshot_;
  BiosConfigurationSink biosConfigurationSink_;
  DiscOperationSink discOperationSink_;
  GameInformationRequestSink gameInformationRequestSink_;
  GameLibraryActions gameLibraryActions_;
  ScreenshotSink screenshotSink_;
  ScreenshotSettingsSink screenshotSettingsSink_;
  CheatConfigurationSink cheatConfigurationSink_;
  PerGameSettingsSink perGameSettingsSink_;
  AppearanceSettingsSink appearanceSettingsSink_;
  DiagnosticsSnapshotProvider diagnosticsSnapshotProvider_;
  cheats::CheatConfiguration cheatConfiguration_;
  cheats::CheatSystem cheatSystem_{cheats::CheatSystem::genesis};
  settings::PerGameSettings perGameSettings_;
  settings::AppearanceSettings appearanceSettings_;
  settings::GlobalGameSettings globalGameSettings_;
  settings::ScreenshotSettings screenshotSettings_;
  ApplicationPaths applicationPaths_;
  std::filesystem::path defaultScreenshotDirectory_;
  std::vector<library::LibraryDirectory> gameLibraryDirectories_;
  std::vector<library::LibraryGame> gameLibraryGames_;
  std::array<StateSlotView, 10> stateSlotViews_{};
  std::filesystem::path loadedGamePath_;
  std::filesystem::path pendingGamePath_;
  bool hasRecentGames_{false};
  bool gameLoading_{false};
  bool stateSessionReady_{false};
  bool stateOperationBusy_{false};
  bool emulationPaused_{false};
  bool fastForwardActive_{false};
  bool fastForwardHeld_{false};
  bool fastForwardToggled_{false};
  bool segaCdSession_{false};
  bool discEjected_{false};
  bool discPresent_{false};
  bool discOperationBusy_{false};
  bool gameInformationBusy_{false};
  bool gameLibraryAvailable_{true};
  bool screenshotBusy_{false};
  bool cheatSessionReady_{false};
  bool perGameSettingsSessionReady_{false};
  bool applicationPathsAvailable_{false};
  std::string gameLibraryUnavailableDetail_;
  std::string discRegion_;
  std::filesystem::path currentDiscPath_;
  std::uint32_t selectedStateSlot_{0};
};

} // namespace genplusgx::ui
