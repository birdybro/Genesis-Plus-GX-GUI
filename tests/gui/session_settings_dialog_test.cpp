#include "genplusgx/ui/main_window.h"
#include "genplusgx/ui/session_settings_dialog.h"
#include "genplusgx/ui/settings_dialog.h"

#include <QAction>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QStatusBar>
#include <QTest>

namespace {

class SessionSettingsDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void appliesRestoresAndReportsPersistenceFailure();
  void settingsCenterRoutesToTypedEditor();
};

void SessionSettingsDialogTest::appliesRestoresAndReportsPersistenceFailure()
{
  genplusgx::ui::SessionSettingsDialog dialog{false};
  dialog.show();
  QApplication::processEvents();
  auto* enabled = dialog.findChild<QCheckBox*>(
    QStringLiteral("resumeOnLaunchCheckBox"));
  auto* apply = dialog.findChild<QPushButton*>(
    QStringLiteral("applySessionSettingsButton"));
  auto* restore = dialog.findChild<QPushButton*>(
    QStringLiteral("restoreSessionDefaultsButton"));
  auto* error = dialog.findChild<QLabel*>(
    QStringLiteral("sessionSettingsErrorLabel"));
  QVERIFY(enabled && apply && restore && error);
  QVERIFY(!enabled->isChecked());

  bool persisted = false;
  int calls = 0;
  dialog.setSettingsSink([&persisted, &calls](bool value) {
    persisted = value;
    ++calls;
    return genplusgx::PersistenceStatus{};
  });
  enabled->setChecked(true);
  QTest::mouseClick(apply, Qt::LeftButton);
  QCOMPARE(calls, 1);
  QVERIFY(persisted);
  QTest::mouseClick(restore, Qt::LeftButton);
  QVERIFY(!enabled->isChecked());

  dialog.setSettingsSink([](bool) {
    return genplusgx::PersistenceStatus{
      .error = genplusgx::PersistenceError::fileWriteFailed,
      .message = "Synthetic session persistence failure",
    };
  });
  QTest::mouseClick(apply, Qt::LeftButton);
  QVERIFY(error->isVisible());
  QVERIFY(error->text().contains(QStringLiteral("Synthetic")));
}

void SessionSettingsDialogTest::settingsCenterRoutesToTypedEditor()
{
  genplusgx::ui::MainWindow window;
  genplusgx::settings::SessionSettings initial{
    .resumeOnLaunch = true,
    .lastGamePath = std::filesystem::temp_directory_path() / "last.md",
  };
  window.setSessionSettings(initial);
  bool saved = true;
  window.setSessionSettingsSink([&saved](bool value) {
    saved = value;
    return genplusgx::PersistenceStatus{};
  });
  window.showSettings(genplusgx::ui::SettingsPage::general);
  QApplication::processEvents();
  auto* center = window.findChild<genplusgx::ui::SettingsDialog*>(
    QStringLiteral("settingsDialog"));
  QVERIFY(center != nullptr);
  auto* summary = center->findChild<QLabel*>(
    QStringLiteral("generalSettingsSummary"));
  QVERIFY(summary->text().contains(QStringLiteral("Resume last session: enabled")));
  QTest::mouseClick(center->findChild<QPushButton*>(
    QStringLiteral("configureSessionButton")), Qt::LeftButton);
  QApplication::processEvents();
  auto* editor = window.findChild<genplusgx::ui::SessionSettingsDialog*>(
    QStringLiteral("sessionSettingsDialog"));
  QVERIFY(editor != nullptr);
  editor->findChild<QCheckBox*>(QStringLiteral("resumeOnLaunchCheckBox"))
    ->setChecked(false);
  QTest::mouseClick(editor->findChild<QPushButton*>(
    QStringLiteral("okSessionSettingsButton")), Qt::LeftButton);
  QVERIFY(!saved);
  QVERIFY(!window.sessionSettings().resumeOnLaunch);
  QVERIFY(!window.sessionSettings().lastGamePath);

  window.setEmulationControlSink([](auto, bool) { return true; });
  window.setGameLoaded(std::filesystem::temp_directory_path() / "active.md");
  window.setSessionResumeBusy(true);
  QVERIFY(!window.findChild<QAction*>(QStringLiteral("openGameAction"))
    ->isEnabled());
  QVERIFY(!window.findChild<QAction*>(QStringLiteral("resetAction"))
    ->isEnabled());
  QCOMPARE(window.statusBar()->currentMessage(),
    QStringLiteral("Restoring the previous session…"));
  window.setSessionResumeBusy(false);
  QVERIFY(window.findChild<QAction*>(QStringLiteral("openGameAction"))
    ->isEnabled());
  QVERIFY(window.findChild<QAction*>(QStringLiteral("resetAction"))
    ->isEnabled());
  QCOMPARE(window.statusBar()->currentMessage(),
    QStringLiteral("Session restoration complete."));
}

} // namespace

QTEST_MAIN(SessionSettingsDialogTest)

#include "session_settings_dialog_test.moc"
