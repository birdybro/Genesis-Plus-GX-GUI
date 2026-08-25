#include "genplusgx/ui/main_window.h"

#include "genplusgx/game_file.h"
#include "genplusgx/ui/about_dialog.h"
#include "genplusgx/ui/dialog_service.h"
#include "genplusgx/ui/input_configuration_dialog.h"
#include "genplusgx/version.h"
#include "genplusgx/video/display_widget.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>

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
  setGameActionsEnabled(false);
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
  addAction(*emulation, tr("&Save State"), "saveStateAction", QKeySequence{tr("F5")});
  addAction(*emulation, tr("&Load State"), "loadStateAction", QKeySequence{tr("F8")});
  auto* stateSlotMenu = emulation->addMenu(tr("State &Slot"));
  stateSlotMenu->setObjectName(QStringLiteral("stateSlotMenu"));
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
    displayWidget_->setScaleMode(video::ScaleMode::fit);
  });
  connect(integerScale, &QAction::triggered, this, [this] {
    displayWidget_->setScaleMode(video::ScaleMode::integer);
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
    displayWidget_->setAspectMode(video::AspectMode::native);
  });
  connect(fourThreeAspect, &QAction::triggered, this, [this] {
    displayWidget_->setAspectMode(video::AspectMode::fourThree);
  });
  connect(stretchAspect, &QAction::triggered, this, [this] {
    displayWidget_->setAspectMode(video::AspectMode::stretch);
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
    displayWidget_->setVideoFilter(video::VideoFilter::nearest);
  });
  connect(bilinear, &QAction::triggered, this, [this] {
    displayWidget_->setVideoFilter(video::VideoFilter::bilinear);
  });
  auto* overscan = video->addMenu(tr("&Overscan"));
  overscan->setObjectName(QStringLiteral("overscanMenu"));

  auto* audio = createMenu(tr("&Audio"), "audioMenu");
  auto* mute = addAction(*audio, tr("&Mute"), "muteAction", QKeySequence{tr("M")});
  mute->setCheckable(true);
  addAction(*audio, tr("Volume &Up"), "volumeUpAction", QKeySequence{tr("+")});
  addAction(*audio, tr("Volume &Down"), "volumeDownAction", QKeySequence{tr("-")});

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
  addAction(*tools, tr("&Settings…"), "settingsAction", QKeySequence::Preferences);
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
    "softResetAction", "fastForwardAction", "frameAdvanceAction", "saveStateAction",
    "loadStateAction", "changeDiscAction", "ejectDiscAction", "cheatsAction",
    "gameInformationAction"};
  for (const auto* name : gameActionNames) {
    findChild<QAction*>(QString::fromLatin1(name))->setEnabled(enabled);
  }
  findChild<QMenu*>(QStringLiteral("stateSlotMenu"))->setEnabled(enabled);
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
  loadedGamePath_ = path;
  pendingGamePath_.clear();
  gameLoading_ = false;
  findChild<QAction*>(QStringLiteral("openGameAction"))->setEnabled(true);
  findChild<QMenu*>(QStringLiteral("openRecentMenu"))->setEnabled(hasRecentGames_);
  setGameActionsEnabled(true);
  gameStatus_->setText(pathToQString(path.filename()));
  statusBar()->showMessage(tr("Game loaded"), 3000);
}

void MainWindow::setNoGameLoaded()
{
  loadedGamePath_.clear();
  pendingGamePath_.clear();
  gameLoading_ = false;
  findChild<QAction*>(QStringLiteral("openGameAction"))->setEnabled(true);
  findChild<QMenu*>(QStringLiteral("openRecentMenu"))->setEnabled(hasRecentGames_);
  setGameActionsEnabled(false);
  gameStatus_->setText(tr("No game loaded"));
  systemStatus_->setText(tr("System: —"));
  regionStatus_->setText(tr("Region: —"));
  fpsStatus_->setText(tr("0.0 FPS"));
  displayWidget_->clearFrame();
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
