#include "genplusgx/ui/about_dialog.h"
#include "genplusgx/ui/audio_settings_dialog.h"
#include "genplusgx/ui/bios_settings_dialog.h"
#include "genplusgx/ui/dialog_service.h"
#include "genplusgx/ui/game_information_dialog.h"
#include "genplusgx/ui/main_window.h"
#include "genplusgx/ui/screenshot_settings_dialog.h"
#include "genplusgx/ui/system_settings_dialog.h"
#include "genplusgx/ui/video_settings_dialog.h"
#include "genplusgx/version.h"
#include "genplusgx/video/display_widget.h"

#include "synthetic_rom.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTest>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

namespace {

class FakeDialogService final : public genplusgx::ui::DialogService {
public:
  std::vector<QString> errors;
  std::optional<std::filesystem::path> discSelection;
  std::optional<std::filesystem::path> directorySelection;
  std::optional<std::filesystem::path> shaderSelection;
  std::filesystem::path discInitialDirectory;
  std::filesystem::path directoryInitialDirectory;
  std::filesystem::path shaderInitialDirectory;
  int chooseDiscCount{0};
  int chooseDirectoryCount{0};
  int chooseShaderCount{0};

  std::optional<std::filesystem::path> chooseGame(
    QWidget*, const std::filesystem::path&) override
  {
    return std::nullopt;
  }

  std::optional<std::filesystem::path> chooseDisc(
    QWidget*, const std::filesystem::path& initialDirectory) override
  {
    ++chooseDiscCount;
    discInitialDirectory = initialDirectory;
    return discSelection;
  }

  std::optional<std::filesystem::path> chooseDirectory(
    QWidget*, const std::filesystem::path& initialDirectory) override
  {
    ++chooseDirectoryCount;
    directoryInitialDirectory = initialDirectory;
    return directorySelection;
  }

  std::optional<std::filesystem::path> chooseShaderPreset(
    QWidget*, const std::filesystem::path& initialDirectory) override
  {
    ++chooseShaderCount;
    shaderInitialDirectory = initialDirectory;
    return shaderSelection;
  }

  void showError(QWidget*, const QString& title, const QString& message) override
  {
    errors.push_back(title + QStringLiteral(": ") + message);
  }
};

class MainWindowTest final : public QObject {
  Q_OBJECT

private slots:
  void shellIsVisibleAndIdentified();
  void menusAndActionsHaveStableSemantics();
  void emptyStatusIsDescriptive();
  void runtimeStatusReportsIdentityAndMeasuredFrameRate();
  void startupAndAudioFailuresAreConsolidatedAndVisible();
  void emulationRuntimeFailuresAreVisible();
  void runtimeConfigurationFailuresAreVisibleAndRejected();
  void aboutDialogReportsBuildIdentity();
  void exitActionClosesWindow();
  void videoActionsDriveDisplayPolicy();
  void videoSettingsDialogAppliesCancelsAndRestores();
  void shaderMenuAndParameterWorkflow();
  void audioSettingsWorkflowAppliesCancelsAndRestores();
  void systemSettingsWorkflowIsValidatedAndDeferred();
  void biosSettingsValidatePersistAndCancel();
  void screenshotCaptureAndSettingsWorkflow();
  void saveStateActionsExposeSlotSemantics();
  void segaCdDiscActionsAreTypedAndRecoverable();
  void gameInformationWorkflowIsAsynchronousAndInspectable();
};

void MainWindowTest::shellIsVisibleAndIdentified()
{
  genplusgx::ui::MainWindow window;
  window.show();
  QApplication::processEvents();

  QCOMPARE(window.objectName(), QStringLiteral("mainWindow"));
  QVERIFY(window.isVisible());
  QVERIFY(window.windowTitle().contains(QString::fromLatin1(GENPLUSGX_VERSION)));
  QCOMPARE(window.minimumSize(), QSize(800, 600));
  QVERIFY(window.acceptDrops());

  auto* canvas = window.findChild<QWidget*>(QStringLiteral("emulatorCanvas"));
  auto* prompt = window.findChild<QLabel*>(QStringLiteral("emptyCanvasLabel"));
  QVERIFY(canvas != nullptr);
  QVERIFY(prompt != nullptr);
  QCOMPARE(canvas, window.centralWidget());
  QVERIFY(!canvas->accessibleName().isEmpty());
  QVERIFY(prompt->text().contains(QStringLiteral("Open")));
  QVERIFY(window.statusBar()->isVisible());
  QVERIFY(prompt->isVisible());

#if defined(Q_OS_LINUX)
  QVERIFY(window.menuBar()->isVisible());
  QVERIFY(window.menuBar()->height() > 0);
  const QImage rendered = window.grab().toImage();
  QVERIFY(!rendered.isNull());
  QVERIFY(rendered.pixelColor(
    window.menuBar()->geometry().center()).value() > 0);
#endif
}

void MainWindowTest::menusAndActionsHaveStableSemantics()
{
  genplusgx::ui::MainWindow window;
  const char* menuNames[]{
    "fileMenu", "emulationMenu", "videoMenu", "audioMenu", "inputMenu",
    "toolsMenu", "helpMenu", "openRecentMenu", "stateSlotMenu", "videoScaleMenu",
    "aspectRatioMenu", "filteringMenu", "overscanMenu", "ntscFilterMenu",
    "interlacedRenderMenu", "shaderMenu", "emulationSpeedMenu"};
  for (const auto* name : menuNames) {
    QVERIFY2(window.findChild<QMenu*>(QString::fromLatin1(name)) != nullptr, name);
  }

  const char* enabledNames[]{
    "openGameAction", "gameLibraryAction", "exitAction", "fullscreenAction",
    "muteAction", "volumeUpAction", "volumeDownAction",
    "controllerConfigurationAction", "playerAssignmentsAction", "settingsAction",
    "videoSettingsAction", "audioSettingsAction",
    "systemSettingsAction", "biosSettingsAction", "screenshotSettingsAction",
    "diagnosticsAction", "userGuideAction", "keyboardShortcutsAction", "aboutAction",
    "aboutQtAction", "shaderDisabledAction", "builtinCrtShaderAction",
    "loadShaderPresetAction", "speedSettingsAction", "emulationSpeed50Action",
    "emulationSpeed75Action", "emulationSpeed100Action",
    "emulationSpeed125Action", "emulationSpeed150Action",
    "emulationSpeed200Action"};
  for (const auto* name : enabledNames) {
    auto* action = window.findChild<QAction*>(QString::fromLatin1(name));
    QVERIFY2(action != nullptr, name);
    QVERIFY2(action->isEnabled(), name);
  }

  const char* gameOnlyNames[]{
    "closeGameAction", "screenshotAction", "pauseAction", "resetAction",
    "softResetAction", "fastForwardAction", "slowMotionAction",
    "frameAdvanceAction", "saveStateAction",
    "loadStateAction", "changeDiscAction", "ejectDiscAction", "cheatsAction",
    "gameInformationAction"};
  for (const auto* name : gameOnlyNames) {
    auto* action = window.findChild<QAction*>(QString::fromLatin1(name));
    QVERIFY2(action != nullptr, name);
    QVERIFY2(!action->isEnabled(), name);
  }
  QVERIFY(!window.findChild<QMenu*>(QStringLiteral("openRecentMenu"))->isEnabled());
  QVERIFY(!window.findChild<QMenu*>(QStringLiteral("stateSlotMenu"))->isEnabled());
  QVERIFY(window.findChild<QAction*>(QStringLiteral("pauseAction"))->isCheckable());
  QVERIFY(window.findChild<QAction*>(QStringLiteral("fastForwardAction"))->isCheckable());
  QVERIFY(window.findChild<QAction*>(QStringLiteral("slowMotionAction"))->isCheckable());
  QVERIFY(window.findChild<QAction*>(QStringLiteral("fullscreenAction"))->isCheckable());
}

void MainWindowTest::emptyStatusIsDescriptive()
{
  genplusgx::ui::MainWindow window;
  QCOMPARE(window.statusBar()->objectName(), QStringLiteral("mainStatusBar"));
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("gameStatusLabel"))->text(),
    QStringLiteral("No game loaded"));
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("systemStatusLabel"))->text(),
    QStringLiteral("System: —"));
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("regionStatusLabel"))->text(),
    QStringLiteral("Region: —"));
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("fpsStatusLabel"))->text(),
    QStringLiteral("0.0 FPS"));
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("stateSlotStatusLabel"))->text(),
    QStringLiteral("Slot 0"));
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("speedStatusLabel"))->text(),
    QStringLiteral("Speed 100%"));
}

