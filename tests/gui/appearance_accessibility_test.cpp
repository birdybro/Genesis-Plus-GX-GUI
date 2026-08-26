#include "genplusgx/ui/appearance_settings_dialog.h"
#include "genplusgx/ui/main_window.h"
#include "genplusgx/ui/settings_dialog.h"
#include "genplusgx/ui/theme_controller.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTest>

#include <optional>

namespace {

class AppearanceAccessibilityTest final : public QObject {
  Q_OBJECT

private slots:
  void themesApplyApplicationWideAndRestoreSystemPalette();
  void dialogSupportsKeyboardApplyRestoreAndFailure();
  void preferencesActionUsesTheSettingsCenterAppearanceWorkflow();
};

void AppearanceAccessibilityTest::themesApplyApplicationWideAndRestoreSystemPalette()
{
  const auto originalPalette = qApp->palette();
  genplusgx::ui::ThemeController controller{*qApp};

  QVERIFY(controller.apply({.theme = genplusgx::settings::ThemeMode::dark}));
  QCOMPARE(controller.settings().theme, genplusgx::settings::ThemeMode::dark);
  QCOMPARE(qApp->palette().color(QPalette::Window),
    genplusgx::ui::ThemeController::darkPalette().color(QPalette::Window));
  QVERIFY(qApp->palette().color(QPalette::WindowText).lightness() >
          qApp->palette().color(QPalette::Window).lightness());

  QVERIFY(controller.apply({.theme = genplusgx::settings::ThemeMode::light}));
  QCOMPARE(qApp->palette().color(QPalette::Window),
    genplusgx::ui::ThemeController::lightPalette().color(QPalette::Window));
  QVERIFY(qApp->palette().color(QPalette::WindowText).lightness() <
          qApp->palette().color(QPalette::Window).lightness());

  QVERIFY(controller.apply({.theme = genplusgx::settings::ThemeMode::system}));
  QCOMPARE(qApp->palette(), originalPalette);
  genplusgx::ui::configureHighDpiPolicy();
  QCOMPARE(QGuiApplication::highDpiScaleFactorRoundingPolicy(),
    Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
}

void AppearanceAccessibilityTest::dialogSupportsKeyboardApplyRestoreAndFailure()
{
  genplusgx::ui::AppearanceSettingsDialog dialog{
    {.theme = genplusgx::settings::ThemeMode::system}};
  std::optional<genplusgx::settings::AppearanceSettings> applied;
  dialog.setSettingsSink(
    [&applied](const genplusgx::settings::AppearanceSettings& settings) {
      applied = settings;
      return genplusgx::PersistenceStatus{};
    });
  dialog.show();
  QVERIFY(QTest::qWaitForWindowExposed(&dialog));

  auto* theme = dialog.findChild<QComboBox*>(QStringLiteral("themeModeCombo"));
  auto* label = dialog.findChild<QLabel*>(QStringLiteral("themeModeLabel"));
  auto* apply =
    dialog.findChild<QPushButton*>(QStringLiteral("applyAppearanceSettingsButton"));
  auto* restore =
    dialog.findChild<QPushButton*>(QStringLiteral("restoreAppearanceDefaultsButton"));
  auto* validation =
    dialog.findChild<QLabel*>(QStringLiteral("appearanceValidationLabel"));
  QVERIFY(theme != nullptr);
  QVERIFY(label != nullptr);
  QVERIFY(apply != nullptr);
  QVERIFY(restore != nullptr);
  QVERIFY(validation != nullptr);
  QCOMPARE(label->buddy(), theme);
  QVERIFY(!theme->accessibleName().isEmpty());
  QVERIFY(!theme->accessibleDescription().isEmpty());

  QTest::keyClick(&dialog, Qt::Key_A, Qt::AltModifier);
  QCOMPARE(QApplication::focusWidget(), theme);
  theme->setCurrentIndex(
    theme->findData(static_cast<int>(genplusgx::settings::ThemeMode::dark)));
  QTest::keyClick(theme, Qt::Key_Tab);
  QCOMPARE(QApplication::focusWidget(), restore);
  QTest::mouseClick(apply, Qt::LeftButton);
  QVERIFY(applied.has_value());
  QCOMPARE(applied->theme, genplusgx::settings::ThemeMode::dark);

  QTest::mouseClick(restore, Qt::LeftButton);
  QCOMPARE(theme->currentData().toInt(),
    static_cast<int>(genplusgx::settings::ThemeMode::system));

  dialog.setSettingsSink([](const genplusgx::settings::AppearanceSettings&) {
    return genplusgx::PersistenceStatus{
      .error = genplusgx::PersistenceError::fileWriteFailed,
      .message = "Simulated read-only settings directory.",
    };
  });
  auto* ok =
    dialog.findChild<QPushButton*>(QStringLiteral("okAppearanceSettingsButton"));
  QTest::mouseClick(ok, Qt::LeftButton);
  QVERIFY(dialog.isVisible());
  QVERIFY(validation->isVisible());
  QVERIFY(validation->text().contains(QStringLiteral("read-only")));
  dialog.reject();
}

void AppearanceAccessibilityTest::preferencesActionUsesTheSettingsCenterAppearanceWorkflow()
{
  genplusgx::ui::MainWindow window;
  window.setAppearanceSettings({.theme = genplusgx::settings::ThemeMode::light});
  std::optional<genplusgx::settings::AppearanceSettings> applied;
  window.setAppearanceSettingsSink(
    [&applied](const genplusgx::settings::AppearanceSettings& settings) {
      applied = settings;
      return genplusgx::PersistenceStatus{};
    });
  window.show();
  window.findChild<QAction*>(QStringLiteral("settingsAction"))->trigger();
  QApplication::processEvents();

  auto* settingsDialog = window.findChild<genplusgx::ui::SettingsDialog*>(
    QStringLiteral("settingsDialog"));
  QVERIFY(settingsDialog != nullptr);
  QTest::mouseClick(settingsDialog->findChild<QPushButton*>(
    QStringLiteral("configureAppearanceButton")), Qt::LeftButton);
  QApplication::processEvents();

  auto* dialog = window.findChild<genplusgx::ui::AppearanceSettingsDialog*>(
    QStringLiteral("appearanceSettingsDialog"));
  QVERIFY(dialog != nullptr);
  auto* theme = dialog->findChild<QComboBox*>(QStringLiteral("themeModeCombo"));
  theme->setCurrentIndex(
    theme->findData(static_cast<int>(genplusgx::settings::ThemeMode::dark)));
  QTest::mouseClick(
    dialog->findChild<QPushButton*>(QStringLiteral("applyAppearanceSettingsButton")),
    Qt::LeftButton);
  QVERIFY(applied.has_value());
  QCOMPARE(applied->theme, genplusgx::settings::ThemeMode::dark);
  QCOMPARE(window.appearanceSettings().theme, genplusgx::settings::ThemeMode::dark);
  QVERIFY(settingsDialog->findChild<QLabel*>(
    QStringLiteral("generalSettingsSummary"))->text().contains(
      QStringLiteral("Dark")));
  dialog->reject();
}

} // namespace

QTEST_MAIN(AppearanceAccessibilityTest)
#include "appearance_accessibility_test.moc"
