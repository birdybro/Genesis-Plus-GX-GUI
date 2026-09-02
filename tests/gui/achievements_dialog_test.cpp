#include "genplusgx/ui/achievements_dialog.h"
#include "genplusgx/ui/main_window.h"

#include <QAction>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTableWidget>
#include <QTest>

#include <filesystem>
#include <string>
#include <utility>

class AchievementsDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void accountWorkflowIsTestableAndSecretSafe();
  void mainWindowEnforcesHardcorePresentation();
};

void AchievementsDialogTest::accountWorkflowIsTestableAndSecretSafe()
{
  genplusgx::ui::AchievementsDialog dialog;
  genplusgx::achievements::Settings applied;
  std::string loginUser;
  std::string loginPassword;
  bool signedOut = false;
  dialog.setSettingsSink(
    [&applied](const genplusgx::achievements::Settings& settings) {
      applied = settings;
      return genplusgx::PersistenceStatus{};
    });
  dialog.setLoginSink(
    [&loginUser, &loginPassword](std::string username, std::string password) {
      loginUser = std::move(username);
      loginPassword = std::move(password);
    });
  dialog.setLogoutSink([&signedOut] { signedOut = true; });
  dialog.show();
  QApplication::processEvents();

  auto* enabled = dialog.findChild<QCheckBox*>(
    QStringLiteral("achievementsEnabledCheck"));
  auto* hardcore = dialog.findChild<QCheckBox*>(
    QStringLiteral("achievementsHardcoreCheck"));
  auto* username = dialog.findChild<QLineEdit*>(
    QStringLiteral("achievementsUsernameEdit"));
  auto* password = dialog.findChild<QLineEdit*>(
    QStringLiteral("achievementsPasswordEdit"));
  auto* signIn = dialog.findChild<QPushButton*>(
    QStringLiteral("achievementsSignInButton"));
  QVERIFY(enabled != nullptr);
  QVERIFY(hardcore != nullptr);
  QVERIFY(username != nullptr);
  QVERIFY(password != nullptr);
  QVERIFY(signIn != nullptr);
  QCOMPARE(password->echoMode(), QLineEdit::Password);
  QVERIFY(!username->accessibleName().isEmpty());
  QVERIFY(!password->accessibleName().isEmpty());

  enabled->setChecked(true);
  hardcore->setChecked(true);
  username->setText(QStringLiteral("GuiPlayer"));
  password->setText(QStringLiteral("not-persisted"));
  QTest::mouseClick(signIn, Qt::LeftButton);
  QCOMPARE(applied.username, std::string{"GuiPlayer"});
  QVERIFY(applied.enabled);
  QVERIFY(applied.hardcore);
  QCOMPARE(loginUser, std::string{"GuiPlayer"});
  QCOMPARE(loginPassword, std::string{"not-persisted"});
  QVERIFY(password->text().isEmpty());

  loginPassword.clear();
  username->setText(QStringLiteral("invalid username"));
  password->setText(QStringLiteral("must-not-submit"));
  QTest::mouseClick(signIn, Qt::LeftButton);
  QVERIFY(loginPassword.empty());
  QVERIFY(!password->text().isEmpty());
  username->setText(QStringLiteral("GuiPlayer"));
  password->clear();

  genplusgx::achievements::Snapshot snapshot;
  snapshot.state = genplusgx::achievements::ConnectionState::active;
  snapshot.username = "GuiPlayer";
  snapshot.displayName = "GUI Player";
  snapshot.gameTitle = "Synthetic Test";
  snapshot.richPresence = "Testing achievements";
  snapshot.userScore = 99U;
  snapshot.gameId = 1U;
  snapshot.unlockedCount = 1U;
  snapshot.achievementCount = 2U;
  snapshot.unlockedPoints = 5U;
  snapshot.totalPoints = 10U;
  snapshot.enabled = true;
  snapshot.authenticated = true;
  snapshot.gameLoaded = true;
  snapshot.hardcore = true;
  genplusgx::achievements::Achievement achievement;
  achievement.id = 1U;
  achievement.title = "First Frame";
  achievement.description = "Run one frame";
  achievement.points = 5U;
  achievement.unlockedHardcore = true;
  snapshot.achievements.push_back(std::move(achievement));
  dialog.setSnapshot(snapshot);
  auto* table = dialog.findChild<QTableWidget*>(
    QStringLiteral("achievementsTable"));
  auto* signOut = dialog.findChild<QPushButton*>(
    QStringLiteral("achievementsSignOutButton"));
  QVERIFY(table != nullptr);
  QCOMPARE(table->rowCount(), 1);
  QCOMPARE(table->item(0, 0)->text(), QStringLiteral("First Frame"));
  QVERIFY(signOut->isEnabled());
  QTest::mouseClick(signOut, Qt::LeftButton);
  QVERIFY(signedOut);
}

