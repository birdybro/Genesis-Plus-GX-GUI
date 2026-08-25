#include "genplusgx/ui/main_window.h"

#include "genplusgx/game_file.h"
#include "genplusgx/ui/about_dialog.h"
#include "genplusgx/ui/appearance_settings_dialog.h"
#include "genplusgx/ui/audio_settings_dialog.h"
#include "genplusgx/ui/bios_settings_dialog.h"
#include "genplusgx/ui/cheat_manager_dialog.h"
#include "genplusgx/ui/dialog_service.h"
#include "genplusgx/ui/diagnostics_dialog.h"
#include "genplusgx/ui/game_information_dialog.h"
#include "genplusgx/ui/game_library_dialog.h"
#include "genplusgx/ui/help_dialog.h"
#include "genplusgx/ui/input_configuration_dialog.h"
#include "genplusgx/ui/per_game_settings_dialog.h"
#include "genplusgx/ui/screenshot_settings_dialog.h"
#include "genplusgx/ui/system_settings_dialog.h"
#include "genplusgx/ui/video_settings_dialog.h"
#include "genplusgx/version.h"
#include "genplusgx/video/display_widget.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDateTime>
#include <QDropEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <chrono>

namespace genplusgx::ui {
namespace {

QLabel* statusLabel(
  QStatusBar& bar,
  const QString& text,
  const char* objectName,
  int stretch = 0,
  bool permanent = false)
{
  auto* label = new QLabel(text, &bar);
  label->setObjectName(QString::fromLatin1(objectName));
  label->setMargin(3);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  if (permanent) {
    bar.addPermanentWidget(label, stretch);
  } else {
    bar.addWidget(label, stretch);
  }
  return label;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
  : QMainWindow(parent), dialogService_(std::make_shared<QtDialogService>())
{
  setObjectName(QStringLiteral("mainWindow"));
  setWindowTitle(QStringLiteral("%1 %2").arg(
    QString::fromLatin1(GENPLUSGX_APP_NAME), QString::fromLatin1(GENPLUSGX_VERSION)));
  setMinimumSize(800, 600);
  resize(1080, 720);
  setAcceptDrops(true);

  createCanvas();
  buildMenus();
  buildStatusBar();
  for (std::uint32_t slot = 0U; slot < stateSlotViews_.size(); ++slot) {
    stateSlotViews_[slot].slot = slot;
  }
  updateStateSlotPresentation();
  setGameActionsEnabled(false);
  applyVideoSettings(videoSettings_, false);
  applyAudioSettings(audioSettings_, false);
}

QAction* MainWindow::addAction(
  QMenu& menu,
  const QString& text,
  const char* objectName,
  const QKeySequence& shortcut)
{
  auto* action = menu.addAction(text);
  action->setObjectName(QString::fromLatin1(objectName));
  if (!shortcut.isEmpty()) {
    action->setShortcut(shortcut);
  }
  return action;
}

void MainWindow::createCanvas()
{
  displayWidget_ = new video::DisplayWidget(this);
  setCentralWidget(displayWidget_);
}

void MainWindow::buildMenus()
{
  auto createMenu = [this](const QString& title, const char* objectName) {
    auto* menu = menuBar()->addMenu(title);
    menu->setObjectName(QString::fromLatin1(objectName));
    return menu;
  };

  auto* file = createMenu(tr("&File"), "fileMenu");
  auto* openGame = addAction(
    *file, tr("&Open Game…"), "openGameAction", QKeySequence::Open);
  connect(openGame, &QAction::triggered, this, &MainWindow::chooseGame);
  auto* recent = file->addMenu(tr("Open &Recent"));
  recent->setObjectName(QStringLiteral("openRecentMenu"));
  recent->setEnabled(false);
  auto* closeGameAction = addAction(
    *file, tr("&Close Game"), "closeGameAction", QKeySequence::Close);
  connect(closeGameAction, &QAction::triggered, this, &MainWindow::closeGame);
  auto* gameLibrary = addAction(
    *file, tr("Game &Library…"), "gameLibraryAction", QKeySequence{tr("Ctrl+L")});
  connect(gameLibrary, &QAction::triggered, this, &MainWindow::showGameLibrary);
  file->addSeparator();
  auto* screenshot = addAction(
    *file, tr("&Screenshot"), "screenshotAction", QKeySequence{tr("F12")});
  connect(screenshot, &QAction::triggered, this, &MainWindow::requestScreenshot);
  auto* screenshotSettings = addAction(
    *file, tr("Screenshot &Settings…"), "screenshotSettingsAction");
  connect(screenshotSettings, &QAction::triggered,
    this, &MainWindow::showScreenshotSettings);
  file->addSeparator();
  auto* exit = addAction(*file, tr("E&xit"), "exitAction", QKeySequence::Quit);
  connect(exit, &QAction::triggered, this, &QWidget::close);

  auto* emulation = createMenu(tr("&Emulation"), "emulationMenu");
  auto* pause = addAction(*emulation, tr("&Pause"), "pauseAction", QKeySequence{tr("Space")});
  pause->setCheckable(true);
  connect(pause, &QAction::toggled, this, [this](bool paused) {
    requestEmulationControl(
      paused ? EmulationUiOperation::pause : EmulationUiOperation::resume);
  });
  auto* reset = addAction(
    *emulation, tr("&Reset"), "resetAction", QKeySequence{tr("Ctrl+R")});
  connect(reset, &QAction::triggered, this, [this] {
    requestEmulationControl(EmulationUiOperation::hardReset);
  });
  auto* softReset = addAction(
    *emulation, tr("&Soft Reset"), "softResetAction");
  connect(softReset, &QAction::triggered, this, [this] {
    requestEmulationControl(EmulationUiOperation::softReset);
  });
  auto* fastForward = addAction(
    *emulation, tr("&Fast Forward"), "fastForwardAction", QKeySequence{tr("Tab")});
  fastForward->setCheckable(true);
  connect(fastForward, &QAction::toggled, this, [this](bool enabled) {
    requestEmulationControl(EmulationUiOperation::setFastForward, enabled);
  });
  auto* frameAdvance = addAction(
    *emulation, tr("Frame &Advance"), "frameAdvanceAction", QKeySequence{tr("N")});
  connect(frameAdvance, &QAction::triggered, this, [this] {
    requestEmulationControl(EmulationUiOperation::frameAdvance);
  });
  emulation->addSeparator();
  auto* saveState = addAction(
    *emulation, tr("&Save State"), "saveStateAction", QKeySequence{tr("F5")});
  auto* loadState = addAction(
    *emulation, tr("&Load State"), "loadStateAction", QKeySequence{tr("F8")});
  connect(saveState, &QAction::triggered, this, [this] {
    requestStateOperation(StateUiOperation::save);
  });
  connect(loadState, &QAction::triggered, this, [this] {
    requestStateOperation(StateUiOperation::load);
  });
  auto* stateSlotMenu = emulation->addMenu(tr("State &Slot"));
  stateSlotMenu->setObjectName(QStringLiteral("stateSlotMenu"));
  auto* slotGroup = new QActionGroup(this);
  slotGroup->setObjectName(QStringLiteral("stateSlotActionGroup"));
  for (std::uint32_t slot = 0U; slot <= 9U; ++slot) {
    auto* slotAction = stateSlotMenu->addAction(tr("Slot %1 — Empty").arg(slot));
    slotAction->setObjectName(QStringLiteral("stateSlotAction%1").arg(slot));
    slotAction->setShortcut(QKeySequence{tr("Ctrl+%1").arg(slot)});
    slotAction->setCheckable(true);
    slotAction->setData(slot);
    slotGroup->addAction(slotAction);
    connect(slotAction, &QAction::triggered, this, [this, slot] {
      setSelectedStateSlot(slot);
    });
  }
  stateSlotMenu->addSeparator();
  auto* previousSlot = addAction(
    *stateSlotMenu,
    tr("&Previous Slot"),
    "previousStateSlotAction",
    QKeySequence{tr("Ctrl+[")});
  auto* nextSlot = addAction(
    *stateSlotMenu,
    tr("&Next Slot"),
    "nextStateSlotAction",
    QKeySequence{tr("Ctrl+]")});
  connect(previousSlot, &QAction::triggered, this, [this] {
    setSelectedStateSlot((selectedStateSlot_ + 9U) % 10U);
  });
  connect(nextSlot, &QAction::triggered, this, [this] {
    setSelectedStateSlot((selectedStateSlot_ + 1U) % 10U);
  });
  stateSlotMenu->addSeparator();
  auto* deleteState = addAction(
    *stateSlotMenu,
    tr("&Delete Selected State"),
    "deleteStateAction",
    QKeySequence{tr("Ctrl+Delete")});
  connect(deleteState, &QAction::triggered, this, [this] {
    requestStateOperation(StateUiOperation::remove);
  });
  auto* changeDisc = addAction(
    *emulation, tr("Change &Disc…"), "changeDiscAction");
  connect(changeDisc, &QAction::triggered, this, &MainWindow::chooseDisc);
  auto* ejectDisc = addAction(
    *emulation, tr("&Eject Disc"), "ejectDiscAction");
  ejectDisc->setCheckable(true);
  connect(ejectDisc, &QAction::toggled,
    this, &MainWindow::requestDiscEjected);

  auto* video = createMenu(tr("&Video"), "videoMenu");
  auto* fullscreen = addAction(
    *video, tr("&Fullscreen"), "fullscreenAction", QKeySequence{tr("Alt+Return")});
  fullscreen->setCheckable(true);
  connect(fullscreen, &QAction::toggled, this, [this](bool enabled) {
    if (enabled) {
      showFullScreen();
    } else {
      showNormal();
    }
  });
  auto* scale = video->addMenu(tr("&Scale"));
  scale->setObjectName(QStringLiteral("videoScaleMenu"));
  auto* scaleGroup = new QActionGroup(this);
  scaleGroup->setObjectName(QStringLiteral("videoScaleActionGroup"));
  auto* fitScale = addAction(*scale, tr("&Fit to Window"), "fitScaleAction");
  auto* integerScale = addAction(*scale, tr("&Integer Scale"), "integerScaleAction");
  scaleGroup->addAction(fitScale);
  scaleGroup->addAction(integerScale);
  fitScale->setCheckable(true);
  integerScale->setCheckable(true);
  fitScale->setChecked(true);
  connect(fitScale, &QAction::triggered, this, [this] {
    auto settings = videoSettings_;
    settings.scaling = video::ScaleMode::fit;
    applyVideoSettings(settings, true);
  });
  connect(integerScale, &QAction::triggered, this, [this] {
    auto settings = videoSettings_;
    settings.scaling = video::ScaleMode::integer;
    applyVideoSettings(settings, true);
  });

  auto* aspect = video->addMenu(tr("&Aspect Ratio"));
  aspect->setObjectName(QStringLiteral("aspectRatioMenu"));
  auto* aspectGroup = new QActionGroup(this);
  aspectGroup->setObjectName(QStringLiteral("aspectRatioActionGroup"));
  auto* nativeAspect = addAction(*aspect, tr("&Native Pixels"), "nativeAspectAction");
  auto* fourThreeAspect = addAction(*aspect, tr("Force &4:3"), "fourThreeAspectAction");
  auto* stretchAspect = addAction(*aspect, tr("&Stretch to Window"), "stretchAspectAction");
  aspectGroup->addAction(nativeAspect);
  aspectGroup->addAction(fourThreeAspect);
  aspectGroup->addAction(stretchAspect);
  nativeAspect->setCheckable(true);
  fourThreeAspect->setCheckable(true);
  stretchAspect->setCheckable(true);
  nativeAspect->setChecked(true);
  connect(nativeAspect, &QAction::triggered, this, [this] {
    auto settings = videoSettings_;
    settings.aspect = video::AspectMode::native;
    applyVideoSettings(settings, true);
  });
  connect(fourThreeAspect, &QAction::triggered, this, [this] {
    auto settings = videoSettings_;
    settings.aspect = video::AspectMode::fourThree;
    applyVideoSettings(settings, true);
  });
  connect(stretchAspect, &QAction::triggered, this, [this] {
    auto settings = videoSettings_;
    settings.aspect = video::AspectMode::stretch;
    applyVideoSettings(settings, true);
  });

  auto* filtering = video->addMenu(tr("&Filtering"));
  filtering->setObjectName(QStringLiteral("filteringMenu"));
  auto* filterGroup = new QActionGroup(this);
  filterGroup->setObjectName(QStringLiteral("filterActionGroup"));
  auto* nearest = addAction(*filtering, tr("&Nearest Neighbor"), "nearestFilterAction");
  auto* bilinear = addAction(*filtering, tr("&Bilinear"), "bilinearFilterAction");
  filterGroup->addAction(nearest);
  filterGroup->addAction(bilinear);
  nearest->setCheckable(true);
  bilinear->setCheckable(true);
  nearest->setChecked(true);
  connect(nearest, &QAction::triggered, this, [this] {
    auto settings = videoSettings_;
    settings.presentationFilter = video::VideoFilter::nearest;
    applyVideoSettings(settings, true);
  });
  connect(bilinear, &QAction::triggered, this, [this] {
    auto settings = videoSettings_;
    settings.presentationFilter = video::VideoFilter::bilinear;
    applyVideoSettings(settings, true);
  });
  auto* overscan = video->addMenu(tr("&Overscan"));
  overscan->setObjectName(QStringLiteral("overscanMenu"));
  auto* overscanGroup = new QActionGroup(this);
  overscanGroup->setObjectName(QStringLiteral("overscanActionGroup"));
  const std::array overscanChoices{
    std::pair{tr("&Disabled"), CoreOverscanMode::disabled},
    std::pair{tr("&Top and Bottom"), CoreOverscanMode::vertical},
    std::pair{tr("&Left and Right"), CoreOverscanMode::horizontal},
    std::pair{tr("&All Borders"), CoreOverscanMode::full},
  };
  const std::array overscanNames{
    "overscanDisabledAction", "overscanVerticalAction",
    "overscanHorizontalAction", "overscanFullAction"};
  for (std::size_t index = 0U; index < overscanChoices.size(); ++index) {
    const auto [label, mode] = overscanChoices[index];
    auto* action = addAction(*overscan, label, overscanNames[index]);
    action->setCheckable(true);
    action->setData(static_cast<int>(mode));
    overscanGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, mode] {
      auto settings = videoSettings_;
      settings.core.overscan = mode;
      applyVideoSettings(settings, true);
    });
  }

