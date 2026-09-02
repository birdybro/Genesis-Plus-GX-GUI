#pragma once

#include "genplusgx/core_adapter.h"
#include "genplusgx/achievements/achievement_settings.h"
#include "genplusgx/achievements/achievement_types.h"
#include "genplusgx/game_file.h"
#include "genplusgx/cheats/cheat_manager.h"
#include "genplusgx/cloud/cloud_settings.h"
#include "genplusgx/cloud/cloud_types.h"
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
#include "genplusgx/settings/session_settings.h"
#include "genplusgx/settings/speed_settings.h"
#include "genplusgx/settings/video_settings.h"
#include "genplusgx/ui/dialog_service.h"
#include "genplusgx/ui/input_configuration_dialog.h"
#include "genplusgx/ui/netplay_dialog.h"
#include "genplusgx/ui/game_library_dialog.h"
#include "genplusgx/ui/physical_media_dialog.h"
#include "genplusgx/ui/settings_dialog.h"
#include "genplusgx/ui/state_manager_dialog.h"
#include "genplusgx/settings/rewind_settings.h"
#include "genplusgx/settings/run_ahead_settings.h"

#include <QMainWindow>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <memory>
#include <optional>
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

enum class DiscUiOperation {
  change,
  setEjected,
};

enum class RecordingUiState {
  unavailable,
  idle,
  starting,
  recording,
  stopping,
};

