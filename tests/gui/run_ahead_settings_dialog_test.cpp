#include "genplusgx/settings/run_ahead_settings.h"
#include "genplusgx/ui/main_window.h"
#include "genplusgx/ui/run_ahead_settings_dialog.h"

#include <QAction>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTest>

namespace {

class RunAheadSettingsDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void appliesRestoresAndReportsPersistenceFailures();
  void mainWindowActionPersistsAndTracksRuntimeSupport();
};

void RunAheadSettingsDialogTest::appliesRestoresAndReportsPersistenceFailures()
{
  genplusgx::ui::RunAheadSettingsDialog dialog{
    genplusgx::settings::defaultRunAheadSettings()};
  dialog.show();
  QApplication::processEvents();
  auto* enabled = dialog.findChild<QCheckBox*>(
    QStringLiteral("runAheadEnabledCheckBox"));
  auto* frames = dialog.findChild<QSpinBox*>(
    QStringLiteral("runAheadFramesSpinBox"));
  auto* apply = dialog.findChild<QPushButton*>(
    QStringLiteral("applyRunAheadSettingsButton"));
  auto* restore = dialog.findChild<QPushButton*>(
    QStringLiteral("restoreRunAheadDefaultsButton"));
  auto* summary = dialog.findChild<QLabel*>(
    QStringLiteral("runAheadBehaviorSummaryLabel"));
  auto* error = dialog.findChild<QLabel*>(
    QStringLiteral("runAheadSettingsErrorLabel"));
  QVERIFY(enabled && frames && apply && restore && summary && error);
  QVERIFY(!enabled->isChecked());
  QCOMPARE(frames->value(), 1);
  QVERIFY(!frames->isEnabled());

  genplusgx::RunAheadConfiguration applied;
  int calls = 0;
  dialog.setSettingsSink([&applied, &calls](const auto& settings) {
    ++calls;
    applied = settings;
    return genplusgx::PersistenceStatus{};
  });
  enabled->setChecked(true);
  frames->setValue(4);
  QVERIFY(frames->isEnabled());
  QVERIFY(summary->text().contains(QStringLiteral("speculative")));
  QTest::mouseClick(apply, Qt::LeftButton);
  QCOMPARE(calls, 1);
  const genplusgx::RunAheadConfiguration expected{
    .enabled = true,
    .frames = 4U,
  };
  QCOMPARE(applied, expected);

  QTest::mouseClick(restore, Qt::LeftButton);
  QVERIFY(!enabled->isChecked());
  QCOMPARE(frames->value(), 1);
  dialog.setSettingsSink([](const auto&) {
    return genplusgx::PersistenceStatus{
      .error = genplusgx::PersistenceError::fileWriteFailed,
      .message = "Synthetic run-ahead persistence failure",
    };
  });
  enabled->setChecked(true);
  QTest::mouseClick(apply, Qt::LeftButton);
  QVERIFY(error->isVisible());
  QVERIFY(error->text().contains(QStringLiteral("Synthetic")));
}

void RunAheadSettingsDialogTest::mainWindowActionPersistsAndTracksRuntimeSupport()
{
  genplusgx::ui::MainWindow window;
  genplusgx::RunAheadConfiguration persisted;
  int calls = 0;
  window.setRunAheadSettingsSink([&persisted, &calls](const auto& settings) {
    ++calls;
    persisted = settings;
    return genplusgx::PersistenceStatus{};
  });
  window.setGameLoaded("synthetic.bin");
  window.setRunAheadRuntimeState(true, false, false);
  auto* toggle = window.findChild<QAction*>(QStringLiteral("runAheadAction"));
  auto* settings = window.findChild<QAction*>(
    QStringLiteral("runAheadSettingsAction"));
  QVERIFY(toggle && settings);
  QVERIFY(toggle->isEnabled());
  QVERIFY(!toggle->isChecked());
  toggle->trigger();
  QCOMPARE(calls, 1);
  QVERIFY(persisted.enabled);
  QCOMPARE(persisted.frames, 1U);
  QCOMPARE(window.runAheadSettings(), persisted);

  window.setRunAheadRuntimeState(true, true, true);
  QVERIFY(toggle->toolTip().contains(QStringLiteral("verified")));
  settings->trigger();
  QApplication::processEvents();
  auto* dialog = window.findChild<genplusgx::ui::RunAheadSettingsDialog*>(
    QStringLiteral("runAheadSettingsDialog"));
  QVERIFY(dialog != nullptr);
  dialog->findChild<QSpinBox*>(QStringLiteral("runAheadFramesSpinBox"))
    ->setValue(3);
  QTest::mouseClick(dialog->findChild<QPushButton*>(
    QStringLiteral("okRunAheadSettingsButton")), Qt::LeftButton);
  QCOMPARE(calls, 2);
  QVERIFY(persisted.enabled);
  QCOMPARE(persisted.frames, 3U);
  QCOMPARE(window.runAheadSettings(), persisted);

  window.setRunAheadRuntimeState(false, false, false);
  QVERIFY(!toggle->isEnabled());
  QVERIFY(toggle->isChecked());
  QVERIFY(toggle->toolTip().contains(QStringLiteral("unavailable")));

  window.setRunAheadSettingsSink([](const auto&) {
    return genplusgx::PersistenceStatus{
      .error = genplusgx::PersistenceError::fileWriteFailed,
      .message = "Rejected",
    };
  });
  window.setRunAheadRuntimeState(true, true, true);
  toggle->trigger();
  QVERIFY(toggle->isChecked());
  QVERIFY(window.runAheadSettings().enabled);
}

} // namespace

QTEST_MAIN(RunAheadSettingsDialogTest)

#include "run_ahead_settings_dialog_test.moc"