void MainWindowTest::runtimeStatusReportsIdentityAndMeasuredFrameRate()
{
  genplusgx::test::TemporaryFixture game{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  genplusgx::ui::MainWindow window;

  window.setGameRuntimeIdentity("Ignored", "Ignored");
  window.setMeasuredFrameRate(60.0);
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("systemStatusLabel"))->text(),
    QStringLiteral("System: —"));
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("fpsStatusLabel"))->text(),
    QStringLiteral("0.0 FPS"));

  window.setGameLoaded(game.path());
  window.setGameRuntimeIdentity("Genesis / Mega Drive", "USA");
  window.setMeasuredFrameRate(59.9227);
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("systemStatusLabel"))->text(),
    QStringLiteral("System: Genesis / Mega Drive"));
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("regionStatusLabel"))->text(),
    QStringLiteral("Region: USA"));
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("fpsStatusLabel"))->text(),
    QStringLiteral("59.9 FPS"));

  window.setGameRuntimeIdentity({}, {});
  window.setMeasuredFrameRate(-1.0);
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("systemStatusLabel"))->text(),
    QStringLiteral("System: Unknown"));
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("regionStatusLabel"))->text(),
    QStringLiteral("Region: Unknown"));
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("fpsStatusLabel"))->text(),
    QStringLiteral("0.0 FPS"));

  window.setNoGameLoaded();
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("systemStatusLabel"))->text(),
    QStringLiteral("System: —"));
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("fpsStatusLabel"))->text(),
    QStringLiteral("0.0 FPS"));
}

void MainWindowTest::startupAndAudioFailuresAreConsolidatedAndVisible()
{
  auto dialogs = std::make_shared<FakeDialogService>();
  genplusgx::ui::MainWindow window;
  window.setDialogService(dialogs);

  window.showStartupIssues({});
  QCOMPARE(dialogs->errors.size(), std::size_t{0});

  std::vector<std::string> issues{
    "Audio output: no playback device is available.",
    "",
    "Audio output: no playback device is available.",
    "Save-state service: the state directory is not writable.",
  };
  for (int index = 0; index < 12; ++index) {
    issues.push_back("Additional startup issue " + std::to_string(index));
  }
  window.showStartupIssues(std::move(issues));
  QCOMPARE(dialogs->errors.size(), std::size_t{1});
  const auto startupError = dialogs->errors.back();
  QVERIFY(startupError.contains(QStringLiteral("Startup Issues")));
  QCOMPARE(
    startupError.count(QStringLiteral("no playback device is available")), 1);
  QVERIFY(startupError.contains(QStringLiteral("safe defaults")));
  QVERIFY(startupError.contains(QStringLiteral("additional issue(s)")));
  QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("issues")));

  window.showAudioOutputError("The selected device disconnected and default recovery failed.");
  QCOMPARE(dialogs->errors.size(), std::size_t{2});
  QVERIFY(dialogs->errors.back().contains(
    QStringLiteral("Audio Output Unavailable")));
  QVERIFY(dialogs->errors.back().contains(QStringLiteral("default recovery failed")));
  QVERIFY(window.statusBar()->currentMessage().contains(
    QStringLiteral("unavailable")));
}