enum class EmulationUiOperation {
  pause,
  resume,
  hardReset,
  softReset,
  frameAdvance,
  setFastForward,
  setSlowMotion,
  setRewinding,
};

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  using InputConfigurationSink =
    std::function<PersistenceStatus(const input::InputConfiguration&)>;
  using ControllerAssignmentSink =
    std::function<PersistenceStatus(std::uint32_t, std::size_t)>;
  using GameLoadSink = std::function<void(const GameLaunchTarget&)>;
  using GameCloseSink = std::function<void()>;
  using ClearRecentGamesSink = std::function<PersistenceStatus()>;
  using StateOperationSink =
    std::function<void(StateUiRequest)>;
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
  using RecordingSink = std::function<bool(
    bool start, const std::filesystem::path& directory)>;
  using CheatConfigurationSink = std::function<PersistenceStatus(
    const cheats::CheatConfiguration&)>;
  using PerGameSettingsSink = std::function<PersistenceStatus(
    const settings::PerGameSettings&)>;
  using AppearanceSettingsSink = std::function<PersistenceStatus(
    const settings::AppearanceSettings&)>;
  using RewindSettingsSink = std::function<PersistenceStatus(
    const RewindConfiguration&)>;
  using RunAheadSettingsSink = std::function<PersistenceStatus(
    const RunAheadConfiguration&)>;
  using SessionSettingsSink = std::function<PersistenceStatus(bool)>;
  using SpeedSettingsSink = std::function<PersistenceStatus(
    const EmulationSpeedConfiguration&)>;
  using DiagnosticsSnapshotProvider =
    std::function<diagnostics::DiagnosticsSnapshot()>;
  using DebugRequestSink = std::function<bool(CoreDebugRequest)>;
  using NetplayRequestSink = NetplayDialog::RequestSink;
  using AchievementSettingsSink = std::function<PersistenceStatus(
    const achievements::Settings&)>;
  using AchievementLoginSink =
    std::function<void(std::string, std::string)>;
  using AchievementLogoutSink = std::function<void()>;
  using CloudSettingsSink = std::function<PersistenceStatus(
    const cloud::Settings&)>;
  using CloudPasswordSink = std::function<void(
    std::string, std::string, std::string)>;
  using CloudAccountSink = std::function<void(std::string, std::string)>;
  using CloudSyncSink = std::function<void(std::string)>;

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
  void setDebugRequestSink(DebugRequestSink sink);
  void showDebugTools();
  void setNetplayRequestSink(NetplayRequestSink sink);
  void setNetplaySessionState(
    netplay::NetplaySessionState state,
    const std::string& detail = {});
  void showNetplay();
  void showNetplayError(const std::string& detail);
  void setAchievementSettings(achievements::Settings settings);
  void setAchievementSettingsSink(AchievementSettingsSink sink);
  void setAchievementLoginSink(AchievementLoginSink sink);
  void setAchievementLogoutSink(AchievementLogoutSink sink);
  void setAchievementSnapshot(achievements::Snapshot snapshot);
  void presentAchievementEvent(const achievements::Event& event);
  void showAchievementError(const std::string& detail);
  void showAchievements();
  void setCloudSettings(cloud::Settings settings);
  void setCloudSettingsSink(CloudSettingsSink sink);
  void setCloudPasswordSink(CloudPasswordSink sink);
  void setCloudForgetSink(CloudAccountSink sink);
  void setCloudSyncSink(CloudSyncSink sink);
  void setCloudSyncBusy(bool busy);
  void showCloudSyncResult(const cloud::SyncResult& result);
  void showCloudSyncStatus(const std::string& detail);
  void showCloudSyncError(const std::string& detail);
  void showCloudSync();
  void presentDebugResponse(CoreDebugResponse response);
  void showDebugRequestError(
    const std::string& detail, std::uint64_t clientToken);
  void setGameLoadSink(GameLoadSink sink);
  void setPhysicalMediaSupported(bool supported);
  void setPhysicalMediaActions(PhysicalMediaDialogActions actions);
  void showPhysicalMedia();
  void setPhysicalMediaDrives(std::vector<platform::PhysicalDrive> drives);
  void setPhysicalMediaImportStarted();
  void setPhysicalMediaImportProgress(
    std::uint32_t completedSectors, std::uint32_t totalSectors);
  void showPhysicalMediaError(const std::string& detail);
  void showPhysicalMediaCancelled();
  [[nodiscard]] bool requestPhysicalMediaLoad(
    const platform::PhysicalMediaSnapshot& snapshot);
  void setArchiveCacheDirectory(std::filesystem::path directory);
  void setPatchCacheDirectory(std::filesystem::path directory);
  void setGameCloseSink(GameCloseSink sink);
  void setClearRecentGamesSink(ClearRecentGamesSink sink);
  void setStateOperationSink(StateOperationSink sink);
  void setEmulationControlSink(EmulationControlSink sink);
  void setEmulationControlState(
    bool paused,
    bool fastForward,
    bool slowMotion,
    std::uint32_t speedPercent,
    bool rewinding,
    bool rewindAvailable);
  void setRewindSettings(RewindConfiguration settings);
  void setRewindSettingsSink(RewindSettingsSink sink);
  [[nodiscard]] const RewindConfiguration& rewindSettings() const noexcept;
  void showRewindSettings();
  void setRunAheadSettings(RunAheadConfiguration settings);
  void setRunAheadSettingsSink(RunAheadSettingsSink sink);
  [[nodiscard]] const RunAheadConfiguration&
    runAheadSettings() const noexcept;
  void setRunAheadRuntimeState(
    bool supported,
    bool active,
    bool verified);
  void showRunAheadSettings();
  void setSessionSettings(settings::SessionSettings settings);
  void setSessionSettingsSink(SessionSettingsSink sink);
  [[nodiscard]] const settings::SessionSettings& sessionSettings() const noexcept;
  void showSessionSettings();
  void setSpeedSettings(EmulationSpeedConfiguration settings);
  void setSpeedSettingsSink(SpeedSettingsSink sink);
  [[nodiscard]] const EmulationSpeedConfiguration&
    speedSettings() const noexcept;
  void showSpeedSettings();
  void setVideoSettings(settings::VideoSettings settings);
  void setVideoSettingsSink(VideoSettingsSink sink);
  [[nodiscard]] const settings::VideoSettings& videoSettings() const noexcept;
  void showVideoSettings();
  void chooseVideoArtwork(
    video::ArtworkMode requestedMode,
    bool forceChooser = false);
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
  void setRecordingSink(RecordingSink sink);
  void setRecordingState(
    RecordingUiState state,
    std::filesystem::path path = {},
    std::uint64_t writtenFrames = 0U,
    std::uint64_t droppedFrames = 0U);
  void showRecordingError(const std::string& detail);
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
  void setSessionResumeBusy(bool busy);
  void setStateOperationBusy(bool busy);
  void setStateSlotViews(std::array<StateSlotView, 10> views);
  void showStateManager();
  void setSelectedStateSlot(std::uint32_t slot);
  [[nodiscard]] std::uint32_t selectedStateSlot() const noexcept;
  [[nodiscard]] std::vector<std::uint8_t> captureStateThumbnailPng() const;
  void showStateOperationSuccess(StateUiOperation operation, std::uint32_t slot);
  void showStateOperationError(StateUiOperation operation, const std::string& detail);
  [[nodiscard]] bool requestGameLoad(
    const std::filesystem::path& path,
    std::optional<std::filesystem::path> patchPath = std::nullopt);
  void setGameLoading(const std::filesystem::path& path);
  void setGameLoaded(const std::filesystem::path& path);
  void setGameLoaded(const GameLaunchTarget& target);
  void setGameRuntimeIdentity(std::string system, std::string region);
  void setMeasuredFrameRate(double framesPerSecond);
  void setNominalVideoRate(double framesPerSecond);
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
  [[nodiscard]] const std::filesystem::path& loadedRuntimePath() const noexcept;
  [[nodiscard]] const GameLaunchTarget& loadedGameTarget() const noexcept;
  [[nodiscard]] bool isSegaCdSession() const noexcept;
  [[nodiscard]] bool isDiscEjected() const noexcept;
  [[nodiscard]] bool isDiscPresent() const noexcept;
  [[nodiscard]] const std::filesystem::path& currentDiscPath() const noexcept;
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
  [[nodiscard]] bool submitGameLoadTarget(GameLaunchTarget target);
  void updatePhysicalMediaAction();
  void updateNetplayControls();
  void updateAchievementControls();
  void updateCloudDialogState();
  void chooseGame();
  void chooseGameWithPatch();
  void closeGame();
  void chooseDisc();
  void requestPlaylistDisc(int direction);
  void chooseShaderPreset();
  void requestDiscEjected(bool ejected);
  void updateDiscActions();
  void requestGameInformation();
  void updateGameInformationAction();
  void requestStateOperation(
    StateUiOperation operation,
    std::filesystem::path path = {},
    std::string name = {});
  bool requestEmulationControl(
    EmulationUiOperation operation,
    bool enabled = false);
  void setFastForwardHeld(bool held);
  void setSlowMotionHeld(bool held);
  void setRewindHeld(bool held);
  bool applySpeedSettings(
    const EmulationSpeedConfiguration& settings,
    bool notifySink);
  bool applyRunAheadSettings(
    const RunAheadConfiguration& settings,
    bool notifySink);
  void updateRunAheadAction();
  void updateSpeedActionChecks();
  void updateEmulationControls();
  void updateStateActions();
  void updateStateSlotPresentation();
  void requestScreenshot();
  void updateScreenshotAction();
  void requestRecordingToggle(bool enabled);
  void updateRecordingAction();
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
  QLabel* speedStatus_{nullptr};
  QLabel* slotStatus_{nullptr};
  video::DisplayWidget* displayWidget_{nullptr};
  input::InputConfiguration inputConfiguration_{input::defaultInputConfiguration()};
  std::vector<input::ControllerInfo> controllers_;
  InputConfigurationSink inputConfigurationSink_;
  ControllerAssignmentSink controllerAssignmentSink_;
  std::shared_ptr<DialogService> dialogService_;
  GameLoadSink gameLoadSink_;
  PhysicalMediaDialogActions physicalMediaActions_;
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
  RecordingSink recordingSink_;
  CheatConfigurationSink cheatConfigurationSink_;
  PerGameSettingsSink perGameSettingsSink_;
  AppearanceSettingsSink appearanceSettingsSink_;
  RewindSettingsSink rewindSettingsSink_;
  RunAheadSettingsSink runAheadSettingsSink_;
  SessionSettingsSink sessionSettingsSink_;
  SpeedSettingsSink speedSettingsSink_;
  DiagnosticsSnapshotProvider diagnosticsSnapshotProvider_;
  DebugRequestSink debugRequestSink_;
  NetplayRequestSink netplayRequestSink_;
  AchievementSettingsSink achievementSettingsSink_;
  AchievementLoginSink achievementLoginSink_;
  AchievementLogoutSink achievementLogoutSink_;
  CloudSettingsSink cloudSettingsSink_;
  CloudPasswordSink cloudPasswordSink_;
  CloudAccountSink cloudForgetSink_;
  CloudSyncSink cloudSyncSink_;
  achievements::Settings achievementSettings_;
  achievements::Snapshot achievementSnapshot_;
  cloud::Settings cloudSettings_;
  bool cloudSyncBusy_{false};
  netplay::NetplaySessionState netplayState_{
    netplay::NetplaySessionState::disconnected};
  cheats::CheatConfiguration cheatConfiguration_;
  cheats::CheatSystem cheatSystem_{cheats::CheatSystem::genesis};
  settings::PerGameSettings perGameSettings_;
  settings::AppearanceSettings appearanceSettings_;
  settings::GlobalGameSettings globalGameSettings_;
  settings::ScreenshotSettings screenshotSettings_;
  RewindConfiguration rewindSettings_;
  RunAheadConfiguration runAheadSettings_;
  settings::SessionSettings sessionSettings_;
  EmulationSpeedConfiguration speedSettings_;
  ApplicationPaths applicationPaths_;
  std::filesystem::path defaultScreenshotDirectory_;
  std::filesystem::path recordingPath_;
  std::vector<library::LibraryDirectory> gameLibraryDirectories_;
  std::vector<library::LibraryGame> gameLibraryGames_;
  std::array<StateSlotView, 10> stateSlotViews_{};
  StateManagerDialog* stateManagerDialog_{nullptr};
  PhysicalMediaDialog* physicalMediaDialog_{nullptr};
  std::filesystem::path loadedGamePath_;
  std::filesystem::path loadedRuntimePath_;
  GameLaunchTarget loadedGameTarget_;
  GameLaunchTarget pendingGameTarget_;
  std::filesystem::path pendingGamePath_;
  std::filesystem::path archiveCacheDirectory_;
  std::filesystem::path patchCacheDirectory_;
  bool hasRecentGames_{false};
  bool gameLoading_{false};
  bool physicalMediaSupported_{false};
  bool stateSessionReady_{false};
  bool stateOperationBusy_{false};
  bool sessionResumeBusy_{false};
  bool emulationPaused_{false};
  bool fastForwardActive_{false};
  bool fastForwardHeld_{false};
  bool fastForwardToggled_{false};
  bool slowMotionActive_{false};
  bool slowMotionHeld_{false};
  bool slowMotionToggled_{false};
  std::uint32_t activeSpeedPercent_{100U};
  bool rewindActive_{false};
  bool rewindAvailable_{false};
  bool rewindHeld_{false};
  bool rewindToggled_{false};
  bool runAheadSupported_{false};
  bool runAheadActive_{false};
  bool runAheadVerified_{false};
  bool segaCdSession_{false};
  bool discEjected_{false};
  bool discPresent_{false};
  bool discOperationBusy_{false};
  bool gameInformationBusy_{false};
  bool gameLibraryAvailable_{true};
  bool screenshotBusy_{false};
  RecordingUiState recordingState_{RecordingUiState::unavailable};
  bool cheatSessionReady_{false};
  bool perGameSettingsSessionReady_{false};
  bool applicationPathsAvailable_{false};
  std::string gameLibraryUnavailableDetail_;
  std::string discRegion_;
  std::filesystem::path currentDiscPath_;
  std::optional<std::size_t> playlistDiscIndex_;
  std::uint32_t selectedStateSlot_{0};
};

} // namespace genplusgx::ui
