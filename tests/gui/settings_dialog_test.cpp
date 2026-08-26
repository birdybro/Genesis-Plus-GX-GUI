#include "genplusgx/ui/appearance_settings_dialog.h"
#include "genplusgx/ui/main_window.h"
#include "genplusgx/ui/settings_dialog.h"
#include "genplusgx/ui/video_settings_dialog.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <filesystem>
#include <vector>

namespace {

class SettingsDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void eightPagesExposeCurrentValuesAndTypedActions();
  void mainWindowPreferencesRoutesThroughOneSettingsCenter();
};

void SettingsDialogTest::eightPagesExposeCurrentValuesAndTypedActions()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto root = std::filesystem::path{directory.path().toStdString()};
  genplusgx::ui::SettingsOverview overview{
    .appearance = {.theme = genplusgx::settings::ThemeMode::dark},
    .video = genplusgx::settings::defaultVideoSettings(),
    .audio = genplusgx::settings::defaultAudioSettings(),
    .input = genplusgx::input::defaultInputConfiguration(),
    .system = {},
    .bios = {},
    .screenshots = {.directory = root / "custom-shots"},
    .paths = genplusgx::ApplicationPaths{root},
    .connectedControllerCount = 2U,
    .pathsAvailable = true,
    .gameLoaded = false,
  };
  genplusgx::ui::SettingsDialog dialog{overview};
  std::vector<genplusgx::ui::SettingsPageAction> actions;
  dialog.setActionSink([&actions](auto action) { actions.push_back(action); });
  dialog.show();
  QApplication::processEvents();

  auto* categories =
    dialog.findChild<QListWidget*>(QStringLiteral("settingsCategoryList"));
  auto* pages =
    dialog.findChild<QStackedWidget*>(QStringLiteral("settingsPageStack"));
  QVERIFY(categories != nullptr);
  QVERIFY(pages != nullptr);
  QCOMPARE(categories->count(), 8);
  QCOMPARE(pages->count(), 8);
  categories->setFocus();
  QTest::keyClick(categories, Qt::Key_End);
  QCOMPARE(dialog.currentPage(), genplusgx::ui::SettingsPage::advanced);
  QTest::keyClick(categories, Qt::Key_Home);
  QCOMPARE(dialog.currentPage(), genplusgx::ui::SettingsPage::general);
  const std::array pageNames{
    "generalSettingsPage", "videoSettingsPage", "audioSettingsPage",
    "inputSettingsPage", "systemSettingsPage", "biosSettingsPage",
    "pathsSettingsPage", "advancedSettingsPage"};
  for (std::size_t index = 0U; index < pageNames.size(); ++index) {
    QVERIFY(dialog.findChild<QWidget*>(
      QString::fromLatin1(pageNames[index])) != nullptr);
    dialog.openPage(static_cast<genplusgx::ui::SettingsPage>(index));
    QCOMPARE(pages->currentIndex(), static_cast<int>(index));
  }

  dialog.openPage(genplusgx::ui::SettingsPage::general);
  QCOMPARE(dialog.currentPage(), genplusgx::ui::SettingsPage::general);
  QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("generalSettingsSummary"))
    ->text().contains(QStringLiteral("Dark")));
  dialog.openPage(genplusgx::ui::SettingsPage::input);
  auto* inputSummary =
    dialog.findChild<QLabel*>(QStringLiteral("inputSettingsSummary"));
  QVERIFY(inputSummary->text().contains(QStringLiteral("Default")));
  QVERIFY(inputSummary->text().contains(QStringLiteral("2")));
  dialog.openPage(genplusgx::ui::SettingsPage::paths);
  QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("pathsSettingsSummary"))
    ->text().contains(directory.path()));

  QTest::mouseClick(
    dialog.findChild<QPushButton*>(QStringLiteral("configureVideoButton")),
    Qt::LeftButton);
  QTest::mouseClick(
    dialog.findChild<QPushButton*>(QStringLiteral("configureAssignmentsButton")),
    Qt::LeftButton);
  const std::vector expectedActions{
    genplusgx::ui::SettingsPageAction::video,
    genplusgx::ui::SettingsPageAction::playerAssignments};
  QCOMPARE(actions, expectedActions);

  auto* perGame =
    dialog.findChild<QPushButton*>(QStringLiteral("configurePerGameButton"));
  QVERIFY(!perGame->isEnabled());
  overview.gameLoaded = true;
  dialog.setOverview(overview);
  QVERIFY(perGame->isEnabled());
  QTest::mouseClick(perGame, Qt::LeftButton);
  QCOMPARE(actions.back(), genplusgx::ui::SettingsPageAction::perGame);
}

void SettingsDialogTest::mainWindowPreferencesRoutesThroughOneSettingsCenter()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  genplusgx::ui::MainWindow window;
  window.setApplicationPaths(genplusgx::ApplicationPaths{
    std::filesystem::path{directory.path().toStdString()}});
  window.show();

  auto* settingsAction =
    window.findChild<QAction*>(QStringLiteral("settingsAction"));
  QVERIFY(settingsAction != nullptr);
  settingsAction->trigger();
  QApplication::processEvents();
  auto* center = window.findChild<genplusgx::ui::SettingsDialog*>(
    QStringLiteral("settingsDialog"));
  QVERIFY(center != nullptr);
  QVERIFY(center->isVisible());
  QCOMPARE(center->currentPage(), genplusgx::ui::SettingsPage::general);

  QTest::mouseClick(center->findChild<QPushButton*>(
    QStringLiteral("configureAppearanceButton")), Qt::LeftButton);
  QApplication::processEvents();
  auto* appearance = window.findChild<genplusgx::ui::AppearanceSettingsDialog*>(
    QStringLiteral("appearanceSettingsDialog"));
  QVERIFY(appearance != nullptr);
  appearance->reject();

  center->openPage(genplusgx::ui::SettingsPage::video);
  QTest::mouseClick(center->findChild<QPushButton*>(
    QStringLiteral("configureVideoButton")), Qt::LeftButton);
  QApplication::processEvents();
  auto* video = window.findChild<genplusgx::ui::VideoSettingsDialog*>(
    QStringLiteral("videoSettingsDialog"));
  QVERIFY(video != nullptr);
  video->reject();

  settingsAction->trigger();
  QApplication::processEvents();
  QCOMPARE(window.findChildren<genplusgx::ui::SettingsDialog*>(
    QStringLiteral("settingsDialog")).size(), 1);
  QCOMPARE(center->currentPage(), genplusgx::ui::SettingsPage::general);
  window.setGameLoaded(std::filesystem::path{"active.md"});
  QVERIFY(center->findChild<QPushButton*>(
    QStringLiteral("configurePerGameButton"))->isEnabled());
}

} // namespace

QTEST_MAIN(SettingsDialogTest)

#include "settings_dialog_test.moc"
