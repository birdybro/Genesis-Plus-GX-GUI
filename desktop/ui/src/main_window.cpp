#include "genplusgx/ui/main_window.h"

#include "genplusgx/game_file.h"
#include "genplusgx/ui/about_dialog.h"
#include "genplusgx/ui/audio_settings_dialog.h"
#include "genplusgx/ui/dialog_service.h"
#include "genplusgx/ui/input_configuration_dialog.h"
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
  addAction(*file, tr("Game &Library…"), "gameLibraryAction", QKeySequence{tr("Ctrl+L")});
  file->addSeparator();
  addAction(*file, tr("&Screenshot"), "screenshotAction", QKeySequence{tr("F12")});
  file->addSeparator();
  auto* exit = addAction(*file, tr("E&xit"), "exitAction", QKeySequence::Quit);
  connect(exit, &QAction::triggered, this, &QWidget::close);

  auto* emulation = createMenu(tr("&Emulation"), "emulationMenu");
  auto* pause = addAction(*emulation, tr("&Pause"), "pauseAction", QKeySequence{tr("Space")});
  pause->setCheckable(true);
  addAction(*emulation, tr("&Reset"), "resetAction", QKeySequence{tr("Ctrl+R")});
  addAction(*emulation, tr("&Soft Reset"), "softResetAction");
  auto* fastForward = addAction(
    *emulation, tr("&Fast Forward"), "fastForwardAction", QKeySequence{tr("Tab")});
  fastForward->setCheckable(true);
  addAction(*emulation, tr("Frame &Advance"), "frameAdvanceAction", QKeySequence{tr("N")});
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
  addAction(*emulation, tr("Change &Disc…"), "changeDiscAction");
  addAction(*emulation, tr("&Eject Disc"), "ejectDiscAction");

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
  addAction(*tools, tr("&Cheats…"), "cheatsAction");
  addAction(*tools, tr("Game &Information…"), "gameInformationAction");
  auto* settings = addAction(
    *tools, tr("&Settings…"), "settingsAction", QKeySequence::Preferences);
  connect(settings, &QAction::triggered, this, &MainWindow::showVideoSettings);
  addAction(*tools, tr("Log and &Diagnostics…"), "diagnosticsAction");

  auto* help = createMenu(tr("&Help"), "helpMenu");
  addAction(*help, tr("&User Guide"), "userGuideAction", QKeySequence::HelpContents);
  addAction(*help, tr("&Keyboard Shortcuts"), "keyboardShortcutsAction");
  help->addSeparator();
  auto* about = addAction(*help, tr("&About Genesis Plus GX GUI"), "aboutAction");
  connect(about, &QAction::triggered, this, &MainWindow::showAboutDialog);
  auto* aboutQt = addAction(*help, tr("About &Qt"), "aboutQtAction");
  connect(aboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);
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
    "changeDiscAction", "ejectDiscAction", "cheatsAction",
    "gameInformationAction"};
  for (const auto* name : gameActionNames) {
    findChild<QAction*>(QString::fromLatin1(name))->setEnabled(enabled);
  }
  updateStateActions();
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
  gameLoading_ = true;
  pendingGamePath_ = path;
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
  findChild<QAction*>(QStringLiteral("openGameAction"))->setEnabled(true);
  findChild<QMenu*>(QStringLiteral("openRecentMenu"))->setEnabled(hasRecentGames_);
  setGameActionsEnabled(true);
  updateStateSlotPresentation();
  gameStatus_->setText(pathToQString(path.filename()));
  statusBar()->showMessage(tr("Game loaded"), 3000);
}

void MainWindow::setNoGameLoaded()
{
  loadedGamePath_.clear();
  pendingGamePath_.clear();
  gameLoading_ = false;
  stateSessionReady_ = false;
  stateOperationBusy_ = false;
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

video::DisplayWidget* MainWindow::displayWidget() const noexcept
{
  return displayWidget_;
}

} // namespace genplusgx::ui
