#include "genplusgx/settings/speed_settings.h"
#include "genplusgx/ui/dialog_service.h"
#include "genplusgx/ui/main_window.h"
#include "genplusgx/ui/speed_settings_dialog.h"

#include <QAction>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTest>

#include <memory>
#include <optional>
#include <vector>

namespace {

class FakeDialogService final : public genplusgx::ui::DialogService {
public:
  std::optional<std::filesystem::path> chooseGame(
    QWidget*, const std::filesystem::path&) override
  {
    return std::nullopt;
  }

  void showError(QWidget*, const QString&, const QString& message) override
  {
    errors.push_back(message);
  }

  std::vector<QString> errors;
};

class SpeedSettingsDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void appliesRestoresCancelsAndReportsPersistenceFailures();
  void mainWindowPresetsAndDialogPersistTransactionally();
};

void SpeedSettingsDialogTest::appliesRestoresCancelsAndReportsPersistenceFailures()
{
  const genplusgx::EmulationSpeedConfiguration initial{
    .normalPercent = 125U,
    .slowMotionPercent = 25U,
    .fastForwardPercent = 800U,
  };
  genplusgx::ui::SpeedSettingsDialog dialog{initial};
  dialog.show();
  QApplication::processEvents();
  auto* normal = dialog.findChild<QSpinBox*>(
    QStringLiteral("normalSpeedPercentSpinBox"));
  auto* slow = dialog.findChild<QSpinBox*>(
    QStringLiteral("slowMotionPercentSpinBox"));
  auto* fast = dialog.findChild<QSpinBox*>(
    QStringLiteral("fastForwardPercentSpinBox"));
  auto* summary = dialog.findChild<QLabel*>(
    QStringLiteral("speedBehaviorSummaryLabel"));
  auto* error = dialog.findChild<QLabel*>(
    QStringLiteral("speedSettingsErrorLabel"));
  auto* apply = dialog.findChild<QPushButton*>(
    QStringLiteral("applySpeedSettingsButton"));
  auto* restore = dialog.findChild<QPushButton*>(
    QStringLiteral("restoreSpeedDefaultsButton"));
  QVERIFY(normal && slow && fast && summary && error && apply && restore);
  QCOMPARE(normal->value(), 125);
  QCOMPARE(slow->value(), 25);
  QCOMPARE(fast->value(), 800);
  QVERIFY(!normal->accessibleName().isEmpty());
  QVERIFY(summary->text().contains(QStringLiteral("125%")));
  QVERIFY(summary->text().contains(QStringLiteral("paused")));

  genplusgx::EmulationSpeedConfiguration applied;
  int calls = 0;
  dialog.setSettingsSink([&applied, &calls](const auto& settings) {
    ++calls;
    applied = settings;
    return genplusgx::PersistenceStatus{};
  });
  normal->setValue(150);
  slow->setValue(50);
  fast->setValue(1'200);
  QTest::mouseClick(apply, Qt::LeftButton);
  QCOMPARE(calls, 1);
  QCOMPARE(applied, (genplusgx::EmulationSpeedConfiguration{
    .normalPercent = 150U,
    .slowMotionPercent = 50U,
    .fastForwardPercent = 1'200U,
  }));
  QVERIFY(!error->isVisible());

  QTest::mouseClick(restore, Qt::LeftButton);
  QCOMPARE(dialog.settings(), genplusgx::settings::defaultSpeedSettings());
  dialog.setSettingsSink([](const auto&) {
    return genplusgx::PersistenceStatus{
      .error = genplusgx::PersistenceError::fileWriteFailed,
      .message = "Synthetic speed persistence failure",
    };
  });
  QTest::mouseClick(apply, Qt::LeftButton);
  QVERIFY(error->isVisible());
  QVERIFY(error->text().contains(QStringLiteral("Synthetic")));

  auto* cancel = dialog.findChild<QPushButton*>(
    QStringLiteral("cancelSpeedSettingsButton"));
  QVERIFY(cancel != nullptr);
  QTest::mouseClick(cancel, Qt::LeftButton);
  QCOMPARE(dialog.result(), static_cast<int>(QDialog::Rejected));
}

void SpeedSettingsDialogTest::mainWindowPresetsAndDialogPersistTransactionally()
{
  genplusgx::ui::MainWindow window;
  auto dialogs = std::make_shared<FakeDialogService>();
  window.setDialogService(dialogs);
  genplusgx::EmulationSpeedConfiguration persisted =
    genplusgx::settings::defaultSpeedSettings();
  int calls = 0;
  bool fail = false;
  window.setSpeedSettingsSink([&persisted, &calls, &fail](const auto& settings) {
    ++calls;
    if (fail) {
      return genplusgx::PersistenceStatus{
        .error = genplusgx::PersistenceError::fileWriteFailed,
        .message = "Synthetic preset failure",
      };
    }
    persisted = settings;
    return genplusgx::PersistenceStatus{};
  });

  auto* preset = window.findChild<QAction*>(
    QStringLiteral("emulationSpeed150Action"));
  QVERIFY(preset != nullptr);
  preset->trigger();
  QCOMPARE(calls, 1);
  QCOMPARE(persisted.normalPercent, 150U);
  QCOMPARE(window.speedSettings(), persisted);
  QVERIFY(preset->isChecked());
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("speedStatusLabel"))->text(),
    QStringLiteral("Speed 150%"));

  fail = true;
  auto* rejectedPreset = window.findChild<QAction*>(
    QStringLiteral("emulationSpeed200Action"));
  QVERIFY(rejectedPreset != nullptr);
  rejectedPreset->trigger();
  QCOMPARE(calls, 2);
  QCOMPARE(window.speedSettings().normalPercent, 150U);
  QVERIFY(preset->isChecked());
  QCOMPARE(dialogs->errors.size(), std::size_t{1U});
  QVERIFY(dialogs->errors.front().contains(QStringLiteral("Synthetic")));

  fail = false;
  auto* action = window.findChild<QAction*>(
    QStringLiteral("speedSettingsAction"));
  QVERIFY(action != nullptr);
  action->trigger();
  QApplication::processEvents();
  auto* dialog = window.findChild<genplusgx::ui::SpeedSettingsDialog*>(
    QStringLiteral("speedSettingsDialog"));
  QVERIFY(dialog != nullptr);
  dialog->findChild<QSpinBox*>(QStringLiteral("slowMotionPercentSpinBox"))
    ->setValue(25);
  dialog->findChild<QSpinBox*>(QStringLiteral("fastForwardPercentSpinBox"))
    ->setValue(800);
  QTest::mouseClick(dialog->findChild<QPushButton*>(
    QStringLiteral("okSpeedSettingsButton")), Qt::LeftButton);
  QCOMPARE(calls, 3);
  QCOMPARE(persisted.slowMotionPercent, 25U);
  QCOMPARE(persisted.fastForwardPercent, 800U);
  QCOMPARE(window.speedSettings(), persisted);
}

} // namespace

QTEST_MAIN(SpeedSettingsDialogTest)

#include "speed_settings_dialog_test.moc"
