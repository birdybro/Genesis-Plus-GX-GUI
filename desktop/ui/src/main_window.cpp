#include "genplusgx/ui/main_window.h"

#include "genplusgx/game_file.h"
#include "genplusgx/game_archive.h"
#include "genplusgx/game_patch.h"
#include "genplusgx/ui/about_dialog.h"
#include "genplusgx/ui/appearance_settings_dialog.h"
#include "genplusgx/ui/audio_settings_dialog.h"
#include "genplusgx/ui/bios_settings_dialog.h"
#include "genplusgx/ui/cheat_manager_dialog.h"
#include "genplusgx/ui/dialog_service.h"
#include "genplusgx/ui/diagnostics_dialog.h"
#include "genplusgx/ui/debug_tools_window.h"
#include "genplusgx/ui/game_information_dialog.h"
#include "genplusgx/ui/game_library_dialog.h"
#include "genplusgx/ui/help_dialog.h"
#include "genplusgx/ui/input_configuration_dialog.h"
#include "genplusgx/ui/per_game_settings_dialog.h"
#include "genplusgx/ui/rewind_settings_dialog.h"
#include "genplusgx/ui/session_settings_dialog.h"
#include "genplusgx/ui/speed_settings_dialog.h"
#include "genplusgx/ui/screenshot_settings_dialog.h"
#include "genplusgx/ui/system_settings_dialog.h"
#include "genplusgx/ui/video_settings_dialog.h"
#include "genplusgx/version.h"
#include "genplusgx/video/display_widget.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
#include <QDragEnterEvent>
#include <QDateTime>
#include <QDebug>
#include <QDropEvent>
#include <QKeyCombination>
#include <QKeyEvent>
#include <QKeySequence>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace genplusgx::ui {
namespace {

constexpr std::array speedPresets{50U, 75U, 100U, 125U, 150U, 200U};

bool isUnambiguousDiscImage(const std::filesystem::path& path)
{
  return hasSupportedDiscExtension(path) &&
    pathToQString(path.extension()).compare(
      QStringLiteral(".bin"), Qt::CaseInsensitive) != 0;
}

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
  applyHotkeyShortcuts();
  buildStatusBar();
  for (std::uint32_t slot = 0U; slot < stateSlotViews_.size(); ++slot) {
    stateSlotViews_[slot].slot = slot;
  }
  updateStateSlotPresentation();
  setGameActionsEnabled(false);
  applyVideoSettings(videoSettings_, false);
  applyAudioSettings(audioSettings_, false);
  static_cast<void>(applySpeedSettings(speedSettings_, false));
  qApp->installEventFilter(this);
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
  displayWidget_->setShaderFailureSink([this](std::string detail) {
    statusBar()->showMessage(tr("Shader disabled after a rendering error."), 5'000);
    dialogService_->showError(this, tr("Libretro Shader Error"),
      QString::fromStdString(detail));
  });
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
  auto* openPatchedGame = addAction(
    *file, tr("Open Game with &Patch…"), "openGameWithPatchAction",
    QKeySequence{tr("Ctrl+Shift+O")});
  openPatchedGame->setToolTip(
    tr("Open a cartridge image with an IPS, BPS, or UPS soft patch"));
  connect(openPatchedGame, &QAction::triggered,
    this, &MainWindow::chooseGameWithPatch);
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
  auto* recording = addAction(
    *file, tr("Start Lossless A/V &Recording…"), "recordingAction",
    QKeySequence{tr("Ctrl+Shift+F12")});
  recording->setCheckable(true);
  recording->setToolTip(
    tr("Record native PNG frames and lossless stereo WAV audio"));
  connect(recording, &QAction::toggled,
    this, &MainWindow::requestRecordingToggle);
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
    *emulation, tr("Fast Forward &Toggle"), "fastForwardAction",
    QKeySequence{tr("`")});
  fastForward->setCheckable(true);
  connect(fastForward, &QAction::toggled, this, [this](bool enabled) {
    const bool previous = fastForwardToggled_;
    fastForwardToggled_ = enabled;
    if (!requestEmulationControl(
          EmulationUiOperation::setFastForward,
          fastForwardToggled_ || fastForwardHeld_)) {
      fastForwardToggled_ = previous;
      updateEmulationControls();
    }
  });
  auto* slowMotion = addAction(
    *emulation, tr("Slow Motion T&oggle"), "slowMotionAction");
  slowMotion->setCheckable(true);
  slowMotion->setToolTip(
    tr("Toggle slow motion; hold the configured hotkey for momentary use"));
  connect(slowMotion, &QAction::toggled, this, [this](bool enabled) {
    const bool previous = slowMotionToggled_;
    slowMotionToggled_ = enabled;
    if (!requestEmulationControl(
          EmulationUiOperation::setSlowMotion,
          slowMotionToggled_ || slowMotionHeld_)) {
      slowMotionToggled_ = previous;
      updateEmulationControls();
    }
  });
  auto* speedMenu = emulation->addMenu(tr("Emulation &Speed"));
  speedMenu->setObjectName(QStringLiteral("emulationSpeedMenu"));
  auto* speedGroup = new QActionGroup(this);
  speedGroup->setObjectName(QStringLiteral("emulationSpeedActionGroup"));
  speedGroup->setExclusive(true);
  for (const auto percent : speedPresets) {
    auto* action = speedMenu->addAction(tr("%1%").arg(percent));
    action->setObjectName(QStringLiteral("emulationSpeed%1Action").arg(percent));
    action->setCheckable(true);
    action->setData(percent);
    speedGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, percent] {
      auto candidate = speedSettings_;
      candidate.normalPercent = percent;
      static_cast<void>(applySpeedSettings(candidate, true));
    });
  }
  speedMenu->addSeparator();
  auto* speedSettings = addAction(
    *speedMenu, tr("Speed &Settings…"), "speedSettingsAction");
  connect(speedSettings, &QAction::triggered,
    this, &MainWindow::showSpeedSettings);
  auto* rewind = addAction(
    *emulation, tr("&Rewind Toggle"), "rewindAction");
  rewind->setCheckable(true);
  rewind->setToolTip(tr("Toggle rewind; hold the configured rewind hotkey for momentary use"));
  connect(rewind, &QAction::toggled, this, [this](bool enabled) {
    const bool previous = rewindToggled_;
    rewindToggled_ = enabled;
    if (!requestEmulationControl(
          EmulationUiOperation::setRewinding,
          rewindToggled_ || rewindHeld_)) {
      rewindToggled_ = previous;
      updateEmulationControls();
    }
  });
  auto* rewindSettings = addAction(
    *emulation, tr("Rewind &Settings…"), "rewindSettingsAction");
  connect(rewindSettings, &QAction::triggered,
    this, &MainWindow::showRewindSettings);
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
  stateSlotMenu->addSeparator();
  auto* importState = addAction(
    *stateSlotMenu,
    tr("&Import State to Selected Slot…"),
    "importStateAction");
  connect(importState, &QAction::triggered, this, [this] {
    requestStateOperation(StateUiOperation::importFile);
  });
  auto* exportState = addAction(
    *stateSlotMenu,
    tr("E&xport Selected State…"),
    "exportStateAction");
  connect(exportState, &QAction::triggered, this, [this] {
    requestStateOperation(StateUiOperation::exportFile);
  });
  auto* manageStates = addAction(
    *stateSlotMenu,
    tr("&Manage Save States…"),
    "stateManagerAction");
  connect(manageStates, &QAction::triggered,
    this, &MainWindow::showStateManager);
  auto* changeDisc = addAction(
    *emulation, tr("Change &Disc…"), "changeDiscAction");
  connect(changeDisc, &QAction::triggered, this, &MainWindow::chooseDisc);
  auto* previousDisc = addAction(
    *emulation, tr("Pre&vious Playlist Disc"), "previousDiscAction",
    QKeySequence{tr("Ctrl+Shift+PageUp")});
  connect(previousDisc, &QAction::triggered, this, [this] {
    requestPlaylistDisc(-1);
  });
  auto* nextDisc = addAction(
    *emulation, tr("Ne&xt Playlist Disc"), "nextDiscAction",
    QKeySequence{tr("Ctrl+Shift+PageDown")});
  connect(nextDisc, &QAction::triggered, this, [this] {
    requestPlaylistDisc(1);
  });
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
  auto* shaders = video->addMenu(tr("&Shaders"));
  shaders->setObjectName(QStringLiteral("shaderMenu"));
  auto* shaderGroup = new QActionGroup(this);
  shaderGroup->setObjectName(QStringLiteral("shaderActionGroup"));
  auto* shaderOff = addAction(*shaders, tr("&Off"), "shaderDisabledAction");
  auto* builtinCrt = addAction(
    *shaders, tr("Built-in &CRT"), "builtinCrtShaderAction");
  auto* customShader = addAction(
    *shaders, tr("Custom Libretro Preset"), "customShaderAction");
  for (auto* action : {shaderOff, builtinCrt, customShader}) {
    action->setCheckable(true);
    shaderGroup->addAction(action);
  }
  customShader->setEnabled(false);
  shaderOff->setChecked(true);
  connect(shaderOff, &QAction::triggered, this, [this] {
    auto settings = videoSettings_;
    settings.shader = {};
    applyVideoSettings(settings, true);
  });
  connect(builtinCrt, &QAction::triggered, this, [this] {
    auto settings = videoSettings_;
    settings.shader = {
      .mode = video::ShaderMode::builtinCrt,
      .presetPath = {},
      .parameters = {},
    };
    applyVideoSettings(settings, true);
  });
  connect(customShader, &QAction::triggered, this, [this] {
    if (videoSettings_.shader.presetPath.empty()) {
      chooseShaderPreset();
      return;
    }
    auto settings = videoSettings_;
    settings.shader.mode = video::ShaderMode::libretroPreset;
    applyVideoSettings(settings, true);
  });
  shaders->addSeparator();
  auto* loadShader = addAction(*shaders, tr("Load Libretro &Preset…"),
    "loadShaderPresetAction");
  connect(loadShader, &QAction::triggered,
    this, &MainWindow::chooseShaderPreset);
  auto* shaderSettings = addAction(*shaders, tr("Shader &Parameters…"),
    "shaderParametersAction");
  connect(shaderSettings, &QAction::triggered,
    this, &MainWindow::showVideoSettings);
