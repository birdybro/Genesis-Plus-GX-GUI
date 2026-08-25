#include "genplusgx/ui/about_dialog.h"
#include "genplusgx/ui/audio_settings_dialog.h"
#include "genplusgx/ui/bios_settings_dialog.h"
#include "genplusgx/ui/dialog_service.h"
#include "genplusgx/ui/main_window.h"
#include "genplusgx/ui/system_settings_dialog.h"
#include "genplusgx/ui/video_settings_dialog.h"
#include "genplusgx/version.h"
#include "genplusgx/video/display_widget.h"

#include "synthetic_rom.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTest>
#include <QTemporaryDir>

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
  std::filesystem::path discInitialDirectory;
  int chooseDiscCount{0};

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
  void aboutDialogReportsBuildIdentity();
  void exitActionClosesWindow();
  void videoActionsDriveDisplayPolicy();
  void videoSettingsDialogAppliesCancelsAndRestores();
  void audioSettingsWorkflowAppliesCancelsAndRestores();
  void systemSettingsWorkflowIsValidatedAndDeferred();
  void biosSettingsValidatePersistAndCancel();
  void saveStateActionsExposeSlotSemantics();
  void segaCdDiscActionsAreTypedAndRecoverable();
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
}

void MainWindowTest::menusAndActionsHaveStableSemantics()
{
  genplusgx::ui::MainWindow window;
  const char* menuNames[]{
    "fileMenu", "emulationMenu", "videoMenu", "audioMenu", "inputMenu",
    "toolsMenu", "helpMenu", "openRecentMenu", "stateSlotMenu", "videoScaleMenu",
    "aspectRatioMenu", "filteringMenu", "overscanMenu", "ntscFilterMenu",
    "interlacedRenderMenu"};
  for (const auto* name : menuNames) {
    QVERIFY2(window.findChild<QMenu*>(QString::fromLatin1(name)) != nullptr, name);
  }

  const char* enabledNames[]{
    "openGameAction", "gameLibraryAction", "exitAction", "fullscreenAction",
    "muteAction", "volumeUpAction", "volumeDownAction",
    "controllerConfigurationAction", "playerAssignmentsAction", "settingsAction",
    "systemSettingsAction", "biosSettingsAction",
    "diagnosticsAction", "userGuideAction", "keyboardShortcutsAction", "aboutAction",
    "aboutQtAction"};
  for (const auto* name : enabledNames) {
    auto* action = window.findChild<QAction*>(QString::fromLatin1(name));
    QVERIFY2(action != nullptr, name);
    QVERIFY2(action->isEnabled(), name);
  }

  const char* gameOnlyNames[]{
    "closeGameAction", "screenshotAction", "pauseAction", "resetAction",
    "softResetAction", "fastForwardAction", "frameAdvanceAction", "saveStateAction",
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
  });
  window.show();

  window.findChild<QAction*>(QStringLiteral("settingsAction"))->trigger();
  QApplication::processEvents();
  auto* dialog = window.findChild<genplusgx::ui::VideoSettingsDialog*>(
    QStringLiteral("videoSettingsDialog"));
  QVERIFY(dialog != nullptr);
  QVERIFY(dialog->isVisible());
  auto* aspect = dialog->findChild<QComboBox*>(QStringLiteral("videoAspectCombo"));
  auto* scaling = dialog->findChild<QComboBox*>(QStringLiteral("videoScalingCombo"));
  auto* filter = dialog->findChild<QComboBox*>(
    QStringLiteral("videoPresentationFilterCombo"));
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
  QVERIFY(overscan != nullptr && ntsc != nullptr && render != nullptr);
  QVERIFY(gameGear != nullptr && apply != nullptr && restore != nullptr);
  QCOMPARE(aspect->currentData().toInt(),
    static_cast<int>(genplusgx::video::AspectMode::fourThree));
  QCOMPARE(overscan->currentData().toInt(),
    static_cast<int>(genplusgx::CoreOverscanMode::vertical));

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

void MainWindowTest::audioSettingsWorkflowAppliesCancelsAndRestores()
{
  genplusgx::ui::MainWindow window;
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
  auto* volume = dialog->findChild<QSpinBox*>(
    QStringLiteral("masterVolumeSpinBox"));
  auto* filter = dialog->findChild<QComboBox*>(
    QStringLiteral("coreAudioFilterCombo"));
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
  QVERIFY(filter != nullptr && lowPass != nullptr && eqLow != nullptr);
  QVERIFY(psg != nullptr && apply != nullptr && restore != nullptr);
  QCOMPARE(device->currentData().toString(),
    QStringLiteral("Configured test output"));
  QCOMPARE(latency->value(), 45);
  QCOMPARE(psg->value(), 125);

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

} // namespace

QTEST_MAIN(MainWindowTest)

#include "main_window_test.moc"