void MainWindowTest::runtimeConfigurationFailuresAreVisibleAndRejected()
{
  auto dialogs = std::make_shared<FakeDialogService>();
  genplusgx::ui::MainWindow window;
  window.setDialogService(dialogs);

  const auto initialVideo = window.videoSettings();
  window.setVideoSettingsSink([](const auto&) {
    return genplusgx::PersistenceStatus{
      .error = genplusgx::PersistenceError::fileWriteFailed,
      .message = "Injected read-only video settings file.",
    };
  });
  window.findChild<QAction*>(QStringLiteral("integerScaleAction"))->trigger();
  QCOMPARE(window.videoSettings(), initialVideo);
  QVERIFY(window.findChild<QAction*>(QStringLiteral("fitScaleAction"))->isChecked());
  QVERIFY(dialogs->errors.back().contains(QStringLiteral("Video Settings Error")));
  QVERIFY(dialogs->errors.back().contains(QStringLiteral("read-only")));

  const auto initialSystem = window.systemSettings();
  window.setSystemSettingsSink([](const auto&) {
    return genplusgx::PersistenceStatus{
      .error = genplusgx::PersistenceError::fileWriteFailed,
      .message = "Injected system persistence failure.",
    };
  });
  window.showSystemSettings();
  auto* systemDialog = window.findChild<genplusgx::ui::SystemSettingsDialog*>(
    QStringLiteral("systemSettingsDialog"));
  QVERIFY(systemDialog != nullptr);
  auto* hardware = systemDialog->findChild<QComboBox*>(
    QStringLiteral("systemHardwareCombo"));
  hardware->setCurrentIndex(hardware->findData(
    static_cast<int>(genplusgx::CoreSystemHardware::gameGear)));
  QTest::mouseClick(systemDialog->findChild<QPushButton*>(
    QStringLiteral("okSystemSettingsButton")), Qt::LeftButton);
  QVERIFY(systemDialog->isVisible());
  QCOMPARE(window.systemSettings(), initialSystem);
  QVERIFY(dialogs->errors.back().contains(QStringLiteral("System Settings Error")));
  systemDialog->reject();

  window.setInputConfigurationSink([](const auto&) {
    return genplusgx::PersistenceStatus{
      .error = genplusgx::PersistenceError::fileWriteFailed,
      .message = "Injected input profile persistence failure.",
    };
  });
  window.showInputConfiguration();
  auto* inputDialog = window.findChild<genplusgx::ui::InputConfigurationDialog*>(
    QStringLiteral("inputConfigurationDialog"));
  QVERIFY(inputDialog != nullptr);
  QTest::mouseClick(inputDialog->findChild<QPushButton*>(
    QStringLiteral("inputOkButton")), Qt::LeftButton);
  QVERIFY(inputDialog->isVisible());
  QVERIFY(dialogs->errors.back().contains(
    QStringLiteral("Input Configuration Error")));
  inputDialog->reject();

  genplusgx::test::TemporaryFixture game{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  window.setRecentGames({game.path()});
  window.setClearRecentGamesSink([] {
    return genplusgx::PersistenceStatus{
      .error = genplusgx::PersistenceError::fileWriteFailed,
      .message = "Injected recent-history persistence failure.",
    };
  });
  window.findChild<QAction*>(QStringLiteral("clearRecentGamesAction"))->trigger();
  QApplication::processEvents();
  QVERIFY(window.findChild<QMenu*>(QStringLiteral("openRecentMenu"))->isEnabled());
  QVERIFY(dialogs->errors.back().contains(QStringLiteral("Recent Games Error")));
}

void MainWindowTest::emulationRuntimeFailuresAreVisible()
{
  auto dialogs = std::make_shared<FakeDialogService>();
  genplusgx::ui::MainWindow window;
  window.setDialogService(dialogs);

  window.showEmulationRuntimeError(
    "The emulation worker stopped after a synthetic frame failure.");

  QCOMPARE(dialogs->errors.size(), std::size_t{1});
  QVERIFY(dialogs->errors.front().contains(
    QStringLiteral("Emulation Service Error")));
  QVERIFY(dialogs->errors.front().contains(QStringLiteral("frame failure")));
  QVERIFY(window.statusBar()->currentMessage().contains(
    QStringLiteral("operation failed")));
}

void MainWindowTest::aboutDialogReportsBuildIdentity()
{
  genplusgx::ui::MainWindow window;
  window.show();
  window.findChild<QAction*>(QStringLiteral("aboutAction"))->trigger();
  QApplication::processEvents();

  auto* dialog = window.findChild<genplusgx::ui::AboutDialog*>(QStringLiteral("aboutDialog"));
  QVERIFY(dialog != nullptr);
  QVERIFY(dialog->isVisible());
  QVERIFY(dialog->isModal());
  const auto text = dialog->findChild<QLabel*>(QStringLiteral("aboutVersionLabel"))->text();
  QVERIFY(text.contains(QString::fromLatin1(GENPLUSGX_VERSION)));
  QVERIFY(text.contains(QString::fromLatin1(GENPLUSGX_GIT_COMMIT)));
  QVERIFY(dialog->findChild<QLabel*>(QStringLiteral("aboutLicenseLabel"))->text()
    .contains(QStringLiteral("No games")));
  dialog->close();
}

void MainWindowTest::exitActionClosesWindow()
{
  genplusgx::ui::MainWindow window;
  window.show();
  QApplication::processEvents();
  QVERIFY(window.isVisible());
  window.findChild<QAction*>(QStringLiteral("exitAction"))->trigger();
  QApplication::processEvents();
  QVERIFY(!window.isVisible());
}

void MainWindowTest::videoActionsDriveDisplayPolicy()
{
  genplusgx::ui::MainWindow window;
  std::vector<genplusgx::settings::VideoSettings> updates;
  window.setVideoSettingsSink([&updates](const auto& settings) {
    updates.push_back(settings);
    return genplusgx::PersistenceStatus{};
  });
  window.show();
  QApplication::processEvents();
  auto* display = window.displayWidget();

  window.findChild<QAction*>(QStringLiteral("integerScaleAction"))->trigger();
  QCOMPARE(display->scaleMode(), genplusgx::video::ScaleMode::integer);
  window.findChild<QAction*>(QStringLiteral("fourThreeAspectAction"))->trigger();
  QCOMPARE(display->aspectMode(), genplusgx::video::AspectMode::fourThree);
  window.findChild<QAction*>(QStringLiteral("stretchAspectAction"))->trigger();
  QCOMPARE(display->aspectMode(), genplusgx::video::AspectMode::stretch);
  window.findChild<QAction*>(QStringLiteral("bilinearFilterAction"))->trigger();
  QCOMPARE(display->videoFilter(), genplusgx::video::VideoFilter::bilinear);

  window.findChild<QAction*>(QStringLiteral("fitScaleAction"))->trigger();
  window.findChild<QAction*>(QStringLiteral("nativeAspectAction"))->trigger();
  window.findChild<QAction*>(QStringLiteral("nearestFilterAction"))->trigger();
  QCOMPARE(display->scaleMode(), genplusgx::video::ScaleMode::fit);
  QCOMPARE(display->aspectMode(), genplusgx::video::AspectMode::native);
  QCOMPARE(display->videoFilter(), genplusgx::video::VideoFilter::nearest);

  window.findChild<QAction*>(QStringLiteral("overscanFullAction"))->trigger();
  QCOMPARE(window.videoSettings().core.overscan,
    genplusgx::CoreOverscanMode::full);
  window.findChild<QAction*>(QStringLiteral("ntscSVideoAction"))->trigger();
  QCOMPARE(window.videoSettings().core.ntscFilter,
    genplusgx::CoreNtscFilter::sVideo);
  window.findChild<QAction*>(QStringLiteral("doubleFieldRenderAction"))->trigger();
  QCOMPARE(window.videoSettings().core.interlacedRender,
    genplusgx::CoreInterlacedRenderMode::doubleField);
  window.findChild<QAction*>(
    QStringLiteral("gameGearExtendedScreenAction"))->trigger();
  QVERIFY(window.videoSettings().core.gameGearExtendedScreen);
  QVERIFY(updates.size() >= 10U);
  QCOMPARE(updates.back(), window.videoSettings());

  auto* fullscreen = window.findChild<QAction*>(QStringLiteral("fullscreenAction"));
  fullscreen->trigger();
  QApplication::processEvents();
  QVERIFY(window.isFullScreen());
  QVERIFY(fullscreen->isChecked());
  fullscreen->trigger();
  QApplication::processEvents();
  QVERIFY(!window.isFullScreen());
  QVERIFY(!fullscreen->isChecked());
}

void MainWindowTest::videoSettingsDialogAppliesCancelsAndRestores()
{
  genplusgx::ui::MainWindow window;
  genplusgx::settings::VideoSettings initial;
  initial.aspect = genplusgx::video::AspectMode::fourThree;
  initial.core.overscan = genplusgx::CoreOverscanMode::vertical;
  window.setVideoSettings(initial);
  std::vector<genplusgx::settings::VideoSettings> updates;
  window.setVideoSettingsSink([&updates](const auto& settings) {
    updates.push_back(settings);
    return genplusgx::PersistenceStatus{};
  });
  window.show();

  window.findChild<QAction*>(QStringLiteral("videoSettingsAction"))->trigger();
  QApplication::processEvents();
  auto* dialog = window.findChild<genplusgx::ui::VideoSettingsDialog*>(
    QStringLiteral("videoSettingsDialog"));
  QVERIFY(dialog != nullptr);
  QVERIFY(dialog->isVisible());
  auto* aspect = dialog->findChild<QComboBox*>(QStringLiteral("videoAspectCombo"));
  auto* scaling = dialog->findChild<QComboBox*>(QStringLiteral("videoScalingCombo"));
  auto* filter = dialog->findChild<QComboBox*>(
    QStringLiteral("videoPresentationFilterCombo"));
  auto* shaderMode = dialog->findChild<QComboBox*>(
    QStringLiteral("shaderModeCombo"));
  auto* overscan = dialog->findChild<QComboBox*>(QStringLiteral("coreOverscanCombo"));
  auto* ntsc = dialog->findChild<QComboBox*>(QStringLiteral("coreNtscFilterCombo"));
  auto* render = dialog->findChild<QComboBox*>(
    QStringLiteral("coreInterlacedRenderCombo"));
  auto* gameGear = dialog->findChild<QCheckBox*>(
    QStringLiteral("gameGearExtendedScreenCheckBox"));
  auto* apply = dialog->findChild<QPushButton*>(
    QStringLiteral("applyVideoSettingsButton"));
  auto* restore = dialog->findChild<QPushButton*>(
    QStringLiteral("restoreVideoDefaultsButton"));
  QVERIFY(aspect != nullptr && scaling != nullptr && filter != nullptr);
  QVERIFY(shaderMode != nullptr);
  QVERIFY(overscan != nullptr && ntsc != nullptr && render != nullptr);
  QVERIFY(gameGear != nullptr && apply != nullptr && restore != nullptr);
  QCOMPARE(aspect->currentData().toInt(),
    static_cast<int>(genplusgx::video::AspectMode::fourThree));
  QCOMPARE(overscan->currentData().toInt(),
    static_cast<int>(genplusgx::CoreOverscanMode::vertical));
  QCOMPARE(aspect->count(), 3);
  QCOMPARE(scaling->count(), 2);
  QCOMPARE(filter->count(), 2);
  QCOMPARE(shaderMode->count(), 3);
  QCOMPARE(overscan->count(), 4);
  QCOMPARE(ntsc->count(), 5);
  QCOMPARE(render->count(), 2);

  aspect->setCurrentIndex(aspect->findData(
    static_cast<int>(genplusgx::video::AspectMode::stretch)));
  dialog->reject();
  QApplication::processEvents();
  QVERIFY(updates.empty());
  QCOMPARE(window.videoSettings(), initial);

  window.findChild<QAction*>(QStringLiteral("videoSettingsAction"))->trigger();
  QApplication::processEvents();
  dialog = window.findChild<genplusgx::ui::VideoSettingsDialog*>(
    QStringLiteral("videoSettingsDialog"));
  QVERIFY(dialog != nullptr);
  scaling = dialog->findChild<QComboBox*>(QStringLiteral("videoScalingCombo"));
  filter = dialog->findChild<QComboBox*>(
    QStringLiteral("videoPresentationFilterCombo"));
  ntsc = dialog->findChild<QComboBox*>(QStringLiteral("coreNtscFilterCombo"));
  render = dialog->findChild<QComboBox*>(
    QStringLiteral("coreInterlacedRenderCombo"));
  gameGear = dialog->findChild<QCheckBox*>(
    QStringLiteral("gameGearExtendedScreenCheckBox"));
  apply = dialog->findChild<QPushButton*>(
    QStringLiteral("applyVideoSettingsButton"));
  restore = dialog->findChild<QPushButton*>(
    QStringLiteral("restoreVideoDefaultsButton"));
  scaling->setCurrentIndex(scaling->findData(
    static_cast<int>(genplusgx::video::ScaleMode::integer)));
  filter->setCurrentIndex(filter->findData(
    static_cast<int>(genplusgx::video::VideoFilter::bilinear)));
  ntsc->setCurrentIndex(ntsc->findData(
    static_cast<int>(genplusgx::CoreNtscFilter::rgb)));
  render->setCurrentIndex(render->findData(
    static_cast<int>(genplusgx::CoreInterlacedRenderMode::doubleField)));
  gameGear->setChecked(true);
  QTest::mouseClick(apply, Qt::LeftButton);
  QCOMPARE(updates.size(), std::size_t{1});
  QCOMPARE(window.videoSettings().scaling,
    genplusgx::video::ScaleMode::integer);
  QCOMPARE(window.videoSettings().presentationFilter,
    genplusgx::video::VideoFilter::bilinear);
  QCOMPARE(window.videoSettings().core.ntscFilter,
    genplusgx::CoreNtscFilter::rgb);
  QVERIFY(window.videoSettings().core.gameGearExtendedScreen);
  QCOMPARE(window.displayWidget()->scaleMode(),
    genplusgx::video::ScaleMode::integer);

  QTest::mouseClick(restore, Qt::LeftButton);
  QCOMPARE(updates.size(), std::size_t{1});
  QTest::mouseClick(apply, Qt::LeftButton);
  QCOMPARE(updates.size(), std::size_t{2});
  QCOMPARE(window.videoSettings(),
    genplusgx::settings::defaultVideoSettings());
  dialog->close();
}

void MainWindowTest::shaderMenuAndParameterWorkflow()
{
  genplusgx::ui::MainWindow window;
  auto dialogs = std::make_shared<FakeDialogService>();
  window.setDialogService(dialogs);
  std::vector<genplusgx::settings::VideoSettings> updates;
  window.setVideoSettingsSink([&updates](const auto& settings) {
    updates.push_back(settings);
    return genplusgx::PersistenceStatus{};
  });

  auto* off = window.findChild<QAction*>(QStringLiteral("shaderDisabledAction"));
  auto* crt = window.findChild<QAction*>(QStringLiteral("builtinCrtShaderAction"));
  auto* custom = window.findChild<QAction*>(QStringLiteral("customShaderAction"));
  auto* load = window.findChild<QAction*>(QStringLiteral("loadShaderPresetAction"));
  auto* parameters = window.findChild<QAction*>(QStringLiteral("shaderParametersAction"));
  QVERIFY(off != nullptr && crt != nullptr && custom != nullptr);
  QVERIFY(load != nullptr && parameters != nullptr);
  QVERIFY(off->isChecked());
  QVERIFY(!custom->isEnabled());
  QVERIFY(!parameters->isEnabled());

#if !GENPLUSGX_HAS_LIBRETRO_SHADERS
  auto* shaderMenu = window.findChild<QMenu*>(QStringLiteral("shaderMenu"));
  QVERIFY(shaderMenu != nullptr);
  QVERIFY(!shaderMenu->isEnabled());
  return;
#endif

  crt->trigger();
  QCOMPARE(window.videoSettings().shader.mode,
    genplusgx::video::ShaderMode::builtinCrt);
  QCOMPARE(window.displayWidget()->shaderConfiguration().mode,
    genplusgx::video::ShaderMode::builtinCrt);
  QVERIFY(crt->isChecked());
  QVERIFY(parameters->isEnabled());
  QCOMPARE(updates.size(), std::size_t{1});

  parameters->trigger();
  QApplication::processEvents();
  auto* dialog = window.findChild<genplusgx::ui::VideoSettingsDialog*>(
    QStringLiteral("videoSettingsDialog"));
  QVERIFY(dialog != nullptr);
  auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("shaderModeCombo"));
  auto* curvature = dialog->findChild<QDoubleSpinBox*>(
    QStringLiteral("shaderParameter_CURVATURE"));
  auto* validation = dialog->findChild<QLabel*>(
    QStringLiteral("shaderValidationLabel"));
  auto* apply = dialog->findChild<QPushButton*>(
    QStringLiteral("applyVideoSettingsButton"));
  QVERIFY(mode != nullptr && curvature != nullptr && validation != nullptr);
  QVERIFY(apply != nullptr);
  QCOMPARE(mode->currentData().toInt(),
    static_cast<int>(genplusgx::video::ShaderMode::builtinCrt));
  QVERIFY(validation->text().contains(QStringLiteral("valid")));
  curvature->setValue(0.12);
  QTest::mouseClick(apply, Qt::LeftButton);
  QCOMPARE(updates.size(), std::size_t{2});
  const auto savedCurvature = std::find_if(
    window.videoSettings().shader.parameters.cbegin(),
    window.videoSettings().shader.parameters.cend(),
    [](const auto& parameter) { return parameter.name == "CURVATURE"; });
  QVERIFY(savedCurvature != window.videoSettings().shader.parameters.cend());
  QCOMPARE(savedCurvature->value, 0.12F);
  dialog->close();

  dialogs->shaderSelection = genplusgx::video::builtinCrtPresetPath();
  load->trigger();
  QCOMPARE(dialogs->chooseShaderCount, 1);
  QCOMPARE(window.videoSettings().shader.mode,
    genplusgx::video::ShaderMode::libretroPreset);
  QCOMPARE(window.videoSettings().shader.presetPath,
    *dialogs->shaderSelection);
  QVERIFY(custom->isEnabled());
  QVERIFY(custom->isChecked());
  QVERIFY(custom->text().contains(QStringLiteral("genplusgx-crt.slangp")));

  off->trigger();
  QCOMPARE(window.videoSettings().shader.mode,
    genplusgx::video::ShaderMode::disabled);
  QVERIFY(off->isChecked());
  QVERIFY(!parameters->isEnabled());
}