  auto* ntsc = video->addMenu(tr("&NTSC Filter"));
  ntsc->setObjectName(QStringLiteral("ntscFilterMenu"));
  auto* ntscGroup = new QActionGroup(this);
  ntscGroup->setObjectName(QStringLiteral("ntscFilterActionGroup"));
  const std::array ntscChoices{
    std::pair{tr("&Disabled"), CoreNtscFilter::disabled},
    std::pair{tr("&Monochrome"), CoreNtscFilter::monochrome},
    std::pair{tr("&Composite"), CoreNtscFilter::composite},
    std::pair{tr("&S-Video"), CoreNtscFilter::sVideo},
    std::pair{tr("&RGB"), CoreNtscFilter::rgb},
  };
  const std::array ntscNames{
    "ntscDisabledAction", "ntscMonochromeAction", "ntscCompositeAction",
    "ntscSVideoAction", "ntscRgbAction"};
  for (std::size_t index = 0U; index < ntscChoices.size(); ++index) {
    const auto [label, mode] = ntscChoices[index];
    auto* action = addAction(*ntsc, label, ntscNames[index]);
    action->setCheckable(true);
    action->setData(static_cast<int>(mode));
    ntscGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, mode] {
      auto settings = videoSettings_;
      settings.core.ntscFilter = mode;
      applyVideoSettings(settings, true);
    });
  }

  auto* interlaced = video->addMenu(tr("&Interlaced Output"));
  interlaced->setObjectName(QStringLiteral("interlacedRenderMenu"));
  auto* interlacedGroup = new QActionGroup(this);
  interlacedGroup->setObjectName(QStringLiteral("interlacedRenderActionGroup"));
  auto* singleField = addAction(
    *interlaced, tr("&Single Field"), "singleFieldRenderAction");
  auto* doubleField = addAction(
    *interlaced, tr("&Double Field"), "doubleFieldRenderAction");
  for (auto* action : {singleField, doubleField}) {
    action->setCheckable(true);
    interlacedGroup->addAction(action);
  }
  connect(singleField, &QAction::triggered, this, [this] {
    auto settings = videoSettings_;
    settings.core.interlacedRender = CoreInterlacedRenderMode::singleField;
    applyVideoSettings(settings, true);
  });
  connect(doubleField, &QAction::triggered, this, [this] {
    auto settings = videoSettings_;
    settings.core.interlacedRender = CoreInterlacedRenderMode::doubleField;
    applyVideoSettings(settings, true);
  });
  auto* gameGearExtended = addAction(
    *video, tr("Extended &Game Gear Screen"), "gameGearExtendedScreenAction");
  gameGearExtended->setCheckable(true);
  connect(gameGearExtended, &QAction::triggered, this, [this](bool checked) {
    auto settings = videoSettings_;
    settings.core.gameGearExtendedScreen = checked;
    applyVideoSettings(settings, true);
  });
  video->addSeparator();
  auto* videoSettings = addAction(
    *video, tr("Video &Settings…"), "videoSettingsAction");
  connect(videoSettings, &QAction::triggered, this, &MainWindow::showVideoSettings);

  auto* audio = createMenu(tr("&Audio"), "audioMenu");
  auto* mute = addAction(*audio, tr("&Mute"), "muteAction", QKeySequence{tr("M")});
  mute->setCheckable(true);
  connect(mute, &QAction::toggled, this, [this](bool muted) {
    auto settings = audioSettings_;
    settings.muted = muted;
    applyAudioSettings(settings, true);
  });
  auto* volumeUp = addAction(
    *audio, tr("Volume &Up"), "volumeUpAction", QKeySequence{tr("+")});
  auto* volumeDown = addAction(
    *audio, tr("Volume &Down"), "volumeDownAction", QKeySequence{tr("-")});
  connect(volumeUp, &QAction::triggered, this, [this] {
    auto settings = audioSettings_;
    settings.masterVolumePercent = std::min(
      settings.masterVolumePercent + 5, 100);
    applyAudioSettings(settings, true);
  });
  connect(volumeDown, &QAction::triggered, this, [this] {
    auto settings = audioSettings_;
    settings.masterVolumePercent = std::max(
      settings.masterVolumePercent - 5, 0);
    applyAudioSettings(settings, true);
  });
  audio->addSeparator();
  auto* audioSettings = addAction(
    *audio, tr("Audio &Settings…"), "audioSettingsAction");
  connect(audioSettings, &QAction::triggered, this, &MainWindow::showAudioSettings);

  auto* input = createMenu(tr("&Input"), "inputMenu");
  auto* controllerConfiguration = addAction(
    *input, tr("&Controller Configuration…"), "controllerConfigurationAction");
  auto* playerAssignments = addAction(
    *input, tr("&Player Assignments…"), "playerAssignmentsAction");
  connect(controllerConfiguration, &QAction::triggered, this, [this] {
    showInputConfiguration(InputConfigurationTab::bindings);
  });
  connect(playerAssignments, &QAction::triggered, this, [this] {
    showInputConfiguration(InputConfigurationTab::assignments);
  });

  auto* tools = createMenu(tr("&Tools"), "toolsMenu");
  auto* cheats = addAction(*tools, tr("&Cheats…"), "cheatsAction");
  connect(cheats, &QAction::triggered, this, &MainWindow::showCheats);
  auto* perGameSettings = addAction(
    *tools, tr("Per-&Game Settings…"), "perGameSettingsAction");
  connect(perGameSettings, &QAction::triggered,
    this, &MainWindow::showPerGameSettings);
  auto* gameInformation = addAction(
    *tools, tr("Game &Information…"), "gameInformationAction");
  connect(gameInformation, &QAction::triggered,
    this, &MainWindow::requestGameInformation);
  auto* settings = addAction(
    *tools, tr("&Settings…"), "settingsAction", QKeySequence::Preferences);
  connect(settings, &QAction::triggered,
    this, &MainWindow::showAppearanceSettings);
  auto* systemSettings = addAction(
    *tools, tr("&System Settings…"), "systemSettingsAction");
  connect(systemSettings, &QAction::triggered,
    this, &MainWindow::showSystemSettings);
  auto* biosSettings = addAction(
    *tools, tr("&BIOS Settings…"), "biosSettingsAction");
  connect(biosSettings, &QAction::triggered,
    this, &MainWindow::showBiosSettings);
  auto* diagnostics = addAction(
    *tools, tr("Log and &Diagnostics…"), "diagnosticsAction");
  connect(diagnostics, &QAction::triggered, this, &MainWindow::showDiagnostics);

  auto* help = createMenu(tr("&Help"), "helpMenu");
  auto* userGuide = addAction(
    *help, tr("&User Guide"), "userGuideAction", QKeySequence::HelpContents);
  connect(userGuide, &QAction::triggered, this, &MainWindow::showUserGuide);
  auto* shortcuts = addAction(
    *help, tr("&Keyboard Shortcuts"), "keyboardShortcutsAction");
  connect(shortcuts, &QAction::triggered,
    this, &MainWindow::showKeyboardShortcuts);
  help->addSeparator();
  auto* about = addAction(*help, tr("&About Genesis Plus GX GUI"), "aboutAction");
  connect(about, &QAction::triggered, this, &MainWindow::showAboutDialog);
  auto* aboutQt = addAction(*help, tr("About &Qt"), "aboutQtAction");
  connect(aboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);
}