#if !GENPLUSGX_HAS_LIBRETRO_SHADERS
  shaders->setEnabled(false);
  shaders->menuAction()->setToolTip(
    tr("This build does not include Libretro shader support."));
#endif
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
    this, [this] { showSettings(); });
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
  tools->addSeparator();
  auto* developer = tools->addMenu(tr("&Developer Tools"));
  developer->setObjectName(QStringLiteral("developerToolsMenu"));
  developer->menuAction()->setVisible(false);
  auto* debugger = addAction(
    *developer, tr("Emulator &Debug Workspace…"), "debugToolsAction",
    QKeySequence{tr("Ctrl+Shift+D")});
  connect(debugger, &QAction::triggered, this, &MainWindow::showDebugTools);

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

void MainWindow::applyHotkeyShortcuts()
{
  const std::array actionMappings{
    std::pair{input::EmulatorHotkeyAction::openGame, "openGameAction"},
    std::pair{input::EmulatorHotkeyAction::closeGame, "closeGameAction"},
    std::pair{input::EmulatorHotkeyAction::gameLibrary, "gameLibraryAction"},
    std::pair{input::EmulatorHotkeyAction::pause, "pauseAction"},
    std::pair{input::EmulatorHotkeyAction::hardReset, "resetAction"},
    std::pair{input::EmulatorHotkeyAction::softReset, "softResetAction"},
    std::pair{input::EmulatorHotkeyAction::fullscreen, "fullscreenAction"},
    std::pair{input::EmulatorHotkeyAction::fastForwardToggle,
      "fastForwardAction"},
    std::pair{input::EmulatorHotkeyAction::slowMotionToggle,
      "slowMotionAction"},
    std::pair{input::EmulatorHotkeyAction::frameAdvance, "frameAdvanceAction"},
    std::pair{input::EmulatorHotkeyAction::saveState, "saveStateAction"},
    std::pair{input::EmulatorHotkeyAction::loadState, "loadStateAction"},
    std::pair{input::EmulatorHotkeyAction::previousStateSlot,
      "previousStateSlotAction"},
    std::pair{input::EmulatorHotkeyAction::nextStateSlot,
      "nextStateSlotAction"},
    std::pair{input::EmulatorHotkeyAction::deleteState, "deleteStateAction"},
    std::pair{input::EmulatorHotkeyAction::screenshot, "screenshotAction"},
    std::pair{input::EmulatorHotkeyAction::recording, "recordingAction"},
    std::pair{input::EmulatorHotkeyAction::mute, "muteAction"},
    std::pair{input::EmulatorHotkeyAction::volumeUp, "volumeUpAction"},
    std::pair{input::EmulatorHotkeyAction::volumeDown, "volumeDownAction"},
  };
  for (const auto& [hotkey, objectName] : actionMappings) {
    const auto combination = input::hotkeyCombination(inputConfiguration_, hotkey);
    if (auto* action = findChild<QAction*>(QString::fromLatin1(objectName));
        action != nullptr && combination) {
      action->setShortcut(
        QKeySequence{QKeyCombination::fromCombined(*combination)});
    }
  }
  constexpr std::array slotActions{
    input::EmulatorHotkeyAction::stateSlot0,
    input::EmulatorHotkeyAction::stateSlot1,
    input::EmulatorHotkeyAction::stateSlot2,
    input::EmulatorHotkeyAction::stateSlot3,
    input::EmulatorHotkeyAction::stateSlot4,
    input::EmulatorHotkeyAction::stateSlot5,
    input::EmulatorHotkeyAction::stateSlot6,
    input::EmulatorHotkeyAction::stateSlot7,
    input::EmulatorHotkeyAction::stateSlot8,
    input::EmulatorHotkeyAction::stateSlot9,
  };
  for (std::size_t slot = 0U; slot < slotActions.size(); ++slot) {
    const auto combination = input::hotkeyCombination(
      inputConfiguration_, slotActions[slot]);
    if (auto* action = findChild<QAction*>(
          QStringLiteral("stateSlotAction%1").arg(slot));
        action != nullptr && combination) {
      action->setShortcut(
        QKeySequence{QKeyCombination::fromCombined(*combination)});
    }
  }
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
  speedStatus_ = statusLabel(
    *statusBar(), tr("Speed 100%"), "speedStatusLabel", 0, true);
  slotStatus_ = statusLabel(*statusBar(), tr("Slot 0"), "stateSlotStatusLabel", 0, true);
}

void MainWindow::setGameActionsEnabled(bool enabled)
{
  static constexpr const char* gameActionNames[]{
    "closeGameAction", "screenshotAction", "pauseAction", "resetAction",
    "softResetAction", "fastForwardAction", "slowMotionAction", "rewindAction",
    "frameAdvanceAction",
    "cheatsAction",
    "gameInformationAction"};
  for (const auto* name : gameActionNames) {
    findChild<QAction*>(QString::fromLatin1(name))->setEnabled(enabled);
  }
  updateGameInformationAction();
  updateDiscActions();
  updateStateActions();
  updateScreenshotAction();
  updateRecordingAction();
  updateCheatAction();
  updatePerGameSettingsAction();
  updateEmulationControls();
}

void MainWindow::setEmulationControlSink(EmulationControlSink sink)
{
  emulationControlSink_ = std::move(sink);
  updateEmulationControls();
}

void MainWindow::setEmulationControlState(
  bool paused,
  bool fastForward,
  bool slowMotion,
  std::uint32_t speedPercent,
  bool rewinding,
  bool rewindAvailable)
{
  emulationPaused_ = isGameLoaded() && paused;
  fastForwardActive_ = isGameLoaded() && fastForward;
  slowMotionActive_ = isGameLoaded() && slowMotion;
  activeSpeedPercent_ = isGameLoaded()
    ? speedPercent : speedSettings_.normalPercent;
  rewindActive_ = isGameLoaded() && rewinding;
  rewindAvailable_ = isGameLoaded() && rewindAvailable;
  if (!isGameLoaded()) {
    fastForwardHeld_ = false;
    fastForwardToggled_ = false;
    slowMotionHeld_ = false;
    slowMotionToggled_ = false;
    rewindHeld_ = false;
    rewindToggled_ = false;
  } else if (!fastForwardHeld_) {
    fastForwardToggled_ = fastForwardActive_;
  }
  if (isGameLoaded() && !slowMotionHeld_) {
    slowMotionToggled_ = slowMotionActive_;
  }
  if (isGameLoaded() && !rewindHeld_) {
    rewindToggled_ = rewindActive_;
  }
  updateEmulationControls();
  if (auto* debugger = findChild<DebugToolsWindow*>(
        QStringLiteral("debugToolsWindow"))) {
    debugger->setPaused(emulationPaused_);
  }
}