void MainWindowTest::audioSettingsWorkflowAppliesCancelsAndRestores()
{
  genplusgx::ui::MainWindow window;
  auto dialogs = std::make_shared<FakeDialogService>();
  window.setDialogService(dialogs);
  genplusgx::settings::AudioSettings initial;
  initial.masterVolumePercent = 70;
  initial.latencyMilliseconds = 45;
  initial.outputDeviceName = "Configured test output";
  initial.core.psgLevelPercent = 125;
  window.setAvailableAudioDevices({"Configured test output", "Other output"});
  window.setAudioSettings(initial);
  std::vector<genplusgx::settings::AudioSettings> updates;
  window.setAudioSettingsSink([&updates](const auto& settings) {
    updates.push_back(settings);
  });

  auto* muteAction = window.findChild<QAction*>(QStringLiteral("muteAction"));
  auto* volumeUp = window.findChild<QAction*>(QStringLiteral("volumeUpAction"));
  auto* volumeDown = window.findChild<QAction*>(QStringLiteral("volumeDownAction"));
  QVERIFY(muteAction != nullptr && volumeUp != nullptr && volumeDown != nullptr);
  muteAction->trigger();
  QVERIFY(window.audioSettings().muted);
  volumeUp->trigger();
  QCOMPARE(window.audioSettings().masterVolumePercent, 75);
  volumeDown->trigger();
  QCOMPARE(window.audioSettings().masterVolumePercent, 70);
  QCOMPARE(updates.size(), std::size_t{3});

  window.findChild<QAction*>(QStringLiteral("audioSettingsAction"))->trigger();
  QApplication::processEvents();
  auto* dialog = window.findChild<genplusgx::ui::AudioSettingsDialog*>(
    QStringLiteral("audioSettingsDialog"));
  QVERIFY(dialog != nullptr && dialog->isVisible());
  auto* device = dialog->findChild<QComboBox*>(
    QStringLiteral("audioOutputDeviceCombo"));
  auto* latency = dialog->findChild<QSpinBox*>(
    QStringLiteral("audioLatencySpinBox"));
  auto* liveApplyInformation = dialog->findChild<QLabel*>(
    QStringLiteral("audioLiveApplyInformationLabel"));
  auto* volume = dialog->findChild<QSpinBox*>(
    QStringLiteral("masterVolumeSpinBox"));
  auto* filter = dialog->findChild<QComboBox*>(
    QStringLiteral("coreAudioFilterCombo"));
  auto* output = dialog->findChild<QComboBox*>(
    QStringLiteral("coreSoundOutputCombo"));
  auto* ym2612 = dialog->findChild<QComboBox*>(
    QStringLiteral("ym2612CoreCombo"));
  auto* ym2413Mode = dialog->findChild<QComboBox*>(
    QStringLiteral("ym2413ModeCombo"));
  auto* ym2413Core = dialog->findChild<QComboBox*>(
    QStringLiteral("ym2413CoreCombo"));
  auto* lowPass = dialog->findChild<QSpinBox*>(
    QStringLiteral("lowPassRangeSpinBox"));
  auto* eqLow = dialog->findChild<QSpinBox*>(
    QStringLiteral("equalizerLowSpinBox"));
  auto* psg = dialog->findChild<QSpinBox*>(QStringLiteral("psgLevelSpinBox"));
  auto* apply = dialog->findChild<QPushButton*>(
    QStringLiteral("applyAudioSettingsButton"));
  auto* restore = dialog->findChild<QPushButton*>(
    QStringLiteral("restoreAudioDefaultsButton"));
  QVERIFY(device != nullptr && latency != nullptr && volume != nullptr);
  QVERIFY(liveApplyInformation != nullptr);
  QVERIFY(liveApplyInformation->text().contains(QStringLiteral("immediately")));
  QVERIFY(dialog->findChild<QLabel*>(
    QStringLiteral("audioRestartRequiredLabel")) == nullptr);
  QVERIFY(filter != nullptr && lowPass != nullptr && eqLow != nullptr);
  QVERIFY(output != nullptr && ym2612 != nullptr);
  QVERIFY(ym2413Mode != nullptr && ym2413Core != nullptr);
  QVERIFY(psg != nullptr && apply != nullptr && restore != nullptr);
  QCOMPARE(device->currentData().toString(),
    QStringLiteral("Configured test output"));
  window.setAvailableAudioDevices({"Other output", "Hot-plugged output"});
  QCOMPARE(device->currentData().toString(),
    QStringLiteral("Configured test output"));
  QVERIFY(device->currentText().contains(QStringLiteral("unavailable")));
  QVERIFY(device->findData(QStringLiteral("Hot-plugged output")) >= 0);
  QCOMPARE(latency->value(), 45);
  QCOMPARE(psg->value(), 125);
  QCOMPARE(output->count(), 2);
  QCOMPARE(filter->count(), 3);
  QCOMPARE(ym2612->count(), 5);
  QCOMPARE(ym2413Mode->count(), 3);
  QCOMPARE(ym2413Core->count(), 2);
  QCOMPARE(volume->minimum(), 0);
  QCOMPARE(volume->maximum(), 100);
  QCOMPARE(latency->minimum(), 10);
  QCOMPARE(latency->maximum(), 500);
  QCOMPARE(psg->minimum(), 0);
  QCOMPARE(psg->maximum(), 200);
  QCOMPARE(lowPass->minimum(), 5);
  QCOMPARE(lowPass->maximum(), 95);

  volume->setValue(20);
  dialog->reject();
  QApplication::processEvents();
  QCOMPARE(window.audioSettings().masterVolumePercent, 70);
  QCOMPARE(updates.size(), std::size_t{3});

  window.findChild<QAction*>(QStringLiteral("audioSettingsAction"))->trigger();
  QApplication::processEvents();
  dialog = window.findChild<genplusgx::ui::AudioSettingsDialog*>(
    QStringLiteral("audioSettingsDialog"));
  volume = dialog->findChild<QSpinBox*>(QStringLiteral("masterVolumeSpinBox"));
  filter = dialog->findChild<QComboBox*>(QStringLiteral("coreAudioFilterCombo"));
  lowPass = dialog->findChild<QSpinBox*>(QStringLiteral("lowPassRangeSpinBox"));
  eqLow = dialog->findChild<QSpinBox*>(QStringLiteral("equalizerLowSpinBox"));
  apply = dialog->findChild<QPushButton*>(QStringLiteral("applyAudioSettingsButton"));
  restore = dialog->findChild<QPushButton*>(QStringLiteral("restoreAudioDefaultsButton"));
  volume->setValue(40);
  filter->setCurrentIndex(filter->findData(
    static_cast<int>(genplusgx::CoreAudioFilter::equalizer)));
  QVERIFY(!lowPass->isEnabled() && eqLow->isEnabled());
  eqLow->setValue(130);
  QTest::mouseClick(apply, Qt::LeftButton);
  QCOMPARE(updates.size(), std::size_t{4});
  QCOMPARE(window.audioSettings().masterVolumePercent, 40);
  QCOMPARE(window.audioSettings().core.filter,
    genplusgx::CoreAudioFilter::equalizer);
  QCOMPARE(window.audioSettings().core.equalizerLowPercent, 130);

  QTest::mouseClick(restore, Qt::LeftButton);
  QCOMPARE(updates.size(), std::size_t{4});
  QTest::mouseClick(apply, Qt::LeftButton);
  QCOMPARE(updates.size(), std::size_t{5});
  QCOMPARE(window.audioSettings(), genplusgx::settings::defaultAudioSettings());
  window.showAudioSettingsError("Injected device-open failure.");
  QCOMPARE(dialogs->errors.size(), std::size_t{1U});
  QVERIFY(dialogs->errors.front().contains(QStringLiteral("device-open")));
  dialog->close();
}