void MainWindow::showUserGuide()
{
  if (auto* existing = findChild<HelpDialog*>(QStringLiteral("userGuideDialog"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new HelpDialog(HelpTopic::userGuide, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->open();
}

void MainWindow::showKeyboardShortcuts()
{
  if (auto* existing = findChild<HelpDialog*>(
        QStringLiteral("keyboardShortcutsDialog"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new HelpDialog(HelpTopic::keyboardShortcuts, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->open();
}

void MainWindow::buildStatusBar()
{
  statusBar()->setObjectName(QStringLiteral("mainStatusBar"));
  statusBar()->setSizeGripEnabled(true);
  gameStatus_ = statusLabel(*statusBar(), tr("No game loaded"), "gameStatusLabel", 1);
  systemStatus_ = statusLabel(*statusBar(), tr("System: —"), "systemStatusLabel");
  regionStatus_ = statusLabel(*statusBar(), tr("Region: —"), "regionStatusLabel");
  fpsStatus_ = statusLabel(*statusBar(), tr("0.0 FPS"), "fpsStatusLabel", 0, true);
  slotStatus_ = statusLabel(*statusBar(), tr("Slot 0"), "stateSlotStatusLabel", 0, true);
}

void MainWindow::setGameActionsEnabled(bool enabled)
{
  static constexpr const char* gameActionNames[]{
    "closeGameAction", "screenshotAction", "pauseAction", "resetAction",
    "softResetAction", "fastForwardAction", "frameAdvanceAction",
    "cheatsAction",
    "gameInformationAction"};
  for (const auto* name : gameActionNames) {
    findChild<QAction*>(QString::fromLatin1(name))->setEnabled(enabled);
  }
  updateGameInformationAction();
  updateDiscActions();
  updateStateActions();
  updateScreenshotAction();
  updateCheatAction();
  updatePerGameSettingsAction();
  updateEmulationControls();
}

void MainWindow::setEmulationControlSink(EmulationControlSink sink)
{
  emulationControlSink_ = std::move(sink);
  updateEmulationControls();
}

void MainWindow::setEmulationControlState(bool paused, bool fastForward)
{
  emulationPaused_ = isGameLoaded() && paused;
  fastForwardActive_ = isGameLoaded() && fastForward;
  updateEmulationControls();
}

void MainWindow::requestEmulationControl(
  EmulationUiOperation operation,
  bool enabled)
{
  if (!isGameLoaded() || !emulationControlSink_ ||
      !emulationControlSink_(operation, enabled)) {
    updateEmulationControls();
    statusBar()->showMessage(tr("The emulation command could not be queued."), 5'000);
    return;
  }

  if (operation == EmulationUiOperation::pause) {
    emulationPaused_ = true;
  } else if (operation == EmulationUiOperation::resume) {
    emulationPaused_ = false;
  } else if (operation == EmulationUiOperation::setFastForward) {
    fastForwardActive_ = enabled;
  }
  updateEmulationControls();
}

void MainWindow::updateEmulationControls()
{
  const bool available = isGameLoaded() && !gameLoading_ &&
                         static_cast<bool>(emulationControlSink_);
  auto* pause = findChild<QAction*>(QStringLiteral("pauseAction"));
  auto* reset = findChild<QAction*>(QStringLiteral("resetAction"));
  auto* softReset = findChild<QAction*>(QStringLiteral("softResetAction"));
  auto* fastForward = findChild<QAction*>(QStringLiteral("fastForwardAction"));
  auto* frameAdvance = findChild<QAction*>(QStringLiteral("frameAdvanceAction"));
  if (pause != nullptr) {
    const QSignalBlocker blocker{pause};
    pause->setChecked(available && emulationPaused_);
    pause->setText(available && emulationPaused_ ? tr("&Resume") : tr("&Pause"));
    pause->setEnabled(available);
  }
  if (reset != nullptr) {
    reset->setEnabled(available);
  }
  if (softReset != nullptr) {
    softReset->setEnabled(available);
  }
  if (fastForward != nullptr) {
    const QSignalBlocker blocker{fastForward};
    fastForward->setChecked(available && fastForwardActive_);
    fastForward->setEnabled(available);
  }
  if (frameAdvance != nullptr) {
    frameAdvance->setEnabled(available && emulationPaused_);
  }
}

void MainWindow::setCheatSession(
  cheats::CheatSystem system,
  cheats::CheatConfiguration configuration)
{
  if (!isGameLoaded() ||
      !cheats::validateCheatConfiguration(system, configuration)) {
    return;
  }
  cheatSystem_ = system;
  cheatConfiguration_ = std::move(configuration);
  cheatSessionReady_ = true;
  if (auto* dialog = findChild<CheatManagerDialog*>(
        QStringLiteral("cheatManagerDialog"))) {
    dialog->setConfiguration(cheatConfiguration_);
  }
  updateCheatAction();
}

void MainWindow::clearCheatSession()
{
  cheatSessionReady_ = false;
  cheatConfiguration_ = {};
  if (auto* dialog = findChild<CheatManagerDialog*>(
        QStringLiteral("cheatManagerDialog"))) {
    dialog->reject();
  }
  updateCheatAction();
}

void MainWindow::setCheatConfigurationSink(CheatConfigurationSink sink)
{
  cheatConfigurationSink_ = std::move(sink);
}

void MainWindow::showCheats()
{
  if (!cheatSessionReady_ || !isGameLoaded()) {
    return;
  }
  if (auto* existing = findChild<CheatManagerDialog*>(
        QStringLiteral("cheatManagerDialog"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new CheatManagerDialog(
    cheatSystem_, cheatConfiguration_, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setConfigurationSink(
    [this](const cheats::CheatConfiguration& configuration) {
      if (cheatConfigurationSink_) {
        const auto saved = cheatConfigurationSink_(configuration);
        if (!saved) {
          return saved;
        }
      }
      cheatConfiguration_ = configuration;
      statusBar()->showMessage(tr("Cheat configuration applied."), 3'000);
      return PersistenceStatus{};
    });
  dialog->open();
}

void MainWindow::showCheatError(const std::string& detail)
{
  statusBar()->showMessage(tr("Cheat operation failed"), 5'000);
  dialogService_->showError(
    this, tr("Cheat Error"), QString::fromStdString(detail));
}

void MainWindow::updateCheatAction()
{
  if (auto* action = findChild<QAction*>(QStringLiteral("cheatsAction"))) {
    action->setEnabled(isGameLoaded() && cheatSessionReady_);
  }
}

void MainWindow::setPerGameSettingsSession(
  settings::PerGameSettings overrides,
  settings::GlobalGameSettings global)
{
  if (!isGameLoaded() || !settings::validatePerGameSettings(overrides) ||
      !settings::validateVideoSettings(global.video) ||
      !settings::validateAudioSettings(global.audio) ||
      !validateCoreSystemSettings(global.system)) {
    return;
  }
  perGameSettings_ = std::move(overrides);
  globalGameSettings_ = std::move(global);
  perGameSettingsSessionReady_ = true;
  if (auto* dialog = findChild<PerGameSettingsDialog*>(
        QStringLiteral("perGameSettingsDialog"))) {
    dialog->setSession(perGameSettings_, globalGameSettings_);
  }
  updatePerGameSettingsAction();
}

void MainWindow::clearPerGameSettingsSession()
{
  perGameSettingsSessionReady_ = false;
  perGameSettings_ = {};
  if (auto* dialog = findChild<PerGameSettingsDialog*>(
        QStringLiteral("perGameSettingsDialog"))) {
    dialog->reject();
  }
  updatePerGameSettingsAction();
}

void MainWindow::setPerGameSettingsSink(PerGameSettingsSink sink)
{
  perGameSettingsSink_ = std::move(sink);
}

void MainWindow::showPerGameSettings()
{
  if (!perGameSettingsSessionReady_ || !isGameLoaded()) {
    return;
  }
  if (auto* existing = findChild<PerGameSettingsDialog*>(
        QStringLiteral("perGameSettingsDialog"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  std::vector<std::string> profiles;
  profiles.reserve(inputConfiguration_.profiles.size());
  for (const auto& profile : inputConfiguration_.profiles) {
    profiles.push_back(profile.name);
  }
  auto* dialog = new PerGameSettingsDialog(
    perGameSettings_, globalGameSettings_, std::move(profiles),
    availableAudioDevices_, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setConfigurationSink(
    [this](const settings::PerGameSettings& configuration) {
      if (perGameSettingsSink_) {
        const auto saved = perGameSettingsSink_(configuration);
        if (!saved) {
          return saved;
        }
      }
      perGameSettings_ = configuration;
      statusBar()->showMessage(
        configuration.empty()
          ? tr("This game now uses all global settings.")
          : tr("Per-game settings applied."),
        3'000);
      return PersistenceStatus{};
    });
  dialog->open();
}

void MainWindow::showPerGameSettingsError(const std::string& detail)
{
  statusBar()->showMessage(tr("Per-game settings operation failed"), 5'000);
  dialogService_->showError(
    this, tr("Per-Game Settings Error"), QString::fromStdString(detail));
}

void MainWindow::updatePerGameSettingsAction()
{
  if (auto* action = findChild<QAction*>(
        QStringLiteral("perGameSettingsAction"))) {
    action->setEnabled(isGameLoaded() && perGameSettingsSessionReady_);
  }
}

void MainWindow::setGameInformationRequestSink(GameInformationRequestSink sink)
{
  gameInformationRequestSink_ = std::move(sink);
}

void MainWindow::setGameInformationBusy(bool busy)
{
  gameInformationBusy_ = busy;
  updateGameInformationAction();
  if (busy) {
    statusBar()->showMessage(tr("Reading game information…"));
  }
}

void MainWindow::requestGameInformation()
{
  if (loadedGamePath_.empty() || gameInformationBusy_) {
    return;
  }
  if (!gameInformationRequestSink_) {
    showGameInformationError("The game metadata service is not available.");
    return;
  }
  setGameInformationBusy(true);
  gameInformationRequestSink_(loadedGamePath_);
}

void MainWindow::updateGameInformationAction()
{
  auto* action = findChild<QAction*>(QStringLiteral("gameInformationAction"));
  action->setEnabled(
    !loadedGamePath_.empty() && !gameLoading_ && !gameInformationBusy_);
}

void MainWindow::showGameInformation(const library::GameMetadata& metadata)
{
  setGameInformationBusy(false);
  statusBar()->showMessage(tr("Game information ready"), 3000);
  if (auto* existing = findChild<GameInformationDialog*>(
        QStringLiteral("gameInformationDialog"))) {
    existing->setMetadata(metadata);
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new GameInformationDialog(metadata, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->open();
}

void MainWindow::showGameInformationError(const std::string& detail)
{
  setGameInformationBusy(false);
  statusBar()->showMessage(tr("Game information failed"), 5000);
  dialogService_->showError(
    this, tr("Game Information Failed"), QString::fromStdString(detail));
}

void MainWindow::setGameLibraryActions(GameLibraryActions actions)
{
  gameLibraryActions_ = std::move(actions);
  if (auto* dialog = findChild<GameLibraryDialog*>(
        QStringLiteral("gameLibraryDialog"))) {
    dialog->setActions(gameLibraryActions_);
  }
}

void MainWindow::setGameLibrarySnapshot(
  std::vector<library::LibraryDirectory> directories,
  std::vector<library::LibraryGame> games)
{
  gameLibraryDirectories_ = std::move(directories);
  gameLibraryGames_ = std::move(games);
  if (auto* dialog = findChild<GameLibraryDialog*>(
        QStringLiteral("gameLibraryDialog"))) {
    dialog->setSnapshot(gameLibraryDirectories_, gameLibraryGames_);
  }
}

void MainWindow::setGameLibraryAvailable(
  bool available,
  const std::string& detail)
{
  gameLibraryAvailable_ = available;
  gameLibraryUnavailableDetail_ = detail;
  if (auto* dialog = findChild<GameLibraryDialog*>(
        QStringLiteral("gameLibraryDialog"))) {
    dialog->setServiceAvailable(available, detail);
  }
}

void MainWindow::showGameLibrary()
{
  if (auto* existing = findChild<GameLibraryDialog*>(
        QStringLiteral("gameLibraryDialog"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new GameLibraryDialog(dialogService_, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setActions(gameLibraryActions_);
  dialog->setSnapshot(gameLibraryDirectories_, gameLibraryGames_);
  dialog->setServiceAvailable(
    gameLibraryAvailable_, gameLibraryUnavailableDetail_);
  dialog->open();
}

void MainWindow::showGameLibraryScanStarted(
  std::int64_t directoryId,
  const std::filesystem::path& path)
{
  if (auto* dialog = findChild<GameLibraryDialog*>(
        QStringLiteral("gameLibraryDialog"))) {
    dialog->showScanStarted(directoryId, path);
  }
}

void MainWindow::showGameLibraryScanProgress(
  std::int64_t directoryId,
  const library::GameLibraryScanSummary& summary)
{
  if (auto* dialog = findChild<GameLibraryDialog*>(
        QStringLiteral("gameLibraryDialog"))) {
    dialog->showScanProgress(directoryId, summary);
  }
}

void MainWindow::showGameLibraryScanCompleted(
  std::int64_t directoryId,
  const library::GameLibraryScanSummary& summary)
{
  if (auto* dialog = findChild<GameLibraryDialog*>(
        QStringLiteral("gameLibraryDialog"))) {
    dialog->showScanCompleted(directoryId, summary);
  }
}

void MainWindow::showGameLibraryScanFailed(
  std::int64_t directoryId,
  const std::string& detail)
{
  if (auto* dialog = findChild<GameLibraryDialog*>(
        QStringLiteral("gameLibraryDialog"))) {
    dialog->showScanFailed(directoryId, detail);
    return;
  }
  dialogService_->showError(
    this, tr("Game Library Scan Failed"), QString::fromStdString(detail));
}

void MainWindow::showGameLibraryError(const std::string& detail)
{
  if (auto* dialog = findChild<GameLibraryDialog*>(
        QStringLiteral("gameLibraryDialog"))) {
    dialog->showOperationError(detail);
    return;
  }
  dialogService_->showError(
    this, tr("Game Library Error"), QString::fromStdString(detail));
}

void MainWindow::setScreenshotSink(ScreenshotSink sink)
{
  screenshotSink_ = std::move(sink);
  updateScreenshotAction();
}

void MainWindow::setScreenshotBusy(bool busy)
{
  screenshotBusy_ = busy;
  updateScreenshotAction();
  if (busy) {
    statusBar()->showMessage(tr("Saving screenshot…"));
  }
}

void MainWindow::showScreenshotSaved(const std::filesystem::path& path)
{
  setScreenshotBusy(false);
  statusBar()->showMessage(
    tr("Screenshot saved: %1").arg(pathToQString(path)), 5'000);
}

void MainWindow::showScreenshotError(const std::string& detail)
{
  setScreenshotBusy(false);
  statusBar()->showMessage(tr("Screenshot failed"), 5'000);
  dialogService_->showError(
    this, tr("Unable to Save Screenshot"), QString::fromStdString(detail));
}

void MainWindow::setScreenshotSettings(
  settings::ScreenshotSettings settings,
  std::filesystem::path defaultDirectory)
{
  if (!settings::validateScreenshotSettings(settings) ||
      defaultDirectory.empty()) {
    return;
  }
  screenshotSettings_ = std::move(settings);
  defaultScreenshotDirectory_ = std::move(defaultDirectory);
  if (auto* dialog = findChild<ScreenshotSettingsDialog*>(
        QStringLiteral("screenshotSettingsDialog"))) {
    dialog->setSettings(screenshotSettings_);
  }
}

void MainWindow::setScreenshotSettingsSink(ScreenshotSettingsSink sink)
{
  screenshotSettingsSink_ = std::move(sink);
}

const settings::ScreenshotSettings& MainWindow::screenshotSettings() const noexcept
{
  return screenshotSettings_;
}

void MainWindow::showScreenshotSettings()
{
  if (auto* existing = findChild<ScreenshotSettingsDialog*>(
        QStringLiteral("screenshotSettingsDialog"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  if (!settings::validateScreenshotSettings(screenshotSettings_) ||
      defaultScreenshotDirectory_.empty()) {
    showScreenshotError("The screenshot settings are unavailable.");
    return;
  }
  auto* dialog = new ScreenshotSettingsDialog(
    screenshotSettings_, defaultScreenshotDirectory_, dialogService_, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setSettingsSink(
    [this](const settings::ScreenshotSettings& settings) {
      if (screenshotSettingsSink_) {
        const auto saved = screenshotSettingsSink_(settings);
        if (!saved) {
          dialogService_->showError(
            this, tr("Screenshot Settings Error"),
            QString::fromStdString(saved.message));
          return false;
        }
      }
      screenshotSettings_ = settings;
      statusBar()->showMessage(tr("Screenshot settings saved."), 3'000);
      return true;
    });
  dialog->open();
}

void MainWindow::requestScreenshot()
{
  if (screenshotBusy_) {
    return;
  }
  if (!isGameLoaded() || !displayWidget_->hasFrame()) {
    showScreenshotError("No native emulator frame is available yet.");
    return;
  }
  if (!screenshotSink_) {
    showScreenshotError("The screenshot service is not available.");
    return;
  }
  const auto frame = displayWidget_->currentFrameInfo();
  const auto pixels = displayWidget_->currentPixels();
  if (pixels.size() < frame.pixelCount()) {
    showScreenshotError("The native emulator frame is incomplete.");
    return;
  }
  std::vector<std::uint16_t> copy(
    pixels.begin(), pixels.begin() + static_cast<std::ptrdiff_t>(frame.pixelCount()));
  setScreenshotBusy(true);
  screenshotSink_(frame, std::move(copy));
}

void MainWindow::updateScreenshotAction()
{
  if (auto* action = findChild<QAction*>(QStringLiteral("screenshotAction"))) {
    action->setEnabled(isGameLoaded() && !screenshotBusy_ &&
      displayWidget_->hasFrame() && static_cast<bool>(screenshotSink_));
  }
}

void MainWindow::showAboutDialog()
{
  if (auto* existing = findChild<AboutDialog*>(QStringLiteral("aboutDialog"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new AboutDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->open();
}

void MainWindow::showInputConfiguration(InputConfigurationTab tab)
{
  if (auto* existing = findChild<InputConfigurationDialog*>(
        QStringLiteral("inputConfigurationDialog"))) {
    existing->openTab(tab);
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new InputConfigurationDialog(
    inputConfiguration_, controllers_, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setConfigurationSink(
    [this](const input::InputConfiguration& configuration) {
      inputConfiguration_ = configuration;
      if (inputConfigurationSink_) {
        inputConfigurationSink_(configuration);
      }
    });
  dialog->setAssignmentSink(
    [this](std::uint32_t instanceId, std::size_t player) {
      if (controllerAssignmentSink_) {
        controllerAssignmentSink_(instanceId, player);
      }
    });
  dialog->openTab(tab);
  dialog->open();
}

void MainWindow::showAppearanceSettings()
{
  if (auto* existing = findChild<AppearanceSettingsDialog*>(
        QStringLiteral("appearanceSettingsDialog"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new AppearanceSettingsDialog(appearanceSettings_, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setSettingsSink(
    [this](const settings::AppearanceSettings& settings) {
      if (appearanceSettingsSink_) {
        const auto saved = appearanceSettingsSink_(settings);
        if (!saved) {
          return saved;
        }
      }
      appearanceSettings_ = settings;
      statusBar()->showMessage(tr("Appearance settings applied."), 3'000);
      return PersistenceStatus{};
    });
  dialog->open();
}

void MainWindow::setDiagnosticsSnapshotProvider(
  DiagnosticsSnapshotProvider provider)
{
  diagnosticsSnapshotProvider_ = std::move(provider);
}

void MainWindow::showDiagnostics()
{
  auto snapshot = diagnosticsSnapshotProvider_
    ? diagnosticsSnapshotProvider_()
    : diagnostics::staticDiagnosticsSnapshot();
  if (auto* existing = findChild<DiagnosticsDialog*>(
        QStringLiteral("diagnosticsDialog"))) {
    existing->setSnapshot(std::move(snapshot));
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new DiagnosticsDialog(std::move(snapshot), this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->open();
}

void MainWindow::showVideoSettings()
{
  if (auto* existing = findChild<VideoSettingsDialog*>(
        QStringLiteral("videoSettingsDialog"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new VideoSettingsDialog(videoSettings_, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setSettingsSink([this](const settings::VideoSettings& settings) {
    applyVideoSettings(settings, true);
  });
  dialog->open();
}

void MainWindow::showAudioSettings()
{
  if (auto* existing = findChild<AudioSettingsDialog*>(
        QStringLiteral("audioSettingsDialog"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new AudioSettingsDialog(
    audioSettings_, availableAudioDevices_, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setSettingsSink([this](const settings::AudioSettings& settings) {
    applyAudioSettings(settings, true);
  });
  dialog->open();
}

void MainWindow::showSystemSettings()
{
  if (auto* existing = findChild<SystemSettingsDialog*>(
        QStringLiteral("systemSettingsDialog"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new SystemSettingsDialog(systemSettings_, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setSettingsSink([this](const CoreSystemSettings& settings) {
    setSystemSettings(settings);
    if (systemSettingsSink_) {
      systemSettingsSink_(systemSettings_);
    }
    statusBar()->showMessage(
      tr("System settings will apply when the next game is loaded."), 3'000);
  });
  dialog->open();
}

void MainWindow::showBiosSettings()
{
  if (auto* existing = findChild<BiosSettingsDialog*>(
        QStringLiteral("biosSettingsDialog"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new BiosSettingsDialog(biosSnapshot_, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setConfigurationSink(
    [this](const platform::BiosConfiguration& configuration) {
      if (!biosConfigurationSink_) {
        return;
      }
      const auto saved = biosConfigurationSink_(configuration);
      if (!saved) {
        dialogService_->showError(this, tr("BIOS configuration error"),
          QString::fromStdString(saved.message));
        return;
      }
      statusBar()->showMessage(tr("BIOS configuration saved."), 3'000);
    });
  dialog->open();
}

void MainWindow::setVideoSettings(settings::VideoSettings settings)
{
  applyVideoSettings(settings, false);
}

void MainWindow::setVideoSettingsSink(VideoSettingsSink sink)
{
  videoSettingsSink_ = std::move(sink);
}

const settings::VideoSettings& MainWindow::videoSettings() const noexcept
{
  return videoSettings_;
}

void MainWindow::setAudioSettings(settings::AudioSettings settings)
{
  applyAudioSettings(settings, false);
}

void MainWindow::setAppearanceSettings(settings::AppearanceSettings value)
{
  if (!settings::validateAppearanceSettings(value)) {
    return;
  }
  appearanceSettings_ = value;
  if (auto* dialog = findChild<AppearanceSettingsDialog*>(
        QStringLiteral("appearanceSettingsDialog"))) {
    dialog->setSettings(appearanceSettings_);
  }
}

void MainWindow::setAppearanceSettingsSink(AppearanceSettingsSink sink)
{
  appearanceSettingsSink_ = std::move(sink);
}

const settings::AppearanceSettings&
MainWindow::appearanceSettings() const noexcept
{
  return appearanceSettings_;
}

void MainWindow::setAvailableAudioDevices(std::vector<std::string> devices)
{
  availableAudioDevices_ = std::move(devices);
}

void MainWindow::setAudioSettingsSink(AudioSettingsSink sink)
{
  audioSettingsSink_ = std::move(sink);
}

const settings::AudioSettings& MainWindow::audioSettings() const noexcept
{
  return audioSettings_;
}

void MainWindow::setSystemSettings(CoreSystemSettings settings)
{
  if (!validateCoreSystemSettings(settings)) {
    return;
  }
  systemSettings_ = settings;
  if (auto* dialog = findChild<SystemSettingsDialog*>(
        QStringLiteral("systemSettingsDialog"))) {
    dialog->setSettings(systemSettings_);
  }
}

void MainWindow::setSystemSettingsSink(SystemSettingsSink sink)
{
  systemSettingsSink_ = std::move(sink);
}

const CoreSystemSettings& MainWindow::systemSettings() const noexcept
{
  return systemSettings_;
}

void MainWindow::setBiosSnapshot(platform::BiosSnapshot snapshot)
{
  biosSnapshot_ = std::move(snapshot);
  if (auto* dialog = findChild<BiosSettingsDialog*>(
        QStringLiteral("biosSettingsDialog"))) {
    dialog->setSnapshot(biosSnapshot_);
  }
}

void MainWindow::setBiosConfigurationSink(BiosConfigurationSink sink)
{
  biosConfigurationSink_ = std::move(sink);
}

void MainWindow::setDiscOperationSink(DiscOperationSink sink)
{
  discOperationSink_ = std::move(sink);
}

void MainWindow::setSegaCdSession(
  bool enabled,
  std::string region,
  std::filesystem::path discPath,
  bool ejected,
  bool discPresent)
{
  segaCdSession_ = enabled && isGameLoaded();
  discRegion_ = std::move(region);
  currentDiscPath_ = std::move(discPath);
  discEjected_ = segaCdSession_ && ejected;
  discPresent_ = segaCdSession_ && discPresent;
  discOperationBusy_ = false;
  if (segaCdSession_) {
    systemStatus_->setText(tr("System: Sega CD / Mega CD"));
    regionStatus_->setText(tr("Region: %1").arg(
      QString::fromStdString(discRegion_.empty() ? "Unknown" : discRegion_)));
  } else {
    discRegion_.clear();
    currentDiscPath_.clear();
    systemStatus_->setText(tr("System: —"));
    regionStatus_->setText(tr("Region: —"));
  }
  updateDiscActions();
}

void MainWindow::setDiscOperationBusy(bool busy)
{
  discOperationBusy_ = busy && segaCdSession_;
  updateDiscActions();
}

void MainWindow::showDiscOperationSuccess(DiscUiOperation operation)
{
  statusBar()->showMessage(
    operation == DiscUiOperation::change
      ? tr("Replacement disc inserted.")
      : (discEjected_ ? tr("Disc tray opened.") : tr("Disc tray closed.")),
    3'000);
}

void MainWindow::showDiscOperationError(
  DiscUiOperation operation,
  const std::string& detail)
{
  discOperationBusy_ = false;
  updateDiscActions();
  const auto title = operation == DiscUiOperation::change
    ? tr("Unable to Change Disc") : tr("Unable to Operate Disc Tray");
  statusBar()->showMessage(title, 5'000);
  dialogService_->showError(this, title, QString::fromStdString(detail));
}

const platform::BiosSnapshot& MainWindow::biosSnapshot() const noexcept
{
  return biosSnapshot_;
}

void MainWindow::applyVideoSettings(
  const settings::VideoSettings& settings,
  bool notifySink)
{
  if (!settings::validateVideoSettings(settings)) {
    return;
  }
  videoSettings_ = settings;
  displayWidget_->setAspectMode(settings.aspect);
  displayWidget_->setScaleMode(settings.scaling);
  displayWidget_->setVideoFilter(settings.presentationFilter);
  updateVideoActionChecks();
  if (auto* dialog = findChild<VideoSettingsDialog*>(
        QStringLiteral("videoSettingsDialog"))) {
    dialog->setSettings(videoSettings_);
  }
  if (notifySink && videoSettingsSink_) {
    videoSettingsSink_(videoSettings_);
  }
}

void MainWindow::updateVideoActionChecks()
{
  findChild<QAction*>(QStringLiteral("nativeAspectAction"))->setChecked(
    videoSettings_.aspect == video::AspectMode::native);
  findChild<QAction*>(QStringLiteral("fourThreeAspectAction"))->setChecked(
    videoSettings_.aspect == video::AspectMode::fourThree);
  findChild<QAction*>(QStringLiteral("stretchAspectAction"))->setChecked(
    videoSettings_.aspect == video::AspectMode::stretch);
  findChild<QAction*>(QStringLiteral("fitScaleAction"))->setChecked(
    videoSettings_.scaling == video::ScaleMode::fit);
  findChild<QAction*>(QStringLiteral("integerScaleAction"))->setChecked(
    videoSettings_.scaling == video::ScaleMode::integer);
  findChild<QAction*>(QStringLiteral("nearestFilterAction"))->setChecked(
    videoSettings_.presentationFilter == video::VideoFilter::nearest);
  findChild<QAction*>(QStringLiteral("bilinearFilterAction"))->setChecked(
    videoSettings_.presentationFilter == video::VideoFilter::bilinear);

  const auto setDataChecked = [this](const char* actionName, int value, int current) {
    auto* action = findChild<QAction*>(QString::fromLatin1(actionName));
    action->setChecked(value == current);
  };
  const auto overscan = static_cast<int>(videoSettings_.core.overscan);
  setDataChecked("overscanDisabledAction",
    static_cast<int>(CoreOverscanMode::disabled), overscan);
  setDataChecked("overscanVerticalAction",
    static_cast<int>(CoreOverscanMode::vertical), overscan);
  setDataChecked("overscanHorizontalAction",
    static_cast<int>(CoreOverscanMode::horizontal), overscan);
  setDataChecked("overscanFullAction",
    static_cast<int>(CoreOverscanMode::full), overscan);
  const auto ntsc = static_cast<int>(videoSettings_.core.ntscFilter);
  setDataChecked("ntscDisabledAction",
    static_cast<int>(CoreNtscFilter::disabled), ntsc);
  setDataChecked("ntscMonochromeAction",
    static_cast<int>(CoreNtscFilter::monochrome), ntsc);
  setDataChecked("ntscCompositeAction",
    static_cast<int>(CoreNtscFilter::composite), ntsc);
  setDataChecked("ntscSVideoAction",
    static_cast<int>(CoreNtscFilter::sVideo), ntsc);
  setDataChecked("ntscRgbAction", static_cast<int>(CoreNtscFilter::rgb), ntsc);
  findChild<QAction*>(QStringLiteral("singleFieldRenderAction"))->setChecked(
    videoSettings_.core.interlacedRender ==
      CoreInterlacedRenderMode::singleField);
  findChild<QAction*>(QStringLiteral("doubleFieldRenderAction"))->setChecked(
    videoSettings_.core.interlacedRender ==
      CoreInterlacedRenderMode::doubleField);
  findChild<QAction*>(QStringLiteral("gameGearExtendedScreenAction"))->setChecked(
    videoSettings_.core.gameGearExtendedScreen);
}

void MainWindow::applyAudioSettings(
  const settings::AudioSettings& settings,
  bool notifySink)
{
  if (!settings::validateAudioSettings(settings)) {
    return;
  }
  audioSettings_ = settings;
  updateAudioActionChecks();
  if (auto* dialog = findChild<AudioSettingsDialog*>(
        QStringLiteral("audioSettingsDialog"))) {
    dialog->setSettings(audioSettings_);
  }
  if (notifySink && audioSettingsSink_) {
    audioSettingsSink_(audioSettings_);
  }
  statusBar()->showMessage(
    tr("Volume: %1%2").arg(audioSettings_.masterVolumePercent)
      .arg(audioSettings_.muted ? tr(" (muted)") : QString{}),
    2'000);
}

void MainWindow::updateAudioActionChecks()
{
  const QSignalBlocker blocker{
    findChild<QAction*>(QStringLiteral("muteAction"))};
  findChild<QAction*>(QStringLiteral("muteAction"))->setChecked(
    audioSettings_.muted);
}

void MainWindow::setInputConfiguration(input::InputConfiguration configuration)
{
  if (input::validateInputConfiguration(configuration)) {
    inputConfiguration_ = std::move(configuration);
  }
}

void MainWindow::setConnectedControllers(
  std::vector<input::ControllerInfo> controllers)
{
  controllers_ = std::move(controllers);
  if (auto* dialog = findChild<InputConfigurationDialog*>(
        QStringLiteral("inputConfigurationDialog"))) {
    dialog->setControllers(controllers_);
  }
}

void MainWindow::setInputConfigurationSink(InputConfigurationSink sink)
{
  inputConfigurationSink_ = std::move(sink);
}

void MainWindow::setControllerAssignmentSink(ControllerAssignmentSink sink)
{
  controllerAssignmentSink_ = std::move(sink);
}

void MainWindow::setDialogService(std::shared_ptr<DialogService> service)
{
  if (service) {
    dialogService_ = std::move(service);
    if (auto* dialog = findChild<GameLibraryDialog*>(
          QStringLiteral("gameLibraryDialog"))) {
      dialog->setDialogService(dialogService_);
    }
    if (auto* dialog = findChild<ScreenshotSettingsDialog*>(
          QStringLiteral("screenshotSettingsDialog"))) {
      dialog->setDialogService(dialogService_);
    }
  }
}

void MainWindow::setGameLoadSink(GameLoadSink sink)
{
  gameLoadSink_ = std::move(sink);
}

void MainWindow::setGameCloseSink(GameCloseSink sink)
{
  gameCloseSink_ = std::move(sink);
}

void MainWindow::setClearRecentGamesSink(ClearRecentGamesSink sink)
{
  clearRecentGamesSink_ = std::move(sink);
}

void MainWindow::setStateOperationSink(StateOperationSink sink)
{
  stateOperationSink_ = std::move(sink);
}

void MainWindow::setStateSessionReady(bool ready)
{
  stateSessionReady_ = ready && isGameLoaded();
  updateStateActions();
}

void MainWindow::setStateOperationBusy(bool busy)
{
  stateOperationBusy_ = busy;
  updateStateActions();
  updateStateSlotPresentation();
}

void MainWindow::setStateSlotViews(std::array<StateSlotView, 10> views)
{
  for (std::uint32_t slot = 0U; slot < views.size(); ++slot) {
    views[slot].slot = slot;
  }
  stateSlotViews_ = std::move(views);
  updateStateSlotPresentation();
  updateStateActions();
}

void MainWindow::setSelectedStateSlot(std::uint32_t slot)
{
  if (slot > 9U) {
    return;
  }
  selectedStateSlot_ = slot;
  updateStateSlotPresentation();
  updateStateActions();
}

std::uint32_t MainWindow::selectedStateSlot() const noexcept
{
  return selectedStateSlot_;
}

void MainWindow::showStateOperationSuccess(
  StateUiOperation operation,
  std::uint32_t slot)
{
  QString action;
  switch (operation) {
    case StateUiOperation::save:
      action = tr("saved");
      break;
    case StateUiOperation::load:
      action = tr("loaded");
      break;
    case StateUiOperation::remove:
      action = tr("deleted");
      break;
  }
  statusBar()->showMessage(tr("State slot %1 %2").arg(slot).arg(action), 4000);
}

void MainWindow::showStateOperationError(
  StateUiOperation operation,
  const std::string& detail)
{
  QString action;
  switch (operation) {
    case StateUiOperation::save:
      action = tr("Save State Failed");
      break;
    case StateUiOperation::load:
      action = tr("Load State Failed");
      break;
    case StateUiOperation::remove:
      action = tr("Delete State Failed");
      break;
  }
  statusBar()->showMessage(action, 5000);
  dialogService_->showError(this, action, QString::fromStdString(detail));
}

void MainWindow::setRecentGames(std::vector<std::filesystem::path> paths)
{
  constexpr std::size_t maximumRecentGames = 12U;
  if (paths.size() > maximumRecentGames) {
    paths.resize(maximumRecentGames);
  }
  auto* menu = findChild<QMenu*>(QStringLiteral("openRecentMenu"));
  menu->clear();
  for (std::size_t index = 0; index < paths.size(); ++index) {
    auto path = std::move(paths[index]);
    auto title = pathToQString(path.filename());
    title.replace(QStringLiteral("&"), QStringLiteral("&&"));
    auto* action = menu->addAction(QStringLiteral("%1. %2")
      .arg(static_cast<qulonglong>(index + 1U)).arg(title));
    action->setObjectName(
      QStringLiteral("recentGameAction%1").arg(index));
    action->setToolTip(pathToQString(path));
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
      action->setText(action->text() + tr(" (Missing)"));
      action->setEnabled(false);
    }
    connect(action, &QAction::triggered, this, [this, path = std::move(path)] {
      static_cast<void>(requestGameLoad(path));
    });
  }
  if (!paths.empty()) {
    menu->addSeparator();
    auto* clear = menu->addAction(tr("&Clear Recent Games"));
    clear->setObjectName(QStringLiteral("clearRecentGamesAction"));
    connect(clear, &QAction::triggered, this, [this] {
      if (clearRecentGamesSink_) {
        const auto sink = clearRecentGamesSink_;
        QTimer::singleShot(0, this, [sink] { sink(); });
      }
    });
  }
  hasRecentGames_ = !paths.empty();
  menu->setEnabled(hasRecentGames_ && !gameLoading_);
}

bool MainWindow::requestGameLoad(const std::filesystem::path& path)
{
  if (gameLoading_) {
    presentGameLoadError(path, "Another game operation is still in progress.");
    return false;
  }
  const auto status = validateGameFile(path);
  if (!status) {
    presentGameLoadError(path, status.message);
    return false;
  }
  if (!gameLoadSink_) {
    presentGameLoadError(path, "The emulation service is not available.");
    return false;
  }
  setGameLoading(path);
  gameLoadSink_(path);
  return true;
}

void MainWindow::setGameLoading(const std::filesystem::path& path)
{
  if (auto* information = findChild<GameInformationDialog*>(
        QStringLiteral("gameInformationDialog"))) {
    information->close();
  }
  if (auto* cheats = findChild<CheatManagerDialog*>(
        QStringLiteral("cheatManagerDialog"))) {
    cheats->reject();
  }
  if (auto* perGame = findChild<PerGameSettingsDialog*>(
        QStringLiteral("perGameSettingsDialog"))) {
    perGame->reject();
  }
  gameLoading_ = true;
  emulationPaused_ = false;
  fastForwardActive_ = false;
  gameInformationBusy_ = false;
  pendingGamePath_ = path;
  displayWidget_->clearFrame();
  findChild<QAction*>(QStringLiteral("openGameAction"))->setEnabled(false);
  findChild<QMenu*>(QStringLiteral("openRecentMenu"))->setEnabled(false);
  setGameActionsEnabled(false);
  gameStatus_->setText(
    tr("Loading %1…").arg(pathToQString(path.filename())));
  statusBar()->showMessage(tr("Loading game…"));
}

void MainWindow::setGameLoaded(const std::filesystem::path& path)
{
  if (loadedGamePath_ != path) {
    stateSessionReady_ = false;
    stateOperationBusy_ = false;
    for (std::uint32_t slot = 0U; slot < stateSlotViews_.size(); ++slot) {
      stateSlotViews_[slot] = StateSlotView{};
      stateSlotViews_[slot].slot = slot;
    }
  }
  loadedGamePath_ = path;
  pendingGamePath_.clear();
  gameLoading_ = false;
  gameInformationBusy_ = false;
  findChild<QAction*>(QStringLiteral("openGameAction"))->setEnabled(true);
  findChild<QMenu*>(QStringLiteral("openRecentMenu"))->setEnabled(hasRecentGames_);
  setGameActionsEnabled(true);
  updateStateSlotPresentation();
  gameStatus_->setText(pathToQString(path.filename()));
  statusBar()->showMessage(tr("Game loaded"), 3000);
}

void MainWindow::setNoGameLoaded()
{
  clearCheatSession();
  clearPerGameSettingsSession();
  if (auto* information = findChild<GameInformationDialog*>(
        QStringLiteral("gameInformationDialog"))) {
    information->close();
  }
  loadedGamePath_.clear();
  pendingGamePath_.clear();
  gameLoading_ = false;
  gameInformationBusy_ = false;
  stateSessionReady_ = false;
  stateOperationBusy_ = false;
  emulationPaused_ = false;
  fastForwardActive_ = false;
  segaCdSession_ = false;
  discEjected_ = false;
  discPresent_ = false;
  discOperationBusy_ = false;
  discRegion_.clear();
  currentDiscPath_.clear();
  for (std::uint32_t slot = 0U; slot < stateSlotViews_.size(); ++slot) {
    stateSlotViews_[slot] = StateSlotView{};
    stateSlotViews_[slot].slot = slot;
  }
  findChild<QAction*>(QStringLiteral("openGameAction"))->setEnabled(true);
  findChild<QMenu*>(QStringLiteral("openRecentMenu"))->setEnabled(hasRecentGames_);
  setGameActionsEnabled(false);
  gameStatus_->setText(tr("No game loaded"));
  systemStatus_->setText(tr("System: —"));
  regionStatus_->setText(tr("Region: —"));
  fpsStatus_->setText(tr("0.0 FPS"));
  displayWidget_->clearFrame();
  updateStateSlotPresentation();
}

void MainWindow::chooseDisc()
{
  if (!segaCdSession_ || discOperationBusy_) {
    return;
  }
  const auto initial = currentDiscPath_.empty()
    ? loadedGamePath_.parent_path() : currentDiscPath_.parent_path();
  const auto selected = dialogService_->chooseDisc(this, initial);
  if (!selected) {
    return;
  }
  if (const auto status = validateDiscImageFile(*selected); !status) {
    showDiscOperationError(DiscUiOperation::change, status.message);
    return;
  }
  if (!discOperationSink_) {
    showDiscOperationError(
      DiscUiOperation::change, "The emulation service is not available.");
    return;
  }
  setDiscOperationBusy(true);
  discOperationSink_(DiscUiOperation::change, *selected, false);
}

void MainWindow::requestDiscEjected(bool ejected)
{
  if (!segaCdSession_ || discOperationBusy_ || !discOperationSink_) {
    updateDiscActions();
    return;
  }
  setDiscOperationBusy(true);
  discOperationSink_(DiscUiOperation::setEjected, {}, ejected);
}

void MainWindow::updateDiscActions()
{
  auto* change = findChild<QAction*>(QStringLiteral("changeDiscAction"));
  auto* eject = findChild<QAction*>(QStringLiteral("ejectDiscAction"));
  if (change == nullptr || eject == nullptr) {
    return;
  }
  const bool available = isGameLoaded() && segaCdSession_ && !discOperationBusy_;
  change->setEnabled(available);
  eject->setEnabled(available);
  change->setToolTip(discPresent_ && !currentDiscPath_.empty()
    ? tr("Current disc: %1").arg(pathToQString(currentDiscPath_))
    : tr("No disc inserted"));
  const QSignalBlocker blocker{eject};
  eject->setChecked(discEjected_);
  eject->setText(discEjected_ ? tr("Close Disc &Tray") : tr("&Eject Disc"));
}

void MainWindow::showGameLoadError(
  const std::filesystem::path& path,
  const std::string& detail,
  bool gameWasUnloaded)
{
  const auto previousGame = loadedGamePath_;
  if (gameWasUnloaded || previousGame.empty()) {
    setNoGameLoaded();
  } else {
    setGameLoaded(previousGame);
  }
  presentGameLoadError(path, detail);
}

void MainWindow::showGameCloseError(const std::string& detail)
{
  statusBar()->showMessage(tr("Game close failed"), 5000);
  dialogService_->showError(
    this,
    tr("Unable to Close Game"),
    tr("The game remains loaded because its save data could not be flushed.\n\n%1")
      .arg(QString::fromStdString(detail)));
}

void MainWindow::presentGameLoadError(
  const std::filesystem::path& path,
  const std::string& detail)
{
  const auto fileName = path.empty() ? tr("the selected file")
                                     : pathToQString(path.filename());
  const auto message = tr("Could not load %1.\n\n%2")
    .arg(fileName, QString::fromStdString(detail));
  statusBar()->showMessage(tr("Game load failed"), 5000);
  dialogService_->showError(this, tr("Unable to Open Game"), message);
}

void MainWindow::setFullscreen(bool enabled)
{
  findChild<QAction*>(QStringLiteral("fullscreenAction"))->setChecked(enabled);
}

bool MainWindow::isGameLoaded() const noexcept
{
  return !loadedGamePath_.empty() && !gameLoading_;
}

bool MainWindow::isGameLoading() const noexcept
{
  return gameLoading_;
}

const std::filesystem::path& MainWindow::loadedGamePath() const noexcept
{
  return loadedGamePath_;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
  const auto urls = event->mimeData()->urls();
  if (!gameLoading_ && urls.size() == 1 && urls.front().isLocalFile() &&
      hasSupportedGameExtension(pathFromQString(urls.front().toLocalFile()))) {
    event->acceptProposedAction();
  }
}

void MainWindow::dropEvent(QDropEvent* event)
{
  const auto urls = event->mimeData()->urls();
  if (urls.size() != 1 || !urls.front().isLocalFile()) {
    return;
  }
  if (requestGameLoad(pathFromQString(urls.front().toLocalFile()))) {
    event->acceptProposedAction();
  }
}

void MainWindow::chooseGame()
{
  const auto initialDirectory = loadedGamePath_.empty()
    ? std::filesystem::path{}
    : loadedGamePath_.parent_path();
  const auto selected = dialogService_->chooseGame(this, initialDirectory);
  if (selected) {
    static_cast<void>(requestGameLoad(*selected));
  }
}

void MainWindow::closeGame()
{
  if (gameCloseSink_ && isGameLoaded()) {
    gameLoading_ = true;
    findChild<QAction*>(QStringLiteral("openGameAction"))->setEnabled(false);
    findChild<QMenu*>(QStringLiteral("openRecentMenu"))->setEnabled(false);
    setGameActionsEnabled(false);
    gameStatus_->setText(tr("Closing game…"));
    gameCloseSink_();
  }
}

void MainWindow::requestStateOperation(StateUiOperation operation)
{
  if (!stateOperationSink_ || !stateSessionReady_ || stateOperationBusy_ ||
      !isGameLoaded()) {
    return;
  }
  const auto state = stateSlotViews_[selectedStateSlot_].state;
  if (operation == StateUiOperation::load &&
      state != StateSlotViewState::available) {
    return;
  }
  if (operation == StateUiOperation::remove &&
      state == StateSlotViewState::empty) {
    return;
  }
  stateOperationBusy_ = true;
  updateStateActions();
  updateStateSlotPresentation();
  stateOperationSink_(operation, selectedStateSlot_);
}

void MainWindow::updateStateActions()
{
  const bool ready = isGameLoaded() && stateSessionReady_ && !stateOperationBusy_;
  const auto selectedState = stateSlotViews_[selectedStateSlot_].state;
  findChild<QAction*>(QStringLiteral("saveStateAction"))->setEnabled(ready);
  findChild<QAction*>(QStringLiteral("loadStateAction"))->setEnabled(
    ready && selectedState == StateSlotViewState::available);
  findChild<QAction*>(QStringLiteral("deleteStateAction"))->setEnabled(
    ready && selectedState != StateSlotViewState::empty);
  findChild<QMenu*>(QStringLiteral("stateSlotMenu"))->setEnabled(ready);
  for (std::uint32_t slot = 0U; slot < stateSlotViews_.size(); ++slot) {
    findChild<QAction*>(QStringLiteral("stateSlotAction%1").arg(slot))
      ->setEnabled(ready);
  }
  findChild<QAction*>(QStringLiteral("previousStateSlotAction"))->setEnabled(ready);
  findChild<QAction*>(QStringLiteral("nextStateSlotAction"))->setEnabled(ready);

  if (stateOperationBusy_) {
    findChild<QAction*>(QStringLiteral("openGameAction"))->setEnabled(false);
    findChild<QAction*>(QStringLiteral("closeGameAction"))->setEnabled(false);
    findChild<QMenu*>(QStringLiteral("openRecentMenu"))->setEnabled(false);
  } else if (!gameLoading_) {
    findChild<QAction*>(QStringLiteral("openGameAction"))->setEnabled(true);
    findChild<QAction*>(QStringLiteral("closeGameAction"))->setEnabled(
      isGameLoaded());
    findChild<QMenu*>(QStringLiteral("openRecentMenu"))->setEnabled(
      hasRecentGames_);
  }
}

void MainWindow::updateStateSlotPresentation()
{
  for (std::uint32_t slot = 0U; slot < stateSlotViews_.size(); ++slot) {
    const auto& view = stateSlotViews_[slot];
    auto* action = findChild<QAction*>(
      QStringLiteral("stateSlotAction%1").arg(slot));
    if (action == nullptr) {
      continue;
    }
    QString stateText;
    switch (view.state) {
      case StateSlotViewState::empty:
        stateText = tr("Empty");
        break;
      case StateSlotViewState::available: {
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
          view.timestamp.time_since_epoch()).count();
        stateText = QDateTime::fromMSecsSinceEpoch(milliseconds)
          .toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        break;
      }
      case StateSlotViewState::invalid:
        stateText = tr("Invalid");
        break;
    }
    action->setText(tr("Slot %1 — %2").arg(slot).arg(stateText));
    action->setToolTip(QString::fromStdString(view.detail));
    action->setChecked(slot == selectedStateSlot_);
  }
  slotStatus_->setText(stateOperationBusy_
    ? tr("Slot %1 — Working…").arg(selectedStateSlot_)
    : tr("Slot %1").arg(selectedStateSlot_));
}

bool MainWindow::captureControllerButton(SDL_GamepadButton button)
{
  auto* dialog = findChild<InputConfigurationDialog*>(
    QStringLiteral("inputConfigurationDialog"));
  return dialog != nullptr && dialog->captureControllerButton(button);
}

bool MainWindow::presentLatestFrame()
{
  const bool presented = displayWidget_->presentLatestFrame();
  if (presented) {
    updateScreenshotAction();
  }
  return presented;
}

video::DisplayWidget* MainWindow::displayWidget() const noexcept
{
  return displayWidget_;
}

} // namespace genplusgx::ui
