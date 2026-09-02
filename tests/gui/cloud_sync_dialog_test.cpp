#include "genplusgx/ui/cloud_sync_dialog.h"
#include "genplusgx/ui/main_window.h"

#include <QAction>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>

#include <filesystem>
#include <string>
#include <utility>

class CloudSyncDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void settingsCredentialsAndResultsAreTestable();
  void mainWindowExposesAndGuardsCloudSync();
};

void CloudSyncDialogTest::settingsCredentialsAndResultsAreTestable()
{
  using namespace genplusgx;
  ui::CloudSyncDialog dialog;
  cloud::Settings applied;
  std::string savedEndpoint;
  std::string savedUsername;
  std::string savedPassword;
  std::string forgottenEndpoint;
  std::string syncPassword;
  dialog.setSettingsSink([&applied](const cloud::Settings& settings) {
    applied = settings;
    return PersistenceStatus{};
  });
  dialog.setPasswordSink([&](std::string endpoint, std::string username,
      std::string password) {
    savedEndpoint = std::move(endpoint);
    savedUsername = std::move(username);
    savedPassword = std::move(password);
  });
  dialog.setForgetSink([&](std::string endpoint, std::string) {
    forgottenEndpoint = std::move(endpoint);
  });
  dialog.setSyncSink([&syncPassword](std::string password) {
    syncPassword = std::move(password);
  });
  dialog.show();
  QApplication::processEvents();

  auto* enabled = dialog.findChild<QCheckBox*>(QStringLiteral("cloudEnabledCheck"));
  auto* endpoint = dialog.findChild<QLineEdit*>(QStringLiteral("cloudEndpointEdit"));
  auto* username = dialog.findChild<QLineEdit*>(QStringLiteral("cloudUsernameEdit"));
  auto* password = dialog.findChild<QLineEdit*>(QStringLiteral("cloudPasswordEdit"));
  auto* remember = dialog.findChild<QPushButton*>(
    QStringLiteral("cloudRememberPasswordButton"));
  auto* forget = dialog.findChild<QPushButton*>(
    QStringLiteral("cloudForgetPasswordButton"));
  auto* sync = dialog.findChild<QPushButton*>(QStringLiteral("cloudSyncNowButton"));
  auto* status = dialog.findChild<QLabel*>(QStringLiteral("cloudStatusLabel"));
  auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("cloudResultsTable"));
  QVERIFY(enabled != nullptr);
  QVERIFY(endpoint != nullptr);
  QVERIFY(username != nullptr);
  QVERIFY(password != nullptr);
  QVERIFY(remember != nullptr);
  QVERIFY(forget != nullptr);
  QVERIFY(sync != nullptr);
  QVERIFY(status != nullptr);
  QVERIFY(table != nullptr);
  QCOMPARE(password->echoMode(), QLineEdit::Password);
  QVERIFY(!endpoint->accessibleName().isEmpty());
  QVERIFY(!password->accessibleName().isEmpty());

  enabled->setChecked(true);
  endpoint->setText(QStringLiteral("http://insecure.example.test"));
  username->setText(QStringLiteral("tester"));
  password->setText(QStringLiteral("must-not-submit"));
  QTest::mouseClick(remember, Qt::LeftButton);
  QVERIFY(savedPassword.empty());
  QVERIFY(status->text().contains(QStringLiteral("HTTPS")));
  QVERIFY(!password->text().isEmpty());

  endpoint->setText(QStringLiteral("https://cloud.example.test/webdav"));
  password->setText(QStringLiteral("temporary-secret"));
  QTest::mouseClick(remember, Qt::LeftButton);
  QCOMPARE(applied.endpoint, std::string{"https://cloud.example.test/webdav"});
  QCOMPARE(savedEndpoint, applied.endpoint);
  QCOMPARE(savedUsername, std::string{"tester"});
  QCOMPARE(savedPassword, std::string{"temporary-secret"});
  QVERIFY(password->text().isEmpty());

  password->setText(QStringLiteral("one-time-secret"));
  QTest::mouseClick(sync, Qt::LeftButton);
  QCOMPARE(syncPassword, std::string{"one-time-secret"});
  QVERIFY(password->text().isEmpty());
  dialog.setBusy(true);
  QVERIFY(!sync->isEnabled());
  dialog.setBusy(false);
  dialog.setGameActive(true);
  QVERIFY(!sync->isEnabled());
  QVERIFY(!sync->toolTip().isEmpty());
  dialog.setGameActive(false);
  QVERIFY(sync->isEnabled());

  cloud::SyncResult result;
  result.summary.uploaded = 1U;
  result.summary.downloaded = 1U;
  result.summary.conflicts = 1U;
  result.summary.items = {
    {"saves/game/cartridge.srm", cloud::Action::upload, {}},
    {"states/game/slot-0.gpgxstate", cloud::Action::download, {}},
    {"states/game/slot-1.gpgxstate", cloud::Action::conflict,
      std::filesystem::path{"/tmp/conflict.gpgxstate"}},
  };
  dialog.showResult(result);
  QCOMPARE(table->rowCount(), 3);
  QVERIFY(status->text().contains(QStringLiteral("1 conflict")));
  QVERIFY(table->item(2, 2)->text().contains(QStringLiteral("conflict")));

  QTest::mouseClick(forget, Qt::LeftButton);
  QCOMPARE(forgottenEndpoint, applied.endpoint);
  enabled->setChecked(false);
  QVERIFY(forget->isEnabled());
}

void CloudSyncDialogTest::mainWindowExposesAndGuardsCloudSync()
{
  using namespace genplusgx;
  ui::MainWindow window;
  cloud::Settings settings;
  settings.enabled = true;
  settings.endpoint = "https://cloud.example.test/webdav";
  settings.username = "tester";
  window.setCloudSettings(settings);
  window.setCloudSettingsSink([](const cloud::Settings&) {
    return PersistenceStatus{};
  });
  window.setCloudSyncSink([](std::string) {});
  auto* action = window.findChild<QAction*>(QStringLiteral("cloudSyncAction"));
  QVERIFY(action != nullptr);
#if defined(GENPLUSGX_HAVE_CLOUD_SYNC)
  QVERIFY(action->isEnabled());
  action->trigger();
  auto* dialog = window.findChild<ui::CloudSyncDialog*>(
    QStringLiteral("cloudSyncDialog"));
  QVERIFY(dialog != nullptr);
  auto* sync = dialog->findChild<QPushButton*>(QStringLiteral("cloudSyncNowButton"));
  QVERIFY(sync != nullptr);
  QVERIFY(sync->isEnabled());
  auto* open = window.findChild<QAction*>(QStringLiteral("openGameAction"));
  QVERIFY(open != nullptr);
  window.setCloudSyncBusy(true);
  QVERIFY(!open->isEnabled());
  QVERIFY(!sync->isEnabled());
  window.setCloudSyncBusy(false);
  QVERIFY(open->isEnabled());
  window.setGameLoading(std::filesystem::path{"/tmp/synthetic.bin"});
  QVERIFY(!sync->isEnabled());
  window.setGameLoaded(std::filesystem::path{"/tmp/synthetic.bin"});
  QVERIFY(!sync->isEnabled());
  window.setNoGameLoaded();
  QVERIFY(sync->isEnabled());
#else
  QVERIFY(!action->isEnabled());
#endif
}

QTEST_MAIN(CloudSyncDialogTest)
#include "cloud_sync_dialog_test.moc"