void MainWindowTest::systemSettingsWorkflowIsValidatedAndDeferred()
{
  genplusgx::ui::MainWindow window;
  genplusgx::CoreSystemSettings initial;
  initial.hardware = genplusgx::CoreSystemHardware::masterSystem;
  initial.region = genplusgx::CoreSystemRegion::ntscU;
  window.setSystemSettings(initial);
  std::vector<genplusgx::CoreSystemSettings> updates;
  window.setSystemSettingsSink([&updates](const auto& settings) {
    updates.push_back(settings);
    return genplusgx::PersistenceStatus{};
  });

  window.findChild<QAction*>(QStringLiteral("systemSettingsAction"))->trigger();
  QApplication::processEvents();
  auto* dialog = window.findChild<genplusgx::ui::SystemSettingsDialog*>(
    QStringLiteral("systemSettingsDialog"));
  QVERIFY(dialog != nullptr && dialog->isVisible());
  auto* hardware = dialog->findChild<QComboBox*>(
    QStringLiteral("systemHardwareCombo"));
  auto* region = dialog->findChild<QComboBox*>(QStringLiteral("systemRegionCombo"));
  auto* vdp = dialog->findChild<QComboBox*>(QStringLiteral("vdpModeCombo"));
  auto* clock = dialog->findChild<QComboBox*>(QStringLiteral("masterClockCombo"));
  auto* lockups = dialog->findChild<QCheckBox*>(
    QStringLiteral("illegalAccessLockupsCheckBox"));
  auto* addressErrors = dialog->findChild<QCheckBox*>(
    QStringLiteral("addressErrorsCheckBox"));
  auto* apply = dialog->findChild<QPushButton*>(
    QStringLiteral("applySystemSettingsButton"));
  auto* restore = dialog->findChild<QPushButton*>(
    QStringLiteral("restoreSystemDefaultsButton"));
  QVERIFY(hardware != nullptr && region != nullptr && vdp != nullptr);
  QVERIFY(clock != nullptr && lockups != nullptr && addressErrors != nullptr);
  QVERIFY(apply != nullptr && restore != nullptr);
  QCOMPARE(hardware->currentData().toInt(),
    static_cast<int>(genplusgx::CoreSystemHardware::masterSystem));
  QCOMPARE(region->currentData().toInt(),
    static_cast<int>(genplusgx::CoreSystemRegion::ntscU));
  QCOMPARE(hardware->count(), 9);
  QCOMPARE(region->count(), 5);
  QCOMPARE(vdp->count(), 3);
  QCOMPARE(clock->count(), 3);

  hardware->setCurrentIndex(hardware->findData(
    static_cast<int>(genplusgx::CoreSystemHardware::gameGear)));
  dialog->reject();
  QApplication::processEvents();
  QVERIFY(updates.empty());
  QCOMPARE(window.systemSettings(), initial);

  window.findChild<QAction*>(QStringLiteral("systemSettingsAction"))->trigger();
  QApplication::processEvents();
  dialog = window.findChild<genplusgx::ui::SystemSettingsDialog*>(
    QStringLiteral("systemSettingsDialog"));
  hardware = dialog->findChild<QComboBox*>(QStringLiteral("systemHardwareCombo"));
  region = dialog->findChild<QComboBox*>(QStringLiteral("systemRegionCombo"));
  vdp = dialog->findChild<QComboBox*>(QStringLiteral("vdpModeCombo"));
  clock = dialog->findChild<QComboBox*>(QStringLiteral("masterClockCombo"));
  lockups = dialog->findChild<QCheckBox*>(
    QStringLiteral("illegalAccessLockupsCheckBox"));
  addressErrors = dialog->findChild<QCheckBox*>(
    QStringLiteral("addressErrorsCheckBox"));
  apply = dialog->findChild<QPushButton*>(QStringLiteral("applySystemSettingsButton"));
  restore = dialog->findChild<QPushButton*>(QStringLiteral("restoreSystemDefaultsButton"));
  hardware->setCurrentIndex(hardware->findData(
    static_cast<int>(genplusgx::CoreSystemHardware::sg1000IIRamExtension)));
  region->setCurrentIndex(region->findData(
    static_cast<int>(genplusgx::CoreSystemRegion::palJapan)));
  vdp->setCurrentIndex(vdp->findData(
    static_cast<int>(genplusgx::CoreVideoStandard::pal)));
  clock->setCurrentIndex(clock->findData(
    static_cast<int>(genplusgx::CoreMasterClock::ntsc)));
  lockups->setChecked(false);
  addressErrors->setChecked(false);
  QTest::mouseClick(apply, Qt::LeftButton);
  QCOMPARE(updates.size(), std::size_t{1});
  QCOMPARE(window.systemSettings().hardware,
    genplusgx::CoreSystemHardware::sg1000IIRamExtension);
  QCOMPARE(window.systemSettings().region,
    genplusgx::CoreSystemRegion::palJapan);
  QCOMPARE(window.systemSettings().videoStandard,
    genplusgx::CoreVideoStandard::pal);
  QCOMPARE(window.systemSettings().masterClock,
    genplusgx::CoreMasterClock::ntsc);
  QVERIFY(!window.systemSettings().emulateIllegalAccessLockups);
  QVERIFY(!window.systemSettings().enableAddressErrors);
  QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("next game")));

  QTest::mouseClick(restore, Qt::LeftButton);
  QCOMPARE(updates.size(), std::size_t{1});
  QTest::mouseClick(apply, Qt::LeftButton);
  QCOMPARE(updates.size(), std::size_t{2});
  QCOMPARE(window.systemSettings(), genplusgx::CoreSystemSettings{});
  dialog->close();
}

