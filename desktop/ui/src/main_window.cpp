#include "genplusgx/ui/main_window.h"

#include "genplusgx/ui/about_dialog.h"
#include "genplusgx/version.h"

#include <QAction>
#include <QApplication>
#include <QFrame>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>

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
  : QMainWindow(parent)
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
  auto* canvas = new QFrame(this);
  canvas->setObjectName(QStringLiteral("emulatorCanvas"));
  canvas->setFrameShape(QFrame::NoFrame);
  canvas->setStyleSheet(QStringLiteral("#emulatorCanvas { background: black; }"));
  canvas->setAccessibleName(tr("Emulator display"));

  auto* layout = new QVBoxLayout(canvas);
  auto* prompt = new QLabel(tr("Open or drop a game to begin"), canvas);
  prompt->setObjectName(QStringLiteral("emptyCanvasLabel"));
  prompt->setAlignment(Qt::AlignCenter);
  prompt->setStyleSheet(QStringLiteral("color: #b8b8b8; font-size: 16px;"));
  layout->addWidget(prompt);
  setCentralWidget(canvas);
}

void MainWindow::buildMenus()
{
  auto createMenu = [this](const QString& title, const char* objectName) {
    auto* menu = menuBar()->addMenu(title);
    menu->setObjectName(QString::fromLatin1(objectName));
    return menu;
  };

  auto* file = createMenu(tr("&File"), "fileMenu");
  addAction(*file, tr("&Open Game…"), "openGameAction", QKeySequence::Open);
  auto* recent = file->addMenu(tr("Open &Recent"));
  recent->setObjectName(QStringLiteral("openRecentMenu"));
  recent->setEnabled(false);
  addAction(*file, tr("&Close Game"), "closeGameAction", QKeySequence::Close);
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
  auto* scale = video->addMenu(tr("&Scale"));
  scale->setObjectName(QStringLiteral("videoScaleMenu"));
  auto* aspect = video->addMenu(tr("&Aspect Ratio"));
  aspect->setObjectName(QStringLiteral("aspectRatioMenu"));
  auto* filtering = video->addMenu(tr("&Filtering"));
  filtering->setObjectName(QStringLiteral("filteringMenu"));
  auto* overscan = video->addMenu(tr("&Overscan"));
  overscan->setObjectName(QStringLiteral("overscanMenu"));

  auto* audio = createMenu(tr("&Audio"), "audioMenu");
  auto* mute = addAction(*audio, tr("&Mute"), "muteAction", QKeySequence{tr("M")});
  mute->setCheckable(true);
  addAction(*audio, tr("Volume &Up"), "volumeUpAction", QKeySequence{tr("+")});
  addAction(*audio, tr("Volume &Down"), "volumeDownAction", QKeySequence{tr("-")});

  auto* input = createMenu(tr("&Input"), "inputMenu");
  addAction(*input, tr("&Controller Configuration…"), "controllerConfigurationAction");
  addAction(*input, tr("&Player Assignments…"), "playerAssignmentsAction");

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

} // namespace genplusgx::ui
