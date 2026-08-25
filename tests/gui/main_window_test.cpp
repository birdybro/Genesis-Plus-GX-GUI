#include "genplusgx/ui/about_dialog.h"
#include "genplusgx/ui/main_window.h"
#include "genplusgx/version.h"
#include "genplusgx/video/display_widget.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QStatusBar>
#include <QTest>

namespace {

class MainWindowTest final : public QObject {
  Q_OBJECT

private slots:
  void shellIsVisibleAndIdentified();
  void menusAndActionsHaveStableSemantics();
  void emptyStatusIsDescriptive();
  void aboutDialogReportsBuildIdentity();
  void exitActionClosesWindow();
  void videoActionsDriveDisplayPolicy();
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
    "aspectRatioMenu", "filteringMenu", "overscanMenu"};
  for (const auto* name : menuNames) {
    QVERIFY2(window.findChild<QMenu*>(QString::fromLatin1(name)) != nullptr, name);
  }

  const char* enabledNames[]{
    "openGameAction", "gameLibraryAction", "exitAction", "fullscreenAction",
    "muteAction", "volumeUpAction", "volumeDownAction",
    "controllerConfigurationAction", "playerAssignmentsAction", "settingsAction",
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

} // namespace

QTEST_MAIN(MainWindowTest)

#include "main_window_test.moc"