bool MainWindow::requestEmulationControl(
  EmulationUiOperation operation,
  bool enabled)
{
  if (!isGameLoaded() || !emulationControlSink_ ||
      !emulationControlSink_(operation, enabled)) {
    updateEmulationControls();
    statusBar()->showMessage(tr("The emulation command could not be queued."), 5'000);
    return false;
  }

  if (operation == EmulationUiOperation::pause) {
    emulationPaused_ = true;
  } else if (operation == EmulationUiOperation::resume) {
    emulationPaused_ = false;
  } else if (operation == EmulationUiOperation::setFastForward) {
    fastForwardActive_ = enabled;
    if (enabled) {
      slowMotionActive_ = false;
      slowMotionHeld_ = false;
      slowMotionToggled_ = false;
      rewindActive_ = false;
      rewindHeld_ = false;
      rewindToggled_ = false;
    }
  } else if (operation == EmulationUiOperation::setSlowMotion) {
    slowMotionActive_ = enabled;
    if (enabled) {
      fastForwardActive_ = false;
      fastForwardHeld_ = false;
      fastForwardToggled_ = false;
      rewindActive_ = false;
      rewindHeld_ = false;
      rewindToggled_ = false;
    }
  } else if (operation == EmulationUiOperation::setRewinding) {
    rewindActive_ = enabled;
    if (enabled) {
      fastForwardActive_ = false;
      fastForwardHeld_ = false;
      fastForwardToggled_ = false;
      slowMotionActive_ = false;
      slowMotionHeld_ = false;
      slowMotionToggled_ = false;
    }
  }
  updateEmulationControls();
  return true;
}

void MainWindow::setFastForwardHeld(bool held)
{
  if (held == fastForwardHeld_) {
    return;
  }
  const bool previous = fastForwardHeld_;
  fastForwardHeld_ = held;
  const bool effective = fastForwardToggled_ || fastForwardHeld_;
  if (effective != fastForwardActive_ &&
      !requestEmulationControl(
        EmulationUiOperation::setFastForward, effective)) {
    fastForwardHeld_ = previous;
  }
}

void MainWindow::setSlowMotionHeld(bool held)
{
  if (held == slowMotionHeld_) {
    return;
  }
  const bool previous = slowMotionHeld_;
  slowMotionHeld_ = held;
  const bool effective = slowMotionToggled_ || slowMotionHeld_;
  if (effective != slowMotionActive_ &&
      !requestEmulationControl(EmulationUiOperation::setSlowMotion, effective)) {
    slowMotionHeld_ = previous;
  }
}

void MainWindow::setRewindHeld(bool held)
{
  if (held == rewindHeld_) {
    return;
  }
  const bool previous = rewindHeld_;
  rewindHeld_ = held;
  const bool effective = rewindToggled_ || rewindHeld_;
  if (effective != rewindActive_ &&
      !requestEmulationControl(EmulationUiOperation::setRewinding, effective)) {
    rewindHeld_ = previous;
  }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
  if (event->type() == QEvent::ApplicationDeactivate ||
      (event->type() == QEvent::WindowDeactivate && watched == this) ||
      (event->type() == QEvent::Hide && watched == this)) {
    setFastForwardHeld(false);
    setSlowMotionHeld(false);
    setRewindHeld(false);
  }

  if (event->type() != QEvent::KeyPress &&
      event->type() != QEvent::KeyRelease) {
    return QMainWindow::eventFilter(watched, event);
  }
  const auto fastForwardCombination = input::hotkeyCombination(
    inputConfiguration_, input::EmulatorHotkeyAction::fastForwardHold);
  const auto rewindCombination = input::hotkeyCombination(
    inputConfiguration_, input::EmulatorHotkeyAction::rewindHold);
  const auto slowMotionCombination = input::hotkeyCombination(
    inputConfiguration_, input::EmulatorHotkeyAction::slowMotionHold);
  if (!fastForwardCombination && !slowMotionCombination && !rewindCombination) {
    return QMainWindow::eventFilter(watched, event);
  }

  auto* keyEvent = static_cast<QKeyEvent*>(event);
  const auto matches = [event, keyEvent](
      std::optional<int> combination, bool held) {
    if (!combination) {
      return std::pair{false, false};
    }
    const auto configured = QKeyCombination::fromCombined(*combination);
    const bool release = held && event->type() == QEvent::KeyRelease &&
      keyEvent->key() == configured.key();
    const bool press = event->type() == QEvent::KeyPress &&
      keyEvent->keyCombination() == configured;
    return std::pair{press, release};
  };
  const auto [fastPress, fastRelease] = matches(
    fastForwardCombination, fastForwardHeld_);
  const auto [rewindPress, rewindRelease] = matches(
    rewindCombination, rewindHeld_);
  const auto [slowPress, slowRelease] = matches(
    slowMotionCombination, slowMotionHeld_);
  if (!fastPress && !fastRelease && !slowPress && !slowRelease &&
      !rewindPress && !rewindRelease) {
    return QMainWindow::eventFilter(watched, event);
  }

  auto* widget = qobject_cast<QWidget*>(watched);
  const bool belongsToWindow = widget != nullptr && widget->window() == this;
  const auto* active = QApplication::activeWindow();
  if ((fastPress || slowPress || rewindPress) &&
      (!belongsToWindow || (active != nullptr && active != this) ||
       !isGameLoaded() || gameLoading_ || emulationPaused_ ||
       (rewindPress && !rewindAvailable_))) {
    return QMainWindow::eventFilter(watched, event);
  }
  if (!keyEvent->isAutoRepeat()) {
    if (fastPress || fastRelease) {
      setFastForwardHeld(fastPress);
    }
    if (slowPress || slowRelease) {
      setSlowMotionHeld(slowPress);
    }
    if (rewindPress || rewindRelease) {
      setRewindHeld(rewindPress);
    }
  }
  return true;
}

void MainWindow::updateEmulationControls()
{
  const bool available = isGameLoaded() && !gameLoading_ && !sessionResumeBusy_ &&
                         static_cast<bool>(emulationControlSink_);
  auto* pause = findChild<QAction*>(QStringLiteral("pauseAction"));
  auto* reset = findChild<QAction*>(QStringLiteral("resetAction"));
  auto* softReset = findChild<QAction*>(QStringLiteral("softResetAction"));
  auto* fastForward = findChild<QAction*>(QStringLiteral("fastForwardAction"));
  auto* slowMotion = findChild<QAction*>(QStringLiteral("slowMotionAction"));
  auto* rewind = findChild<QAction*>(QStringLiteral("rewindAction"));
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
    fastForward->setChecked(available && fastForwardToggled_);
    fastForward->setEnabled(available);
  }
  if (slowMotion != nullptr) {
    const QSignalBlocker blocker{slowMotion};
    slowMotion->setChecked(available && slowMotionToggled_);
    slowMotion->setEnabled(available);
  }
  if (rewind != nullptr) {
    const QSignalBlocker blocker{rewind};
    rewind->setChecked(available && rewindToggled_);
    rewind->setEnabled(available && !emulationPaused_ &&
      (rewindAvailable_ || rewindActive_));
  }
  if (frameAdvance != nullptr) {
    frameAdvance->setEnabled(available && emulationPaused_);
  }
  if (speedStatus_ != nullptr) {
    const auto prefix = fastForwardActive_
      ? tr("Fast") : slowMotionActive_ ? tr("Slow") : tr("Speed");
    speedStatus_->setText(tr("%1 %2%").arg(prefix).arg(activeSpeedPercent_));
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
    action->setEnabled(
      isGameLoaded() && !sessionResumeBusy_ && cheatSessionReady_);
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
    action->setEnabled(
      isGameLoaded() && !sessionResumeBusy_ && perGameSettingsSessionReady_);
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
  gameInformationRequestSink_(loadedRuntimePath_);
}

void MainWindow::updateGameInformationAction()
{
  auto* action = findChild<QAction*>(QStringLiteral("gameInformationAction"));
  action->setEnabled(
    !loadedGamePath_.empty() && !gameLoading_ && !sessionResumeBusy_ &&
      !gameInformationBusy_);
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
  refreshSettingsDialog();
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
      refreshSettingsDialog();
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
    action->setEnabled(isGameLoaded() && !sessionResumeBusy_ && !screenshotBusy_ &&
      displayWidget_->hasFrame() && static_cast<bool>(screenshotSink_));
  }
}

void MainWindow::setRecordingSink(RecordingSink sink)
{
  recordingSink_ = std::move(sink);
  if (!recordingSink_) {
    recordingState_ = RecordingUiState::unavailable;
    recordingPath_.clear();
  } else if (recordingState_ == RecordingUiState::unavailable) {
    recordingState_ = RecordingUiState::idle;
  }
  updateRecordingAction();
}