void MainWindowTest::biosSettingsValidatePersistAndCancel()
{
  using genplusgx::platform::BiosSlot;
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto firmwarePath = std::filesystem::path{
    directory.filePath(QStringLiteral("segacd-us.bin")).toStdString()};
  std::vector<std::uint8_t> bytes(128U * 1024U);
  for (std::size_t index = 0U; index < bytes.size(); ++index) {
    bytes[index] = static_cast<std::uint8_t>((index * 29U + 7U) & 0xffU);
  }
  QVERIFY(genplusgx::writeFileAtomically(
    firmwarePath, bytes, bytes.size()));

  genplusgx::ui::MainWindow window;
  genplusgx::platform::BiosSnapshot initial;
  for (const auto& descriptor : genplusgx::platform::biosDescriptors()) {
    initial.validation[static_cast<std::size_t>(descriptor.slot)] =
      genplusgx::platform::validateBios(descriptor.slot, {});
  }
  initial.configuration.setPath(
    BiosSlot::genesis, directory.path().toStdString() + "/missing.bin");
  initial.validation[static_cast<std::size_t>(BiosSlot::genesis)] =
    genplusgx::platform::validateBios(
      BiosSlot::genesis, initial.configuration.path(BiosSlot::genesis));
  window.setBiosSnapshot(initial);
  std::vector<genplusgx::platform::BiosConfiguration> updates;
  window.setBiosConfigurationSink(
    [&window, &updates](const auto& configuration) {
      updates.push_back(configuration);
      genplusgx::platform::BiosSnapshot applied;
      applied.configuration = configuration;
      for (const auto& descriptor : genplusgx::platform::biosDescriptors()) {
        applied.validation[static_cast<std::size_t>(descriptor.slot)] =
          genplusgx::platform::validateBios(
            descriptor.slot, configuration.path(descriptor.slot));
      }
      window.setBiosSnapshot(std::move(applied));
      return genplusgx::PersistenceStatus{};
    });

  window.findChild<QAction*>(QStringLiteral("biosSettingsAction"))->trigger();
  QApplication::processEvents();
  auto* dialog = window.findChild<genplusgx::ui::BiosSettingsDialog*>(
    QStringLiteral("biosSettingsDialog"));
  QVERIFY(dialog != nullptr && dialog->isVisible());
  QVERIFY(dialog->findChild<QLabel*>(QStringLiteral("biosStatus_genesis"))
    ->text().contains(QStringLiteral("Missing")));
  dialog->setFilePicker(
    [&firmwarePath](BiosSlot slot, const std::filesystem::path&)
      -> std::optional<std::filesystem::path> {
      return slot == BiosSlot::segaCdUsa
        ? std::optional{firmwarePath} : std::nullopt;
    });
  auto* browse = dialog->findChild<QPushButton*>(
    QStringLiteral("biosBrowse_segaCdUsa"));
  auto* apply = dialog->findChild<QPushButton*>(
    QStringLiteral("applyBiosSettingsButton"));
  auto* checksum = dialog->findChild<QLineEdit*>(
    QStringLiteral("biosChecksum_segaCdUsa"));
  QVERIFY(browse != nullptr && apply != nullptr && checksum != nullptr);
  browse->click();
  QCOMPARE(checksum->text().size(), 64);
  QVERIFY(dialog->findChild<QLabel*>(QStringLiteral("biosStatus_segaCdUsa"))
    ->text().contains(QStringLiteral("Valid")));
  apply->click();
  QCOMPARE(updates.size(), std::size_t{1});
  QCOMPARE(window.biosSnapshot().configuration.path(BiosSlot::segaCdUsa),
    firmwarePath);
  QVERIFY(window.biosSnapshot().validation[
    static_cast<std::size_t>(BiosSlot::segaCdUsa)].valid());
  dialog->reject();
  QApplication::processEvents();

  window.findChild<QAction*>(QStringLiteral("biosSettingsAction"))->trigger();
  QApplication::processEvents();
  dialog = window.findChild<genplusgx::ui::BiosSettingsDialog*>(
    QStringLiteral("biosSettingsDialog"));
  QVERIFY(dialog != nullptr);
  dialog->findChild<QPushButton*>(QStringLiteral("biosClear_segaCdUsa"))->click();
  dialog->reject();
  QApplication::processEvents();
  QCOMPARE(updates.size(), std::size_t{1});
  QCOMPARE(window.biosSnapshot().configuration.path(BiosSlot::segaCdUsa),
    firmwarePath);

  window.findChild<QAction*>(QStringLiteral("biosSettingsAction"))->trigger();
  QApplication::processEvents();
  dialog = window.findChild<genplusgx::ui::BiosSettingsDialog*>(
    QStringLiteral("biosSettingsDialog"));
  dialog->findChild<QPushButton*>(
    QStringLiteral("restoreBiosDefaultsButton"))->click();
  dialog->findChild<QPushButton*>(
    QStringLiteral("applyBiosSettingsButton"))->click();
  QCOMPARE(updates.size(), std::size_t{2});
  QCOMPARE(window.biosSnapshot().configuration,
    genplusgx::platform::BiosConfiguration{});
  dialog->close();
}

