#include "genplusgx/settings/rewind_settings.h"
#include "genplusgx/ui/main_window.h"
#include "genplusgx/ui/rewind_settings_dialog.h"

#include <QAction>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTest>

#include <cstddef>

namespace {

class RewindSettingsDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void appliesRestoresAndReportsPersistenceFailures();
  void mainWindowActionPersistsTypedSettings();
};

void RewindSettingsDialogTest::appliesRestoresAndReportsPersistenceFailures()
{
  constexpr std::size_t mebibyte = 1024U * 1024U;
  genplusgx::ui::RewindSettingsDialog dialog{
    genplusgx::settings::defaultRewindSettings()};
  dialog.show();
  QApplication::processEvents();
  auto* enabled = dialog.findChild<QCheckBox*>(
    QStringLiteral("rewindEnabledCheckBox"));
  auto* interval = dialog.findChild<QSpinBox*>(
    QStringLiteral("rewindCaptureIntervalSpinBox"));
  auto* memory = dialog.findChild<QSpinBox*>(
    QStringLiteral("rewindMemoryLimitSpinBox"));
  auto* apply = dialog.findChild<QPushButton*>(
    QStringLiteral("applyRewindSettingsButton"));
  auto* restore = dialog.findChild<QPushButton*>(
    QStringLiteral("restoreRewindDefaultsButton"));
  auto* error = dialog.findChild<QLabel*>(
    QStringLiteral("rewindSettingsErrorLabel"));
  QVERIFY(enabled && interval && memory && apply && restore && error);
  QVERIFY(enabled->isChecked());
  QCOMPARE(interval->value(), 6);
  QCOMPARE(memory->value(), 128);

  genplusgx::RewindConfiguration applied;
  int calls = 0;
  dialog.setSettingsSink([&applied, &calls](const auto& settings) {
    ++calls;
    applied = settings;
    return genplusgx::PersistenceStatus{};
  });
  enabled->setChecked(false);
  QCOMPARE(interval->isEnabled(), false);
  enabled->setChecked(true);
  interval->setValue(3);
  memory->setValue(256);
  QTest::mouseClick(apply, Qt::LeftButton);
  QCOMPARE(calls, 1);
  QCOMPARE(applied.captureIntervalFrames, 3U);
  QCOMPARE(applied.memoryLimitBytes, 256U * mebibyte);

  QTest::mouseClick(restore, Qt::LeftButton);
  QCOMPARE(interval->value(), 6);
  QCOMPARE(memory->value(), 128);
  dialog.setSettingsSink([](const auto&) {
    return genplusgx::PersistenceStatus{
      .error = genplusgx::PersistenceError::fileWriteFailed,
      .message = "Synthetic rewind persistence failure",
    };
  });
  QTest::mouseClick(apply, Qt::LeftButton);
  QVERIFY(error->isVisible());
  QVERIFY(error->text().contains(QStringLiteral("Synthetic")));
}

void RewindSettingsDialogTest::mainWindowActionPersistsTypedSettings()
{
  constexpr std::size_t mebibyte = 1024U * 1024U;
  genplusgx::ui::MainWindow window;
  genplusgx::RewindConfiguration persisted;
  int calls = 0;
  window.setRewindSettingsSink([&persisted, &calls](const auto& settings) {
    ++calls;
    persisted = settings;
    return genplusgx::PersistenceStatus{};
  });
  auto* action = window.findChild<QAction*>(QStringLiteral("rewindSettingsAction"));
  QVERIFY(action != nullptr);
  action->trigger();
  QApplication::processEvents();
  auto* dialog = window.findChild<genplusgx::ui::RewindSettingsDialog*>(
    QStringLiteral("rewindSettingsDialog"));
  QVERIFY(dialog != nullptr);
  dialog->findChild<QSpinBox*>(QStringLiteral("rewindCaptureIntervalSpinBox"))
    ->setValue(2);
  dialog->findChild<QSpinBox*>(QStringLiteral("rewindMemoryLimitSpinBox"))
    ->setValue(64);
  QTest::mouseClick(dialog->findChild<QPushButton*>(
    QStringLiteral("okRewindSettingsButton")), Qt::LeftButton);
  QCOMPARE(calls, 1);
  QCOMPARE(persisted.captureIntervalFrames, 2U);
  QCOMPARE(persisted.memoryLimitBytes, 64U * mebibyte);
  QCOMPARE(window.rewindSettings(), persisted);
}

} // namespace

QTEST_MAIN(RewindSettingsDialogTest)

#include "rewind_settings_dialog_test.moc"