void AchievementsDialogTest::mainWindowEnforcesHardcorePresentation()
{
  genplusgx::ui::MainWindow window;
  window.setEmulationControlSink([](auto, bool) { return true; });
  window.setGameLoaded(std::filesystem::path{"/tmp/synthetic.bin"});
  window.setStateSessionReady(true);

  auto* achievementsAction = window.findChild<QAction*>(
    QStringLiteral("achievementsAction"));
  auto* saveState = window.findChild<QAction*>(
    QStringLiteral("saveStateAction"));
  auto* slowMotion = window.findChild<QAction*>(
    QStringLiteral("slowMotionAction"));
  auto* frameAdvance = window.findChild<QAction*>(
    QStringLiteral("frameAdvanceAction"));
  auto* netplay = window.findChild<QAction*>(QStringLiteral("netplayAction"));
  auto* speed75 = window.findChild<QAction*>(
    QStringLiteral("emulationSpeed75Action"));
  auto* speed125 = window.findChild<QAction*>(
    QStringLiteral("emulationSpeed125Action"));
  auto* speedSettings = window.findChild<QAction*>(
    QStringLiteral("speedSettingsAction"));
  QVERIFY(achievementsAction != nullptr);
  QVERIFY(saveState != nullptr);
  QVERIFY(slowMotion != nullptr);
  QVERIFY(frameAdvance != nullptr);
  QVERIFY(netplay != nullptr);
  QVERIFY(speed75 != nullptr);
  QVERIFY(speed125 != nullptr);
  QVERIFY(speedSettings != nullptr);

  genplusgx::achievements::Snapshot snapshot;
  snapshot.enabled = true;
  snapshot.authenticated = true;
  snapshot.gameLoaded = true;
  snapshot.hardcore = true;
  window.setAchievementSnapshot(snapshot);
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  QVERIFY(achievementsAction->isEnabled());
#else
  QVERIFY(!achievementsAction->isEnabled());
#endif
  QVERIFY(!saveState->isEnabled());
  QVERIFY(!slowMotion->isEnabled());
  QVERIFY(!frameAdvance->isEnabled());
  QVERIFY(!netplay->isEnabled());
  QVERIFY(!speed75->isEnabled());
  QVERIFY(speed125->isEnabled());
  QVERIFY(!speedSettings->isEnabled());

  snapshot.hardcore = false;
  window.setAchievementSnapshot(snapshot);
  QVERIFY(saveState->isEnabled());
  QVERIFY(slowMotion->isEnabled());
  QVERIFY(!netplay->isEnabled());
  QVERIFY(speed75->isEnabled());
  QVERIFY(speedSettings->isEnabled());
  snapshot.gameLoaded = false;
  snapshot.state = genplusgx::achievements::ConnectionState::signedIn;
  window.setAchievementSnapshot(snapshot);
  QVERIFY(netplay->isEnabled());

  auto notificationSettings = genplusgx::achievements::Settings{};
  notificationSettings.notifications = false;
  window.setAchievementSettings(notificationSettings);
  window.statusBar()->showMessage(QStringLiteral("notification sentinel"));
  genplusgx::achievements::Event unlocked;
  unlocked.type = genplusgx::achievements::EventType::achievementUnlocked;
  unlocked.title = "Hidden notice";
  unlocked.snapshot = snapshot;
  window.presentAchievementEvent(unlocked);
  QCOMPARE(window.statusBar()->currentMessage(),
    QStringLiteral("notification sentinel"));
  notificationSettings.notifications = true;
  window.setAchievementSettings(notificationSettings);
  window.presentAchievementEvent(unlocked);
  QVERIFY(window.statusBar()->currentMessage().contains(
    QStringLiteral("Hidden notice")));
  genplusgx::achievements::Event leaderboard;
  leaderboard.type = genplusgx::achievements::EventType::leaderboardSubmitted;
  leaderboard.title = "Synthetic Sprint";
  leaderboard.snapshot = snapshot;
  window.presentAchievementEvent(leaderboard);
  QVERIFY(window.statusBar()->currentMessage().contains(
    QStringLiteral("Synthetic Sprint")));
#if defined(GENPLUSGX_HAVE_ACHIEVEMENTS)
  achievementsAction->trigger();
  QVERIFY(window.findChild<genplusgx::ui::AchievementsDialog*>(
    QStringLiteral("achievementsDialog")) != nullptr);
#endif
}

QTEST_MAIN(AchievementsDialogTest)
#include "achievements_dialog_test.moc"