void MainWindowTest::screenshotCaptureAndSettingsWorkflow()
{
  genplusgx::test::TemporaryFixture game{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto root = genplusgx::ui::pathFromQString(temporary.path());
  const auto defaultDirectory = root / "default-screenshots";
  const auto initialDirectory = root / "initial-screenshots";
  const auto selectedDirectory = root / "selected-screenshots";
  auto dialogs = std::make_shared<FakeDialogService>();
  dialogs->directorySelection = selectedDirectory;

  genplusgx::ui::MainWindow window;
  window.setDialogService(dialogs);
  window.setScreenshotSettings(
    {.directory = initialDirectory}, defaultDirectory);
  auto exchange = std::make_shared<genplusgx::VideoFrameExchange>(4U);
  window.displayWidget()->setFrameExchange(exchange);
  window.setGameLoaded(game.path());
  std::optional<genplusgx::CoreVideoFrameInfo> capturedFrame;
  std::vector<std::uint16_t> capturedPixels;
  window.setScreenshotSink(
    [&capturedFrame, &capturedPixels](const auto& currentFrame, auto pixels) {
      capturedFrame = currentFrame;
      capturedPixels = std::move(pixels);
    });
  auto* screenshot = window.findChild<QAction*>(
    QStringLiteral("screenshotAction"));
  QVERIFY(screenshot != nullptr && !screenshot->isEnabled());

  auto lease = exchange->beginWrite();
  QVERIFY(lease.has_value());
  constexpr std::array<std::uint16_t, 4> expectedPixels{
    0xf800U, 0x07e0U, 0x001fU, 0xffffU};
  std::ranges::copy(expectedPixels, lease->pixels().begin());
  const genplusgx::CoreVideoFrameInfo frame{
    .format = genplusgx::CorePixelFormat::rgb565,
    .width = 2U,
    .height = 2U,
    .frameNumber = 77U,
  };
  QVERIFY(lease->publish(frame));
  QVERIFY(window.presentLatestFrame());
  QVERIFY(screenshot->isEnabled());
  screenshot->trigger();
  QVERIFY(capturedFrame.has_value());
  QCOMPARE(capturedFrame->frameNumber, 77U);
  QCOMPARE(capturedPixels, std::vector<std::uint16_t>(
    expectedPixels.begin(), expectedPixels.end()));
  QVERIFY(!screenshot->isEnabled());
  QVERIFY(window.statusBar()->currentMessage().contains(
    QStringLiteral("Saving")));
  window.showScreenshotSaved(root / "saved.png");
  QVERIFY(screenshot->isEnabled());
  QVERIFY(window.statusBar()->currentMessage().contains(
    QStringLiteral("saved.png")));

  std::vector<genplusgx::settings::ScreenshotSettings> updates;
  bool rejectSettings = false;
  window.setScreenshotSettingsSink(
    [&updates, &rejectSettings](const auto& settings) {
      if (rejectSettings) {
        return genplusgx::PersistenceStatus{
          .error = genplusgx::PersistenceError::fileWriteFailed,
          .message = "Injected settings write failure.",
        };
      }
      updates.push_back(settings);
      return genplusgx::PersistenceStatus{};
    });
  auto* settingsAction = window.findChild<QAction*>(
    QStringLiteral("screenshotSettingsAction"));
  QVERIFY(settingsAction != nullptr && settingsAction->isEnabled());
  settingsAction->trigger();
  QApplication::processEvents();
  auto* settingsDialog = window.findChild<
    genplusgx::ui::ScreenshotSettingsDialog*>(
      QStringLiteral("screenshotSettingsDialog"));
  QVERIFY(settingsDialog != nullptr && settingsDialog->isVisible());
  settingsDialog->findChild<QPushButton*>(
    QStringLiteral("browseScreenshotDirectoryButton"))->click();
  QCOMPARE(dialogs->chooseDirectoryCount, 1);
  QCOMPARE(dialogs->directoryInitialDirectory, initialDirectory);
  QCOMPARE(settingsDialog->findChild<QLineEdit*>(
    QStringLiteral("screenshotDirectoryEdit"))->text(),
    genplusgx::ui::pathToQString(selectedDirectory));
  settingsDialog->findChild<QPushButton*>(
    QStringLiteral("applyScreenshotSettingsButton"))->click();
  QCOMPARE(updates.size(), std::size_t{1});
  QCOMPARE(window.screenshotSettings().directory, selectedDirectory);

  dialogs->directorySelection = root / "cancelled-screenshots";
  settingsDialog->findChild<QPushButton*>(
    QStringLiteral("browseScreenshotDirectoryButton"))->click();
  settingsDialog->findChild<QPushButton*>(
    QStringLiteral("cancelScreenshotSettingsButton"))->click();
  QApplication::processEvents();
  QCOMPARE(window.screenshotSettings().directory, selectedDirectory);
  QCOMPARE(updates.size(), std::size_t{1});

  settingsAction->trigger();
  QApplication::processEvents();
  settingsDialog = window.findChild<
    genplusgx::ui::ScreenshotSettingsDialog*>(
      QStringLiteral("screenshotSettingsDialog"));
  QVERIFY(settingsDialog != nullptr);
  settingsDialog->findChild<QPushButton*>(
    QStringLiteral("restoreScreenshotDefaultsButton"))->click();
  settingsDialog->findChild<QPushButton*>(
    QStringLiteral("okScreenshotSettingsButton"))->click();
  QApplication::processEvents();
  QCOMPARE(window.screenshotSettings().directory, defaultDirectory);
  QCOMPARE(updates.size(), std::size_t{2});

  rejectSettings = true;
  dialogs->directorySelection = root / "rejected-screenshots";
  settingsAction->trigger();
  QApplication::processEvents();
  settingsDialog = window.findChild<
    genplusgx::ui::ScreenshotSettingsDialog*>(
      QStringLiteral("screenshotSettingsDialog"));
  settingsDialog->findChild<QPushButton*>(
    QStringLiteral("browseScreenshotDirectoryButton"))->click();
  settingsDialog->findChild<QPushButton*>(
    QStringLiteral("applyScreenshotSettingsButton"))->click();
  QCOMPARE(dialogs->errors.size(), std::size_t{1});
  QVERIFY(dialogs->errors.front().contains(QStringLiteral("write failure")));
  QCOMPARE(window.screenshotSettings().directory, defaultDirectory);
  QVERIFY(settingsDialog->isVisible());
  settingsDialog->close();

  bool loadRequested = false;
  window.setGameLoadSink([&loadRequested](const auto&) { loadRequested = true; });
  QVERIFY(window.requestGameLoad(game.path()));
  QVERIFY(loadRequested);
  QVERIFY(!window.displayWidget()->hasFrame());
  QVERIFY(!screenshot->isEnabled());
}

void MainWindowTest::saveStateActionsExposeSlotSemantics()
{
  genplusgx::ui::MainWindow window;
  auto dialogs = std::make_shared<FakeDialogService>();
  window.setDialogService(dialogs);
  window.setGameLoaded(std::filesystem::path{"fixture.md"});
  auto* save = window.findChild<QAction*>(QStringLiteral("saveStateAction"));
  auto* load = window.findChild<QAction*>(QStringLiteral("loadStateAction"));
  auto* remove = window.findChild<QAction*>(QStringLiteral("deleteStateAction"));
  auto* menu = window.findChild<QMenu*>(QStringLiteral("stateSlotMenu"));
  QVERIFY(save != nullptr && load != nullptr && remove != nullptr && menu != nullptr);
  QVERIFY(!save->isEnabled());
  QVERIFY(!load->isEnabled());
  QVERIFY(!menu->isEnabled());

  std::array<genplusgx::ui::StateSlotView, 10> views{};
  for (std::uint32_t slot = 0U; slot < views.size(); ++slot) {
    views[slot].slot = slot;
  }
  views[0].state = genplusgx::ui::StateSlotViewState::available;
  views[0].timestamp = std::chrono::system_clock::time_point{
    std::chrono::milliseconds{1'700'000'000'000LL}};
  views[0].emulatedFrameNumber = 42U;
  views[1].state = genplusgx::ui::StateSlotViewState::invalid;
  views[1].detail = "Checksum mismatch";
  window.setStateSlotViews(views);

  std::vector<std::tuple<genplusgx::ui::StateUiOperation, std::uint32_t>> requests;
  window.setStateOperationSink([&requests](auto operation, auto slot) {
    requests.emplace_back(operation, slot);
  });
  window.setStateSessionReady(true);
  QVERIFY(menu->isEnabled());
  QVERIFY(save->isEnabled());
  QVERIFY(load->isEnabled());
  QVERIFY(remove->isEnabled());
  QVERIFY(window.findChild<QAction*>(QStringLiteral("stateSlotAction0"))->text()
    .contains(QStringLiteral("2023")));

  window.findChild<QAction*>(QStringLiteral("stateSlotAction1"))->trigger();
  QCOMPARE(window.selectedStateSlot(), 1U);
  QVERIFY(!load->isEnabled());
  QVERIFY(remove->isEnabled());
  QVERIFY(window.findChild<QAction*>(QStringLiteral("stateSlotAction1"))->text()
    .contains(QStringLiteral("Invalid")));

  window.findChild<QAction*>(QStringLiteral("nextStateSlotAction"))->trigger();
  QCOMPARE(window.selectedStateSlot(), 2U);
  QVERIFY(save->isEnabled());
  QVERIFY(!load->isEnabled());
  QVERIFY(!remove->isEnabled());
  window.findChild<QAction*>(QStringLiteral("previousStateSlotAction"))->trigger();
  QCOMPARE(window.selectedStateSlot(), 1U);
  window.setSelectedStateSlot(0U);

  save->trigger();
  QCOMPARE(requests.size(), std::size_t{1});
  QCOMPARE(std::get<0>(requests.back()), genplusgx::ui::StateUiOperation::save);
  QCOMPARE(std::get<1>(requests.back()), 0U);
  QVERIFY(!save->isEnabled());
  QVERIFY(!window.findChild<QAction*>(QStringLiteral("openGameAction"))->isEnabled());
  QVERIFY(!window.findChild<QAction*>(QStringLiteral("closeGameAction"))->isEnabled());
  QVERIFY(window.findChild<QLabel*>(QStringLiteral("stateSlotStatusLabel"))->text()
    .contains(QStringLiteral("Working")));

  window.setStateOperationBusy(false);
  QVERIFY(window.findChild<QAction*>(QStringLiteral("openGameAction"))->isEnabled());
  QVERIFY(window.findChild<QAction*>(QStringLiteral("closeGameAction"))->isEnabled());
  load->trigger();
  QCOMPARE(std::get<0>(requests.back()), genplusgx::ui::StateUiOperation::load);
  window.setStateOperationBusy(false);
  remove->trigger();
  QCOMPARE(std::get<0>(requests.back()), genplusgx::ui::StateUiOperation::remove);
  window.setStateOperationBusy(false);

  window.showStateOperationSuccess(genplusgx::ui::StateUiOperation::save, 0U);
  QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("saved")));
  window.showStateOperationError(
    genplusgx::ui::StateUiOperation::load, "Injected corrupt state.");
  QCOMPARE(dialogs->errors.size(), std::size_t{1});
  QVERIFY(dialogs->errors.front().contains(QStringLiteral("Load State Failed")));
  QVERIFY(dialogs->errors.front().contains(QStringLiteral("corrupt")));

  window.setNoGameLoaded();
  QVERIFY(!menu->isEnabled());
  QVERIFY(!save->isEnabled());
  QVERIFY(!load->isEnabled());
}