void MainWindow::setRecordingState(
  RecordingUiState state,
  std::filesystem::path path,
  std::uint64_t writtenFrames,
  std::uint64_t droppedFrames)
{
  recordingState_ = state;
  recordingPath_ = std::move(path);
  updateRecordingAction();
  switch (state) {
    case RecordingUiState::unavailable:
      break;
    case RecordingUiState::idle:
      if (!recordingPath_.empty()) {
        statusBar()->showMessage(
          tr("Recording saved: %1 (%2 frames, %3 dropped)")
            .arg(pathToQString(recordingPath_))
            .arg(static_cast<qulonglong>(writtenFrames))
            .arg(static_cast<qulonglong>(droppedFrames)),
          8'000);
      }
      break;
    case RecordingUiState::starting:
      statusBar()->showMessage(tr("Starting lossless recording…"));
      break;
    case RecordingUiState::recording:
      statusBar()->showMessage(
        tr("Recording native video frames and audio…"));
      break;
    case RecordingUiState::stopping:
      statusBar()->showMessage(tr("Finalizing lossless recording…"));
      break;
  }
}

void MainWindow::showRecordingError(const std::string& detail)
{
  recordingState_ = recordingSink_
    ? RecordingUiState::idle : RecordingUiState::unavailable;
  recordingPath_.clear();
  updateRecordingAction();
  statusBar()->showMessage(tr("Recording failed"), 5'000);
  dialogService_->showError(
    this, tr("Lossless Recording Error"), QString::fromStdString(detail));
}

void MainWindow::requestRecordingToggle(bool enabled)
{
  if (enabled) {
    if (recordingState_ != RecordingUiState::idle || !isGameLoaded() || gameLoading_ ||
        !applicationPathsAvailable_ || !recordingSink_) {
      updateRecordingAction();
      return;
    }
    const auto directory = dialogService_->chooseRecordingDirectory(
      this, applicationPaths_.recordingsDirectory());
    if (!directory) {
      updateRecordingAction();
      return;
    }
    recordingState_ = RecordingUiState::starting;
    recordingPath_ = *directory;
    updateRecordingAction();
    statusBar()->showMessage(tr("Starting lossless recording…"));
    if (!recordingSink_(true, *directory)) {
      recordingState_ = RecordingUiState::idle;
      recordingPath_.clear();
      updateRecordingAction();
    }
    return;
  }
  if (recordingState_ != RecordingUiState::recording || !recordingSink_) {
    updateRecordingAction();
    return;
  }
  recordingState_ = RecordingUiState::stopping;
  updateRecordingAction();
  statusBar()->showMessage(tr("Finalizing lossless recording…"));
  if (!recordingSink_(false, {})) {
    recordingState_ = RecordingUiState::recording;
    updateRecordingAction();
  }
}

void MainWindow::updateRecordingAction()
{
  auto* action = findChild<QAction*>(QStringLiteral("recordingAction"));
  if (action == nullptr) {
    return;
  }
  const QSignalBlocker blocker{action};
  const bool recording = recordingState_ == RecordingUiState::recording ||
    recordingState_ == RecordingUiState::stopping;
  action->setChecked(recording);
  action->setText(recording
      ? tr("Stop Lossless A/V &Recording")
      : tr("Start Lossless A/V &Recording…"));
  const bool canStart = recordingState_ == RecordingUiState::idle &&
    isGameLoaded() && !gameLoading_ && !sessionResumeBusy_ &&
    applicationPathsAvailable_ &&
    static_cast<bool>(recordingSink_);
  action->setEnabled(canStart || recordingState_ == RecordingUiState::recording);
  const bool transitionBusy = recordingState_ == RecordingUiState::starting ||
    recordingState_ == RecordingUiState::stopping;
  const bool fileOperationReady = !gameLoading_ && !sessionResumeBusy_ &&
    !stateOperationBusy_ && !transitionBusy;
  findChild<QAction*>(QStringLiteral("openGameAction"))->setEnabled(
    fileOperationReady);
  findChild<QAction*>(QStringLiteral("openGameWithPatchAction"))->setEnabled(
    fileOperationReady);
  findChild<QMenu*>(QStringLiteral("openRecentMenu"))->setEnabled(
    hasRecentGames_ && fileOperationReady);
  findChild<QAction*>(QStringLiteral("closeGameAction"))->setEnabled(
    isGameLoaded() && fileOperationReady);
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
      if (inputConfigurationSink_) {
        const auto status = inputConfigurationSink_(configuration);
        if (!status) {
          statusBar()->showMessage(
            tr("Input configuration could not be saved."), 5'000);
          dialogService_->showError(this, tr("Input Configuration Error"),
            QString::fromStdString(status.message));
          return false;
        }
      }
      setInputConfiguration(configuration);
      statusBar()->showMessage(tr("Input configuration saved."), 3'000);
      return true;
    });
  dialog->setAssignmentSink(
    [this](std::uint32_t instanceId, std::size_t player) {
      if (controllerAssignmentSink_) {
        const auto status = controllerAssignmentSink_(instanceId, player);
        if (!status) {
          statusBar()->showMessage(
            tr("Controller assignment could not be applied."), 5'000);
          dialogService_->showError(this, tr("Controller Assignment Error"),
            QString::fromStdString(status.message));
          return false;
        }
      }
      return true;
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
      refreshSettingsDialog();
      statusBar()->showMessage(tr("Appearance settings applied."), 3'000);
      return PersistenceStatus{};
    });
  dialog->open();
}

void MainWindow::setApplicationPaths(ApplicationPaths paths)
{
  applicationPathsAvailable_ = !paths.root().empty();
  applicationPaths_ = std::move(paths);
  updateRecordingAction();
  refreshSettingsDialog();
}

void MainWindow::showSettings(SettingsPage page)
{
  if (auto* existing = findChild<SettingsDialog*>(
        QStringLiteral("settingsDialog"))) {
    existing->setOverview(settingsOverview());
    existing->openPage(page);
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new SettingsDialog(settingsOverview(), this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setActionSink([this](SettingsPageAction action) {
    switch (action) {
      case SettingsPageAction::appearance:
        showAppearanceSettings();
        break;
      case SettingsPageAction::video:
        showVideoSettings();
        break;
      case SettingsPageAction::audio:
        showAudioSettings();
        break;
      case SettingsPageAction::inputBindings:
        showInputConfiguration(InputConfigurationTab::bindings);
        break;
      case SettingsPageAction::playerAssignments:
        showInputConfiguration(InputConfigurationTab::assignments);
        break;
      case SettingsPageAction::system:
        showSystemSettings();
        break;
      case SettingsPageAction::bios:
        showBiosSettings();
        break;
      case SettingsPageAction::screenshotPath:
        showScreenshotSettings();
        break;
      case SettingsPageAction::gameLibrary:
        showGameLibrary();
        break;
      case SettingsPageAction::diagnostics:
        showDiagnostics();
        break;
      case SettingsPageAction::perGame:
        showPerGameSettings();
        break;
      case SettingsPageAction::rewind:
        showRewindSettings();
        break;
      case SettingsPageAction::session:
        showSessionSettings();
        break;
      case SettingsPageAction::speed:
        showSpeedSettings();
        break;
    }
  });
  dialog->openPage(page);
  dialog->open();
}

void MainWindow::setRewindSettings(RewindConfiguration settings)
{
  if (!validateRewindConfiguration(settings)) {
    return;
  }
  rewindSettings_ = settings;
  if (!rewindSettings_.enabled) {
    rewindAvailable_ = false;
    rewindActive_ = false;
    rewindHeld_ = false;
    rewindToggled_ = false;
  }
  updateEmulationControls();
  refreshSettingsDialog();
}

void MainWindow::setRewindSettingsSink(RewindSettingsSink sink)
{
  rewindSettingsSink_ = std::move(sink);
}

const RewindConfiguration& MainWindow::rewindSettings() const noexcept
{
  return rewindSettings_;
}

void MainWindow::showRewindSettings()
{
  if (auto* existing = findChild<RewindSettingsDialog*>(
        QStringLiteral("rewindSettingsDialog"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new RewindSettingsDialog(rewindSettings_, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setSettingsSink([this](const RewindConfiguration& settings) {
    if (rewindSettingsSink_) {
      const auto saved = rewindSettingsSink_(settings);
      if (!saved) {
        return saved;
      }
    }
    setRewindSettings(settings);
    statusBar()->showMessage(tr("Rewind settings applied."), 3'000);
    return PersistenceStatus{};
  });
  dialog->open();
}

void MainWindow::setSpeedSettings(EmulationSpeedConfiguration settings)
{
  static_cast<void>(applySpeedSettings(settings, false));
}

void MainWindow::setSpeedSettingsSink(SpeedSettingsSink sink)
{
  speedSettingsSink_ = std::move(sink);
}

const EmulationSpeedConfiguration& MainWindow::speedSettings() const noexcept
{
  return speedSettings_;
}

void MainWindow::showSpeedSettings()
{
  if (auto* existing = findChild<SpeedSettingsDialog*>(
        QStringLiteral("speedSettingsDialog"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new SpeedSettingsDialog(speedSettings_, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setSettingsSink([this](const EmulationSpeedConfiguration& settings) {
    if (speedSettingsSink_) {
      const auto saved = speedSettingsSink_(settings);
      if (!saved) {
        return saved;
      }
    }
    return applySpeedSettings(settings, false)
      ? PersistenceStatus{}
      : PersistenceStatus{
          .error = PersistenceError::invalidData,
          .message = "The emulation speed settings are invalid.",
        };
  });
  dialog->open();
}

bool MainWindow::applySpeedSettings(
  const EmulationSpeedConfiguration& settings,
  bool notifySink)
{
  if (!validateEmulationSpeedConfiguration(settings)) {
    return false;
  }
  if (notifySink && speedSettingsSink_) {
    const auto saved = speedSettingsSink_(settings);
    if (!saved) {
      statusBar()->showMessage(tr("Speed settings could not be applied."), 5'000);
      dialogService_->showError(
        this, tr("Emulation Speed Error"), QString::fromStdString(saved.message));
      updateSpeedActionChecks();
      return false;
    }
  }
  speedSettings_ = settings;
  if (!fastForwardActive_ && !slowMotionActive_) {
    activeSpeedPercent_ = speedSettings_.normalPercent;
  }
  updateSpeedActionChecks();
  updateEmulationControls();
  refreshSettingsDialog();
  statusBar()->showMessage(
    tr("Normal emulation speed: %1%").arg(speedSettings_.normalPercent), 3'000);
  return true;
}

void MainWindow::updateSpeedActionChecks()
{
  for (const auto percent : speedPresets) {
    if (auto* action = findChild<QAction*>(
          QStringLiteral("emulationSpeed%1Action").arg(percent))) {
      const QSignalBlocker blocker{action};
      action->setChecked(speedSettings_.normalPercent == percent);
    }
  }
}

void MainWindow::setSessionSettings(settings::SessionSettings settings)
{
  if (!settings::validateSessionSettings(settings)) {
    return;
  }
  sessionSettings_ = std::move(settings);
  if (auto* dialog = findChild<SessionSettingsDialog*>(
        QStringLiteral("sessionSettingsDialog"))) {
    dialog->setResumeOnLaunch(sessionSettings_.resumeOnLaunch);
  }
  refreshSettingsDialog();
}

void MainWindow::setSessionSettingsSink(SessionSettingsSink sink)
{
  sessionSettingsSink_ = std::move(sink);
}

const settings::SessionSettings& MainWindow::sessionSettings() const noexcept
{
  return sessionSettings_;
}

void MainWindow::showSessionSettings()
{
  if (auto* existing = findChild<SessionSettingsDialog*>(
        QStringLiteral("sessionSettingsDialog"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new SessionSettingsDialog(
    sessionSettings_.resumeOnLaunch, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setSettingsSink([this](bool enabled) {
    if (sessionSettingsSink_) {
      const auto saved = sessionSettingsSink_(enabled);
      if (!saved) {
        return saved;
      }
    }
    auto updated = sessionSettings_;
    updated.resumeOnLaunch = enabled;
    if (!enabled) {
      updated.lastGamePath.reset();
      updated.lastPatchPath.reset();
    }
    setSessionSettings(std::move(updated));
    statusBar()->showMessage(tr("Session settings applied."), 3'000);
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

void MainWindow::setDebugRequestSink(DebugRequestSink sink)
{
  debugRequestSink_ = std::move(sink);
}

void MainWindow::showDebugTools()
{
  if (!appearanceSettings_.developerToolsEnabled) {
    return;
  }
  if (auto* existing = findChild<DebugToolsWindow*>(
        QStringLiteral("debugToolsWindow"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* window = new DebugToolsWindow(this);
  window->setRequestSink([this](CoreDebugRequest request) {
    return debugRequestSink_ && debugRequestSink_(std::move(request));
  });
  window->setControlSink([this](DebugControlOperation operation) {
    switch (operation) {
      case DebugControlOperation::pause:
        static_cast<void>(requestEmulationControl(EmulationUiOperation::pause));
        break;
      case DebugControlOperation::resume:
        static_cast<void>(requestEmulationControl(EmulationUiOperation::resume));
        break;
      case DebugControlOperation::frameAdvance:
        static_cast<void>(requestEmulationControl(
          EmulationUiOperation::frameAdvance));
        break;
      case DebugControlOperation::hardReset:
        static_cast<void>(requestEmulationControl(EmulationUiOperation::hardReset));
        break;
      case DebugControlOperation::softReset:
        static_cast<void>(requestEmulationControl(EmulationUiOperation::softReset));
        break;
    }
  });
  window->setStateSink(
    [this](DebugStateOperation operation, std::uint32_t slot) {
      setSelectedStateSlot(slot);
      switch (operation) {
        case DebugStateOperation::save:
          requestStateOperation(StateUiOperation::save);
          break;
        case DebugStateOperation::load:
          requestStateOperation(StateUiOperation::load);
          break;
        case DebugStateOperation::remove:
          requestStateOperation(StateUiOperation::remove);
          break;
      }
    });
  window->setGameLoaded(isGameLoaded());
  window->setPaused(emulationPaused_);
  window->show();
}

void MainWindow::presentDebugResponse(CoreDebugResponse response)
{
  if (auto* debugger = findChild<DebugToolsWindow*>(
        QStringLiteral("debugToolsWindow"))) {
    debugger->presentResponse(std::move(response));
  }
}

void MainWindow::showDebugRequestError(const std::string& detail)
{
  if (auto* debugger = findChild<DebugToolsWindow*>(
        QStringLiteral("debugToolsWindow"))) {
    debugger->showRequestError(detail);
  }
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
    return applyVideoSettings(settings, true);
  });
  dialog->setPresetChooser([this](const std::filesystem::path& initial) {
    return dialogService_->chooseShaderPreset(this, initial);
  });
  dialog->open();
}

void MainWindow::chooseShaderPreset()
{
  const auto initialDirectory = videoSettings_.shader.presetPath.has_parent_path()
    ? videoSettings_.shader.presetPath.parent_path()
    : std::filesystem::path{};
  const auto selected = dialogService_->chooseShaderPreset(
    this, initialDirectory);
  if (!selected) {
    return;
  }
  auto settings = videoSettings_;
  settings.shader = {
    .mode = video::ShaderMode::libretroPreset,
    .presetPath = *selected,
    .parameters = {},
  };
  if (applyVideoSettings(settings, true)) {
    statusBar()->showMessage(tr("Libretro shader preset loaded."), 3'000);
  }
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

void MainWindow::showAudioSettingsError(const std::string& detail)
{
  statusBar()->showMessage(tr("Audio settings could not be applied."), 5'000);
  dialogService_->showError(
    this, tr("Audio Settings Error"), QString::fromStdString(detail));
}

void MainWindow::showAudioOutputError(const std::string& detail)
{
  statusBar()->showMessage(tr("Audio output is unavailable."), 8'000);
  dialogService_->showError(
    this, tr("Audio Output Unavailable"), QString::fromStdString(detail));
}

void MainWindow::showEmulationRuntimeError(const std::string& detail)
{
  statusBar()->showMessage(tr("Emulation service operation failed."), 8'000);
  dialogService_->showError(
    this, tr("Emulation Service Error"), QString::fromStdString(detail));
}

void MainWindow::showStartupIssues(std::vector<std::string> issues)
{
  QStringList uniqueIssues;
  for (const auto& issue : issues) {
    const auto text = QString::fromStdString(issue).trimmed();
    if (!text.isEmpty() && !uniqueIssues.contains(text)) {
      uniqueIssues.push_back(text);
    }
  }
  if (uniqueIssues.isEmpty()) {
    return;
  }

  constexpr qsizetype maximumVisibleIssues = 12;
  QStringList bullets;
  const auto visibleCount = std::min(uniqueIssues.size(), maximumVisibleIssues);
  bullets.reserve(visibleCount + 1);
  for (qsizetype index = 0; index < visibleCount; ++index) {
    bullets.push_back(QStringLiteral("• ") + uniqueIssues.at(index));
  }
  if (uniqueIssues.size() > visibleCount) {
    bullets.push_back(tr("• %1 additional issue(s); see the diagnostics log.")
      .arg(uniqueIssues.size() - visibleCount));
  }
  const auto message = tr(
    "The application started, but some features are unavailable or using safe "
    "defaults. You can continue using unaffected features.\n\n%1")
    .arg(bullets.join(u'\n'));
  qWarning().noquote() << "Startup issues presented:"
                       << uniqueIssues.size();
  statusBar()->showMessage(tr("Startup completed with issues."), 10'000);
  dialogService_->showError(this, tr("Startup Issues"), message);
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
    if (systemSettingsSink_) {
      const auto status = systemSettingsSink_(settings);
      if (!status) {
        statusBar()->showMessage(
          tr("System settings could not be saved."), 5'000);
        dialogService_->showError(this, tr("System Settings Error"),
          QString::fromStdString(status.message));
        return false;
      }
    }
    setSystemSettings(settings);
    statusBar()->showMessage(
      tr("System settings will apply when the next game is loaded."), 3'000);
    return true;
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
  if (auto* menu = findChild<QMenu*>(QStringLiteral("developerToolsMenu"))) {
    menu->menuAction()->setVisible(value.developerToolsEnabled);
  }
  if (!value.developerToolsEnabled) {
    if (auto* debugger = findChild<DebugToolsWindow*>(
          QStringLiteral("debugToolsWindow"))) {
      debugger->close();
    }
  }
  if (auto* dialog = findChild<AppearanceSettingsDialog*>(
        QStringLiteral("appearanceSettingsDialog"))) {
    dialog->setSettings(appearanceSettings_);
  }
  refreshSettingsDialog();
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
  if (auto* dialog = findChild<AudioSettingsDialog*>(
        QStringLiteral("audioSettingsDialog"))) {
    dialog->setAvailableDevices(availableAudioDevices_);
  }
  refreshSettingsDialog();
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
  refreshSettingsDialog();
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
  refreshSettingsDialog();
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
  playlistDiscIndex_.reset();
  if (segaCdSession_) {
    const auto playlistDisc = std::ranges::find(
      loadedGameTarget_.playlistDiscs, currentDiscPath_);
    if (playlistDisc != loadedGameTarget_.playlistDiscs.end()) {
      playlistDiscIndex_ = static_cast<std::size_t>(
        std::distance(loadedGameTarget_.playlistDiscs.begin(), playlistDisc));
    }
  }
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
    playlistDiscIndex_.reset();
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

bool MainWindow::applyVideoSettings(
  const settings::VideoSettings& settings,
  bool notifySink)
{
  if (!settings::validateVideoSettings(settings)) {
    return false;
  }
  if (settings.shader.mode != video::ShaderMode::disabled &&
      settings.shader != videoSettings_.shader) {
    const auto inspection = video::inspectShaderConfiguration(settings.shader);
    if (!inspection.success) {
      updateVideoActionChecks();
      dialogService_->showError(this, tr("Libretro Shader Error"),
        QString::fromStdString(inspection.error));
      return false;
    }
  }
  if (notifySink && videoSettingsSink_) {
    const auto status = videoSettingsSink_(settings);
    if (!status) {
      updateVideoActionChecks();
      statusBar()->showMessage(tr("Video settings could not be saved."), 5'000);
      dialogService_->showError(this, tr("Video Settings Error"),
        QString::fromStdString(status.message));
      return false;
    }
  }
  videoSettings_ = settings;
  displayWidget_->setAspectMode(settings.aspect);
  displayWidget_->setScaleMode(settings.scaling);
  displayWidget_->setVideoFilter(settings.presentationFilter);
  displayWidget_->setShaderConfiguration(settings.shader);
  updateVideoActionChecks();
  if (auto* dialog = findChild<VideoSettingsDialog*>(
        QStringLiteral("videoSettingsDialog"))) {
    dialog->setSettings(videoSettings_);
  }
  refreshSettingsDialog();
  return true;
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
  findChild<QAction*>(QStringLiteral("shaderDisabledAction"))->setChecked(
    videoSettings_.shader.mode == video::ShaderMode::disabled);
  findChild<QAction*>(QStringLiteral("builtinCrtShaderAction"))->setChecked(
    videoSettings_.shader.mode == video::ShaderMode::builtinCrt);
  auto* customShader = findChild<QAction*>(QStringLiteral("customShaderAction"));
  customShader->setChecked(
    videoSettings_.shader.mode == video::ShaderMode::libretroPreset);
  customShader->setEnabled(!videoSettings_.shader.presetPath.empty());
  customShader->setText(videoSettings_.shader.presetPath.empty()
    ? tr("Custom Libretro Preset")
    : tr("Custom: %1").arg(pathToQString(
        videoSettings_.shader.presetPath.filename())));
  findChild<QAction*>(QStringLiteral("shaderParametersAction"))->setEnabled(
    videoSettings_.shader.mode != video::ShaderMode::disabled);

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
  refreshSettingsDialog();
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

SettingsOverview MainWindow::settingsOverview() const
{
  return {
    .appearance = appearanceSettings_,
    .video = videoSettings_,
    .audio = audioSettings_,
    .input = inputConfiguration_,
    .system = systemSettings_,
    .bios = biosSnapshot_,
    .screenshots = screenshotSettings_,
    .rewind = rewindSettings_,
    .session = sessionSettings_,
    .speed = speedSettings_,
    .paths = applicationPaths_,
    .connectedControllerCount = controllers_.size(),
    .pathsAvailable = applicationPathsAvailable_,
    .gameLoaded = isGameLoaded(),
  };
}

void MainWindow::refreshSettingsDialog()
{
  if (auto* dialog = findChild<SettingsDialog*>(
        QStringLiteral("settingsDialog"))) {
    dialog->setOverview(settingsOverview());
  }
}

void MainWindow::setInputConfiguration(input::InputConfiguration configuration)
{
  if (input::validateInputConfiguration(configuration)) {
    setFastForwardHeld(false);
    setSlowMotionHeld(false);
    setRewindHeld(false);
    inputConfiguration_ = std::move(configuration);
    applyHotkeyShortcuts();
    refreshSettingsDialog();
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
  refreshSettingsDialog();
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

void MainWindow::setArchiveCacheDirectory(std::filesystem::path directory)
{
  archiveCacheDirectory_ = std::move(directory);
}

void MainWindow::setPatchCacheDirectory(std::filesystem::path directory)
{
  patchCacheDirectory_ = std::move(directory);
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
  if (stateManagerDialog_ != nullptr) {
    stateManagerDialog_->setSessionReady(stateSessionReady_);
  }
  updateStateActions();
}

void MainWindow::setStateOperationBusy(bool busy)
{
  stateOperationBusy_ = busy;
  if (stateManagerDialog_ != nullptr) {
    stateManagerDialog_->setBusy(stateOperationBusy_ || sessionResumeBusy_);
  }
  updateStateActions();
  updateStateSlotPresentation();
}

void MainWindow::setStateSlotViews(std::array<StateSlotView, 10> views)
{
  for (std::uint32_t slot = 0U; slot < views.size(); ++slot) {
    views[slot].slot = slot;
  }
  stateSlotViews_ = std::move(views);
  if (stateManagerDialog_ != nullptr) {
    stateManagerDialog_->setViews(stateSlotViews_);
  }
  updateStateSlotPresentation();
  updateStateActions();
}

void MainWindow::setSelectedStateSlot(std::uint32_t slot)
{
  if (slot > 9U) {
    return;
  }
  selectedStateSlot_ = slot;
  if (stateManagerDialog_ != nullptr &&
      stateManagerDialog_->selectedSlot() != slot) {
    stateManagerDialog_->setSelectedSlot(slot);
  }
  updateStateSlotPresentation();
  updateStateActions();
}

std::uint32_t MainWindow::selectedStateSlot() const noexcept
{
  return selectedStateSlot_;
}

void MainWindow::showStateManager()
{
  if (stateManagerDialog_ == nullptr) {
    stateManagerDialog_ = new StateManagerDialog(this);
    stateManagerDialog_->setObjectName(QStringLiteral("stateManagerDialog"));
    stateManagerDialog_->setOperationSink([this](StateUiRequest request) {
      requestStateOperation(
        request.operation, std::move(request.path), std::move(request.name));
    });
    stateManagerDialog_->setSelectionSink([this](std::uint32_t slot) {
      setSelectedStateSlot(slot);
    });
  }
  stateManagerDialog_->setViews(stateSlotViews_);
  stateManagerDialog_->setSelectedSlot(selectedStateSlot_);
  stateManagerDialog_->setSessionReady(stateSessionReady_ && isGameLoaded());
  stateManagerDialog_->setBusy(stateOperationBusy_ || sessionResumeBusy_);
  stateManagerDialog_->show();
  stateManagerDialog_->raise();
  stateManagerDialog_->activateWindow();
}

std::vector<std::uint8_t> MainWindow::captureStateThumbnailPng() const
{
  if (displayWidget_ == nullptr || !displayWidget_->hasFrame()) {
    return {};
  }
  const auto& frame = displayWidget_->currentFrameInfo();
  const auto pixels = displayWidget_->currentPixels();
  if (frame.format != CorePixelFormat::rgb565 || frame.width == 0U ||
      frame.height == 0U || pixels.size() < frame.pixelCount()) {
    return {};
  }
  const QImage source{
    reinterpret_cast<const uchar*>(pixels.data()),
    static_cast<int>(frame.width),
    static_cast<int>(frame.height),
    static_cast<int>(frame.width) * static_cast<int>(sizeof(std::uint16_t)),
    QImage::Format_RGB16};
  const auto thumbnail = source.copy().scaled(
    QSize{256, 192}, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  QByteArray encoded;
  QBuffer buffer{&encoded};
  if (!buffer.open(QIODevice::WriteOnly) || !thumbnail.save(&buffer, "PNG") ||
      encoded.size() > 512 * 1024) {
    return {};
  }
  return std::vector<std::uint8_t>{
    reinterpret_cast<const std::uint8_t*>(encoded.constData()),
    reinterpret_cast<const std::uint8_t*>(encoded.constData()) +
      static_cast<std::size_t>(encoded.size())};
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
    case StateUiOperation::importFile:
      action = tr("imported");
      break;
    case StateUiOperation::exportFile:
      action = tr("exported");
      break;
    case StateUiOperation::rename:
      action = tr("renamed");
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
    case StateUiOperation::importFile:
      action = tr("Import State Failed");
      break;
    case StateUiOperation::exportFile:
      action = tr("Export State Failed");
      break;
    case StateUiOperation::rename:
      action = tr("Rename State Failed");
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
        QTimer::singleShot(0, this, [this, sink] {
          const auto status = sink();
          if (!status) {
            statusBar()->showMessage(
              tr("Recent game history could not be cleared."), 5'000);
            dialogService_->showError(this, tr("Recent Games Error"),
              QString::fromStdString(status.message));
          } else {
            statusBar()->showMessage(tr("Recent game history cleared."), 3'000);
          }
        });
      }
    });
  }
  hasRecentGames_ = !paths.empty();
  menu->setEnabled(hasRecentGames_ && !gameLoading_);
  updateRecordingAction();
}

void MainWindow::showRecentGamesError(const std::string& detail)
{
  statusBar()->showMessage(
    tr("Recent game history could not be updated."), 5'000);
  dialogService_->showError(
    this, tr("Recent Games Error"), QString::fromStdString(detail));
}

bool MainWindow::requestGameLoad(
  const std::filesystem::path& path,
  std::optional<std::filesystem::path> patchPath)
{
  if (gameLoading_ || sessionResumeBusy_) {
    presentGameLoadError(path, "Another game operation is still in progress.");
    return false;
  }
  if (recordingState_ == RecordingUiState::starting ||
      recordingState_ == RecordingUiState::stopping) {
    presentGameLoadError(path,
      "Wait for the current recording operation to finish before changing games.");
    return false;
  }
  GameLaunchTarget target{
    .sourcePath = path,
    .runtimePath = path,
    .patchPath = {},
    .archiveEntry = {},
    .playlistDiscs = {},
  };
  if (hasZipArchiveExtension(path)) {
    const auto inspection = inspectZipArchive(path);
    if (!inspection.status) {
      presentGameLoadError(path, inspection.status.message);
      return false;
    }
    std::optional<std::string> entry;
    if (inspection.entries.size() == 1U) {
      entry = inspection.entries.front().name;
    } else {
      entry = dialogService_->chooseArchiveEntry(this, path, inspection.entries);
      if (!entry) {
        return false;
      }
    }
    const auto extracted = extractZipGame(path, *entry, archiveCacheDirectory_);
    if (!extracted.status) {
      presentGameLoadError(path, extracted.status.message);
      return false;
    }
    target.runtimePath = extracted.path;
    target.archiveEntry = std::move(*entry);
  } else if (hasDiscPlaylistExtension(path)) {
    DiscPlaylistInfo playlist;
    if (const auto status = validateDiscPlaylistFile(path, playlist); !status) {
      presentGameLoadError(path, status.message);
      return false;
    }
    target.runtimePath = playlist.discs.front();
    target.playlistDiscs = std::move(playlist.discs);
  } else if (const auto status = validateGameFile(path); !status) {
    presentGameLoadError(path, status.message);
    return false;
  }
  if (!patchPath && !target.isArchive() && !target.isPlaylist() &&
      !isUnambiguousDiscImage(target.runtimePath)) {
    const auto sidecar = discoverGamePatchSidecar(path);
    if (!sidecar.status) {
      presentGameLoadError(path, sidecar.status.message);
      return false;
    }
    patchPath = sidecar.path;
  }
  if (patchPath) {
    if (target.isPlaylist() || isUnambiguousDiscImage(target.runtimePath)) {
      presentGameLoadError(path,
        "Soft patches can only be applied to cartridge game images.");
      return false;
    }
    const auto patched = applyGamePatchFile(
      target.runtimePath, *patchPath, patchCacheDirectory_);
    if (!patched.status) {
      presentGameLoadError(path, patched.status.message);
      return false;
    }
    target.runtimePath = patched.path;
    target.patchPath = *patchPath;
  }
  if (!gameLoadSink_) {
    presentGameLoadError(path, "The emulation service is not available.");
    return false;
  }
  if (recordingState_ == RecordingUiState::recording) {
    recordingState_ = RecordingUiState::stopping;
    updateRecordingAction();
    statusBar()->showMessage(tr("Finalizing lossless recording…"));
    if (!recordingSink_ || !recordingSink_(false, {})) {
      recordingState_ = RecordingUiState::recording;
      updateRecordingAction();
      presentGameLoadError(path,
        "The active recording could not be stopped before replacing the game.");
      return false;
    }
  }
  pendingGameTarget_ = target;
  setGameLoading(path);
  gameLoadSink_(target);
  return true;
}

void MainWindow::setSessionResumeBusy(bool busy)
{
  sessionResumeBusy_ = busy && isGameLoaded();
  if (stateManagerDialog_ != nullptr) {
    stateManagerDialog_->setBusy(stateOperationBusy_ || sessionResumeBusy_);
  }
  setGameActionsEnabled(isGameLoaded() && !sessionResumeBusy_);
  findChild<QAction*>(QStringLiteral("openGameAction"))->setEnabled(
    !gameLoading_ && !sessionResumeBusy_);
  findChild<QAction*>(QStringLiteral("openGameWithPatchAction"))->setEnabled(
    !gameLoading_ && !sessionResumeBusy_);
  findChild<QMenu*>(QStringLiteral("openRecentMenu"))->setEnabled(
    hasRecentGames_ && !gameLoading_ && !sessionResumeBusy_);
  if (sessionResumeBusy_) {
    statusBar()->showMessage(tr("Restoring the previous session…"));
  } else if (isGameLoaded()) {
    statusBar()->showMessage(tr("Session restoration complete."), 3'000);
  }
  updateStateActions();
}

void MainWindow::setGameLoading(const std::filesystem::path& path)
{
  sessionResumeBusy_ = false;
  const bool replacingLoadedGame = !loadedGamePath_.empty();
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
  fastForwardHeld_ = false;
  fastForwardToggled_ = false;
  slowMotionActive_ = false;
  slowMotionHeld_ = false;
  slowMotionToggled_ = false;
  activeSpeedPercent_ = speedSettings_.normalPercent;
  rewindActive_ = false;
  rewindAvailable_ = false;
  rewindHeld_ = false;
  rewindToggled_ = false;
  gameInformationBusy_ = false;
  pendingGamePath_ = path;
  if (!pendingGameTarget_.valid() || pendingGameTarget_.sourcePath != path) {
    pendingGameTarget_ = {
      .sourcePath = path,
      .runtimePath = path,
      .patchPath = {},
      .archiveEntry = {},
      .playlistDiscs = {},
    };
  }
  displayWidget_->clearFrame();
  findChild<QAction*>(QStringLiteral("openGameAction"))->setEnabled(false);
  findChild<QAction*>(QStringLiteral("openGameWithPatchAction"))->setEnabled(false);
  findChild<QMenu*>(QStringLiteral("openRecentMenu"))->setEnabled(false);
  setGameActionsEnabled(false);
  gameStatus_->setText(
    tr("Loading %1…").arg(pathToQString(path.filename())));
  if (!replacingLoadedGame) {
    systemStatus_->setText(tr("System: —"));
    regionStatus_->setText(tr("Region: —"));
  }
  fpsStatus_->setText(tr("0.0 FPS"));
  speedStatus_->setText(
    tr("Speed %1%").arg(speedSettings_.normalPercent));
  statusBar()->showMessage(tr("Loading game…"));
  refreshSettingsDialog();
  if (auto* debugger = findChild<DebugToolsWindow*>(
        QStringLiteral("debugToolsWindow"))) {
    debugger->setGameLoaded(false);
  }
}

void MainWindow::setGameLoaded(const std::filesystem::path& path)
{
  setGameLoaded(GameLaunchTarget{
    .sourcePath = path,
    .runtimePath = path,
    .patchPath = {},
    .archiveEntry = {},
    .playlistDiscs = {},
  });
}

void MainWindow::setGameLoaded(const GameLaunchTarget& target)
{
  if (!target.valid()) {
    setNoGameLoaded();
    return;
  }
  if (loadedRuntimePath_ != target.runtimePath) {
    stateSessionReady_ = false;
    stateOperationBusy_ = false;
    for (std::uint32_t slot = 0U; slot < stateSlotViews_.size(); ++slot) {
      stateSlotViews_[slot] = StateSlotView{};
      stateSlotViews_[slot].slot = slot;
    }
    if (stateManagerDialog_ != nullptr) {
      stateManagerDialog_->setViews(stateSlotViews_);
      stateManagerDialog_->setSessionReady(false);
    }
  }
  loadedGameTarget_ = target;
  loadedGamePath_ = target.sourcePath;
  loadedRuntimePath_ = target.runtimePath;
  pendingGameTarget_ = {};
  pendingGamePath_.clear();
  gameLoading_ = false;
  gameInformationBusy_ = false;
  findChild<QAction*>(QStringLiteral("openGameAction"))->setEnabled(true);
  findChild<QAction*>(QStringLiteral("openGameWithPatchAction"))->setEnabled(true);
  findChild<QMenu*>(QStringLiteral("openRecentMenu"))->setEnabled(hasRecentGames_);
  setGameActionsEnabled(true);
  updateStateSlotPresentation();
  auto gameDescription = target.isArchive()
    ? tr("%1 — %2")
        .arg(pathToQString(target.sourcePath.filename()),
          QString::fromUtf8(target.archiveEntry))
    : pathToQString(target.sourcePath.filename());
  if (target.isPatched()) {
    gameDescription += tr(" — %1 patch")
      .arg(pathToQString(target.patchPath.filename()));
  }
  gameStatus_->setText(gameDescription);
  statusBar()->showMessage(tr("Game loaded"), 3000);
  refreshSettingsDialog();
  if (auto* debugger = findChild<DebugToolsWindow*>(
        QStringLiteral("debugToolsWindow"))) {
    debugger->setGameLoaded(true);
    debugger->setPaused(emulationPaused_);
  }
}

void MainWindow::setGameRuntimeIdentity(std::string system, std::string region)
{
  if (!isGameLoaded()) {
    return;
  }
  if (system.empty()) {
    system = "Unknown";
  }
  if (region.empty()) {
    region = "Unknown";
  }
  systemStatus_->setText(
    tr("System: %1").arg(QString::fromStdString(system)));
  regionStatus_->setText(
    tr("Region: %1").arg(QString::fromStdString(region)));
}

void MainWindow::setMeasuredFrameRate(double framesPerSecond)
{
  if (!isGameLoaded()) {
    return;
  }
  if (!std::isfinite(framesPerSecond) || framesPerSecond < 0.0) {
    framesPerSecond = 0.0;
  }
  fpsStatus_->setText(tr("%1 FPS").arg(framesPerSecond, 0, 'f', 1));
}

void MainWindow::setNominalVideoRate(double framesPerSecond)
{
  if (displayWidget_ != nullptr) {
    displayWidget_->setSourceFramesPerSecond(framesPerSecond);
  }
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
  loadedRuntimePath_.clear();
  loadedGameTarget_ = {};
  pendingGameTarget_ = {};
  pendingGamePath_.clear();
  gameLoading_ = false;
  gameInformationBusy_ = false;
  stateSessionReady_ = false;
  stateOperationBusy_ = false;
  sessionResumeBusy_ = false;
  emulationPaused_ = false;
  fastForwardActive_ = false;
  fastForwardHeld_ = false;
  fastForwardToggled_ = false;
  slowMotionActive_ = false;
  slowMotionHeld_ = false;
  slowMotionToggled_ = false;
  activeSpeedPercent_ = speedSettings_.normalPercent;
  rewindActive_ = false;
  rewindAvailable_ = false;
  rewindHeld_ = false;
  rewindToggled_ = false;
  segaCdSession_ = false;
  discEjected_ = false;
  discPresent_ = false;
  discOperationBusy_ = false;
  discRegion_.clear();
  currentDiscPath_.clear();
  playlistDiscIndex_.reset();
  for (std::uint32_t slot = 0U; slot < stateSlotViews_.size(); ++slot) {
    stateSlotViews_[slot] = StateSlotView{};
    stateSlotViews_[slot].slot = slot;
  }
  if (stateManagerDialog_ != nullptr) {
    stateManagerDialog_->setViews(stateSlotViews_);
    stateManagerDialog_->setSessionReady(false);
    stateManagerDialog_->setBusy(false);
  }
  findChild<QAction*>(QStringLiteral("openGameAction"))->setEnabled(true);
  findChild<QAction*>(QStringLiteral("openGameWithPatchAction"))->setEnabled(true);
  findChild<QMenu*>(QStringLiteral("openRecentMenu"))->setEnabled(hasRecentGames_);
  setGameActionsEnabled(false);
  gameStatus_->setText(tr("No game loaded"));
  systemStatus_->setText(tr("System: —"));
  regionStatus_->setText(tr("Region: —"));
  fpsStatus_->setText(tr("0.0 FPS"));
  speedStatus_->setText(
    tr("Speed %1%").arg(speedSettings_.normalPercent));
  displayWidget_->clearFrame();
  updateStateSlotPresentation();
  refreshSettingsDialog();
  if (auto* debugger = findChild<DebugToolsWindow*>(
        QStringLiteral("debugToolsWindow"))) {
    debugger->setGameLoaded(false);
  }
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

void MainWindow::requestPlaylistDisc(int direction)
{
  if (!segaCdSession_ || discOperationBusy_ || !discOperationSink_ ||
      !playlistDiscIndex_ || direction == 0 ||
      loadedGameTarget_.playlistDiscs.size() < 2U) {
    updateDiscActions();
    return;
  }
  const auto current = static_cast<std::ptrdiff_t>(*playlistDiscIndex_);
  const auto next = current + static_cast<std::ptrdiff_t>(direction);
  if (next < 0 || next >= static_cast<std::ptrdiff_t>(
        loadedGameTarget_.playlistDiscs.size())) {
    updateDiscActions();
    return;
  }
  const auto& path = loadedGameTarget_.playlistDiscs[
    static_cast<std::size_t>(next)];
  if (const auto status = validateDiscImageFile(path); !status) {
    showDiscOperationError(DiscUiOperation::change, status.message);
    return;
  }
  setDiscOperationBusy(true);
  discOperationSink_(DiscUiOperation::change, path, false);
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
  auto* previous = findChild<QAction*>(QStringLiteral("previousDiscAction"));
  auto* next = findChild<QAction*>(QStringLiteral("nextDiscAction"));
  if (change == nullptr || eject == nullptr || previous == nullptr ||
      next == nullptr) {
    return;
  }
  const bool available = isGameLoaded() && !sessionResumeBusy_ &&
    segaCdSession_ && !discOperationBusy_;
  change->setEnabled(available);
  eject->setEnabled(available);
  const bool playlistAvailable = available && playlistDiscIndex_.has_value();
  previous->setEnabled(playlistAvailable && *playlistDiscIndex_ > 0U);
  next->setEnabled(playlistAvailable &&
    *playlistDiscIndex_ + 1U < loadedGameTarget_.playlistDiscs.size());
  const auto playlistPosition = playlistDiscIndex_
    ? tr("Disc %1 of %2")
        .arg(*playlistDiscIndex_ + 1U)
        .arg(loadedGameTarget_.playlistDiscs.size())
    : tr("No active M3U playlist");
  previous->setToolTip(playlistPosition);
  next->setToolTip(playlistPosition);
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
  const auto previousGame = loadedGameTarget_;
  if (gameWasUnloaded || !previousGame.valid()) {
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

const std::filesystem::path& MainWindow::loadedRuntimePath() const noexcept
{
  return loadedRuntimePath_;
}

const GameLaunchTarget& MainWindow::loadedGameTarget() const noexcept
{
  return loadedGameTarget_;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
  const auto urls = event->mimeData()->urls();
  const auto validSingle = urls.size() == 1 && urls.front().isLocalFile() &&
    hasSupportedGameExtension(pathFromQString(urls.front().toLocalFile()));
  const auto validPair = urls.size() == 2 && urls[0].isLocalFile() &&
    urls[1].isLocalFile() &&
    ((hasSupportedGameExtension(pathFromQString(urls[0].toLocalFile())) &&
      hasSupportedGamePatchExtension(pathFromQString(urls[1].toLocalFile()))) ||
     (hasSupportedGameExtension(pathFromQString(urls[1].toLocalFile())) &&
      hasSupportedGamePatchExtension(pathFromQString(urls[0].toLocalFile()))));
  if (!gameLoading_ && (validSingle || validPair)) {
    event->acceptProposedAction();
  }
}

void MainWindow::dropEvent(QDropEvent* event)
{
  const auto urls = event->mimeData()->urls();
  if (urls.size() == 2 && urls[0].isLocalFile() && urls[1].isLocalFile()) {
    auto first = pathFromQString(urls[0].toLocalFile());
    auto second = pathFromQString(urls[1].toLocalFile());
    if (hasSupportedGamePatchExtension(first)) {
      std::swap(first, second);
    }
    if (requestGameLoad(first, second)) {
      event->acceptProposedAction();
    }
    return;
  }
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

void MainWindow::chooseGameWithPatch()
{
  const auto initialDirectory = loadedGamePath_.empty()
    ? std::filesystem::path{}
    : loadedGamePath_.parent_path();
  const auto game = dialogService_->chooseGame(this, initialDirectory);
  if (!game) {
    return;
  }
  const auto patch = dialogService_->choosePatch(this, game->parent_path());
  if (patch) {
    static_cast<void>(requestGameLoad(*game, *patch));
  }
}

void MainWindow::closeGame()
{
  if (recordingState_ == RecordingUiState::starting ||
      recordingState_ == RecordingUiState::stopping) {
    return;
  }
  if (gameCloseSink_ && isGameLoaded()) {
    if (recordingState_ == RecordingUiState::recording) {
      recordingState_ = RecordingUiState::stopping;
      updateRecordingAction();
      statusBar()->showMessage(tr("Finalizing lossless recording…"));
      if (!recordingSink_ || !recordingSink_(false, {})) {
        recordingState_ = RecordingUiState::recording;
        updateRecordingAction();
        statusBar()->showMessage(tr("Recording could not be stopped"), 5'000);
        dialogService_->showError(this, tr("Lossless Recording Error"),
          tr("The active recording could not be stopped before closing the game."));
        return;
      }
    }
    gameLoading_ = true;
    findChild<QAction*>(QStringLiteral("openGameAction"))->setEnabled(false);
    findChild<QAction*>(QStringLiteral("openGameWithPatchAction"))->setEnabled(false);
    findChild<QMenu*>(QStringLiteral("openRecentMenu"))->setEnabled(false);
    setGameActionsEnabled(false);
    gameStatus_->setText(tr("Closing game…"));
    gameCloseSink_();
  }
}

void MainWindow::requestStateOperation(
  StateUiOperation operation,
  std::filesystem::path path,
  std::string name)
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
  if ((operation == StateUiOperation::exportFile ||
       operation == StateUiOperation::rename) &&
      state != StateSlotViewState::available) {
    return;
  }
  if (operation == StateUiOperation::importFile && path.empty()) {
    const auto selected = dialogService_->chooseStateImport(
      this, loadedGamePath_.parent_path());
    if (!selected) {
      return;
    }
    path = *selected;
  } else if (operation == StateUiOperation::exportFile && path.empty()) {
    const auto suggested = applicationPaths_.statesDirectory() /
      ("state-slot-" + std::to_string(selectedStateSlot_) + ".gpgxstate");
    const auto selected = dialogService_->chooseStateExport(this, suggested);
    if (!selected) {
      return;
    }
    path = *selected;
  }
  if (operation == StateUiOperation::save && name.empty()) {
    name = stateSlotViews_[selectedStateSlot_].name;
  }
  setStateOperationBusy(true);
  stateOperationSink_({
    .operation = operation,
    .slot = selectedStateSlot_,
    .path = std::move(path),
    .name = std::move(name),
  });
}

void MainWindow::updateStateActions()
{
  const bool ready = isGameLoaded() && !sessionResumeBusy_ &&
    stateSessionReady_ && !stateOperationBusy_;
  const auto selectedState = stateSlotViews_[selectedStateSlot_].state;
  findChild<QAction*>(QStringLiteral("saveStateAction"))->setEnabled(ready);
  findChild<QAction*>(QStringLiteral("loadStateAction"))->setEnabled(
    ready && selectedState == StateSlotViewState::available);
  findChild<QAction*>(QStringLiteral("deleteStateAction"))->setEnabled(
    ready && selectedState != StateSlotViewState::empty);
  findChild<QAction*>(QStringLiteral("importStateAction"))->setEnabled(ready);
  findChild<QAction*>(QStringLiteral("exportStateAction"))->setEnabled(
    ready && selectedState == StateSlotViewState::available);
  findChild<QAction*>(QStringLiteral("stateManagerAction"))->setEnabled(ready);
  findChild<QMenu*>(QStringLiteral("stateSlotMenu"))->setEnabled(ready);
  for (std::uint32_t slot = 0U; slot < stateSlotViews_.size(); ++slot) {
    findChild<QAction*>(QStringLiteral("stateSlotAction%1").arg(slot))
      ->setEnabled(ready);
  }
  findChild<QAction*>(QStringLiteral("previousStateSlotAction"))->setEnabled(ready);
  findChild<QAction*>(QStringLiteral("nextStateSlotAction"))->setEnabled(ready);

  if (stateOperationBusy_ || sessionResumeBusy_) {
    findChild<QAction*>(QStringLiteral("openGameAction"))->setEnabled(false);
    findChild<QAction*>(QStringLiteral("openGameWithPatchAction"))->setEnabled(false);
    findChild<QAction*>(QStringLiteral("closeGameAction"))->setEnabled(false);
    findChild<QMenu*>(QStringLiteral("openRecentMenu"))->setEnabled(false);
  } else if (!gameLoading_) {
    findChild<QAction*>(QStringLiteral("openGameAction"))->setEnabled(true);
    findChild<QAction*>(QStringLiteral("openGameWithPatchAction"))->setEnabled(true);
    findChild<QAction*>(QStringLiteral("closeGameAction"))->setEnabled(
      isGameLoaded());
    findChild<QMenu*>(QStringLiteral("openRecentMenu"))->setEnabled(
      hasRecentGames_);
  }
  updateRecordingAction();
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
        const auto saved = QDateTime::fromMSecsSinceEpoch(milliseconds)
          .toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        stateText = view.name.empty()
          ? saved
          : tr("%1 (%2)").arg(QString::fromStdString(view.name), saved);
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