void MainWindowTest::segaCdDiscActionsAreTypedAndRecoverable()
{
  genplusgx::test::TemporaryFixture game{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  genplusgx::test::TemporaryFixture disc{
    genplusgx::test::makeSegaCdDiscImage(), ".iso"};
  auto dialogs = std::make_shared<FakeDialogService>();
  dialogs->discSelection = disc.path();
  genplusgx::ui::MainWindow window;
  window.setDialogService(dialogs);
  window.setGameLoaded(game.path());

  auto* change = window.findChild<QAction*>(QStringLiteral("changeDiscAction"));
  auto* eject = window.findChild<QAction*>(QStringLiteral("ejectDiscAction"));
  QVERIFY(change != nullptr && eject != nullptr);
  QVERIFY(eject->isCheckable());
  QVERIFY(!change->isEnabled() && !eject->isEnabled());

  using Request = std::tuple<
    genplusgx::ui::DiscUiOperation, std::filesystem::path, bool>;
  std::vector<Request> requests;
  window.setDiscOperationSink(
    [&requests](auto operation, const auto& path, bool ejected) {
      requests.emplace_back(operation, path, ejected);
    });
  window.setSegaCdSession(true, "USA", disc.path(), false, true);
  QVERIFY(change->isEnabled() && eject->isEnabled());
  QVERIFY(change->toolTip().contains(
    genplusgx::ui::pathToQString(disc.path())));
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("systemStatusLabel"))->text(),
    QStringLiteral("System: Sega CD / Mega CD"));
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("regionStatusLabel"))->text(),
    QStringLiteral("Region: USA"));

  change->trigger();
  QCOMPARE(dialogs->chooseDiscCount, 1);
  QCOMPARE(dialogs->discInitialDirectory, disc.path().parent_path());
  QCOMPARE(requests.size(), std::size_t{1});
  QCOMPARE(std::get<0>(requests.back()), genplusgx::ui::DiscUiOperation::change);
  QCOMPARE(std::get<1>(requests.back()), disc.path());
  QVERIFY(!change->isEnabled() && !eject->isEnabled());

  window.setSegaCdSession(true, "USA", disc.path(), false, true);
  eject->trigger();
  QCOMPARE(requests.size(), std::size_t{2});
  QCOMPARE(std::get<0>(requests.back()),
    genplusgx::ui::DiscUiOperation::setEjected);
  QVERIFY(std::get<2>(requests.back()));
  window.setSegaCdSession(true, "USA", disc.path(), true, true);
  QVERIFY(eject->isChecked());
  QVERIFY(eject->text().contains(QStringLiteral("Close Disc")));
  window.showDiscOperationSuccess(genplusgx::ui::DiscUiOperation::setEjected);
  QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("opened")));

  eject->trigger();
  QCOMPARE(requests.size(), std::size_t{3});
  QVERIFY(!std::get<2>(requests.back()));
  window.setSegaCdSession(true, "USA", disc.path(), false, true);
  QVERIFY(!eject->isChecked());

  genplusgx::test::TemporaryFixture unsupported{{1U}, ".zip"};
  dialogs->discSelection = unsupported.path();
  change->trigger();
  QCOMPARE(requests.size(), std::size_t{3});
  QCOMPARE(dialogs->errors.size(), std::size_t{1});
  QVERIFY(dialogs->errors.front().contains(QStringLiteral("Unable to Change Disc")));

  window.setSegaCdSession(false, "stale", disc.path(), true, true);
  QVERIFY(!change->isEnabled() && !eject->isEnabled());
  QVERIFY(!eject->isChecked());
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("systemStatusLabel"))->text(),
    QStringLiteral("System: —"));
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("regionStatusLabel"))->text(),
    QStringLiteral("Region: —"));
}

void MainWindowTest::gameInformationWorkflowIsAsynchronousAndInspectable()
{
  genplusgx::test::TemporaryFixture game{
    genplusgx::test::makeGenesisSramWriterRom(), ".md"};
  auto dialogs = std::make_shared<FakeDialogService>();
  genplusgx::ui::MainWindow window;
  window.setDialogService(dialogs);
  window.setGameLoaded(game.path());

  std::optional<std::filesystem::path> requestedPath;
  window.setGameInformationRequestSink(
    [&requestedPath](const std::filesystem::path& path) {
      requestedPath = path;
    });
  auto* action = window.findChild<QAction*>(
    QStringLiteral("gameInformationAction"));
  QVERIFY(action != nullptr);
  QVERIFY(action->isEnabled());
  action->trigger();
  QCOMPARE(requestedPath, std::optional{game.path()});
  QVERIFY(!action->isEnabled());
  QVERIFY(window.statusBar()->currentMessage().contains(
    QStringLiteral("Reading")));

  genplusgx::library::GameMetadata metadata;
  metadata.path = game.path();
  metadata.fileSize = 65'536U;
  metadata.system = genplusgx::library::GameSystem::genesis;
  metadata.format = "Genesis / Mega Drive ROM";
  metadata.domesticTitle = "GENPLUSGX CORE TEST";
  metadata.internationalTitle = "GENPLUSGX SYNTHETIC SRAM WRITER";
  metadata.productCode = "TEST-000001";
  metadata.region = "Americas";
  metadata.romType = "GM";
  metadata.peripheralSupport = "J";
  metadata.mapper = "Cartridge SRAM (2097152-2162687)";
  metadata.sha256 = std::string(64U, 'a');
  metadata.notes = "Generated CC0 test fixture.";
  metadata.headerChecksum = 0x1234U;
  metadata.computedChecksum = 0x1234U;
  metadata.declaredRomSize = 65'536U;
  metadata.headerRecognized = true;
  window.showGameInformation(metadata);
  QApplication::processEvents();

  auto* information = window.findChild<genplusgx::ui::GameInformationDialog*>(
    QStringLiteral("gameInformationDialog"));
  QVERIFY(information != nullptr);
  QVERIFY(information->isVisible());
  QVERIFY(information->isModal());
  QVERIFY(action->isEnabled());
  const auto field = [information](const char* name) {
    return information->findChild<QLineEdit*>(QString::fromLatin1(name));
  };
  QCOMPARE(field("gameInfoTitleValue")->text(),
    QStringLiteral("GENPLUSGX SYNTHETIC SRAM WRITER"));
  QCOMPARE(field("gameInfoDomesticTitleValue")->text(),
    QStringLiteral("GENPLUSGX CORE TEST"));
  QCOMPARE(field("gameInfoSystemValue")->text(),
    QStringLiteral("Sega Genesis / Mega Drive"));
  QCOMPARE(field("gameInfoRegionValue")->text(), QStringLiteral("Americas"));
  QVERIFY(field("gameInfoChecksumValue")->text().contains(
    QStringLiteral("0x1234")));
  QCOMPARE(field("gameInfoSha256Value")->text(), QString(64, QLatin1Char{'a'}));
  QCOMPARE(field("gameInfoFilePathValue")->text(),
    genplusgx::ui::pathToQString(game.path()));
  QVERIFY(!field("gameInfoSystemValue")->accessibleName().isEmpty());
  auto* close = information->findChild<QPushButton*>(
    QStringLiteral("closeGameInformationButton"));
  QVERIFY(close != nullptr);
  QTest::mouseClick(close, Qt::LeftButton);
  QApplication::processEvents();

  action->trigger();
  QVERIFY(!action->isEnabled());
  window.showGameInformationError("Injected malformed header.");
  QCOMPARE(dialogs->errors.size(), std::size_t{1});
  QVERIFY(dialogs->errors.front().contains(
    QStringLiteral("Game Information Failed")));
  QVERIFY(dialogs->errors.front().contains(QStringLiteral("malformed")));
  QVERIFY(action->isEnabled());

  window.setNoGameLoaded();
  QVERIFY(!action->isEnabled());
}

} // namespace

QTEST_MAIN(MainWindowTest)

#include "main_window_test.moc"
