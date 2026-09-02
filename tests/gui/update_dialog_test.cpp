#include "genplusgx/ui/update_dialog.h"

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QTest>

#include <filesystem>

class UpdateDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void secureDefaultsAndSettingsApply();
  void verifiedWorkflowEnablesOnlySafeActions();
  void failuresRemainRecoverable();
};

void UpdateDialogTest::secureDefaultsAndSettingsApply()
{
  genplusgx::ui::UpdateDialog dialog;
  dialog.setCurrentVersion("1.2.3");
  dialog.setSettings(genplusgx::updates::defaultSettings());
  auto* automatic = dialog.findChild<QCheckBox*>(
    QStringLiteral("updateAutomaticCheck"));
  QVERIFY(automatic != nullptr);
  QVERIFY(!automatic->isChecked());
  QVERIFY(!automatic->accessibleDescription().isEmpty());
  QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("updateTrustLabel"))
    ->text().contains(QStringLiteral("Ed25519")));
  genplusgx::updates::Settings saved;
  int saves = 0;
  dialog.setSettingsSink([&](const auto& value) {
    ++saves;
    saved = value;
    return genplusgx::PersistenceStatus{};
  });
  automatic->setChecked(true);
  QTest::mouseClick(dialog.findChild<QPushButton*>(
    QStringLiteral("updateApplyButton")), Qt::LeftButton);
  QCOMPARE(saves, 1);
  QVERIFY(saved.automaticChecks);
}

void UpdateDialogTest::verifiedWorkflowEnablesOnlySafeActions()
{
  genplusgx::ui::UpdateDialog dialog;
  dialog.setCurrentVersion("1.2.3");
  int checks = 0;
  int downloads = 0;
  int pages = 0;
  int files = 0;
  dialog.setCheckSink([&] { ++checks; });
  dialog.setDownloadSink([&](const auto&) { ++downloads; });
  dialog.setUrlSink([&](const auto&) { ++pages; return true; });
  dialog.setFileSink([&](const auto&) { ++files; return true; });
  auto* checkButton = dialog.findChild<QPushButton*>(
    QStringLiteral("updateCheckButton"));
  auto* downloadButton = dialog.findChild<QPushButton*>(
    QStringLiteral("updateDownloadButton"));
  auto* releaseButton = dialog.findChild<QPushButton*>(
    QStringLiteral("updateReleasePageButton"));
  auto* openButton = dialog.findChild<QPushButton*>(
    QStringLiteral("updateOpenPackageButton"));
  QVERIFY(checkButton->isEnabled());
  QVERIFY(!downloadButton->isEnabled());
  QVERIFY(!releaseButton->isEnabled());
  QVERIFY(!openButton->isEnabled());
  QTest::mouseClick(checkButton, Qt::LeftButton);
  QCOMPARE(checks, 1);

  genplusgx::updates::Asset asset{.platform = "linux",
    .architecture = "x86_64", .format = "tar.gz", .fileName = "fixture.tar.gz",
    .url = "https://example.test/fixture.tar.gz", .sha256 = std::string(64, 'a'),
    .size = 42U};
  genplusgx::updates::Manifest manifest{.schemaVersion = 1U,
    .version = {9U, 8U, 7U}, .publishedAt = "2026-09-02T18:00:00.000Z",
    .releasePage = "https://example.test/release", .keyId = "fixture-key",
    .assets = {asset}};
  dialog.presentCheck({.status = {}, .manifest = manifest, .asset = asset,
    .updateAvailable = true});
  QVERIFY(downloadButton->isEnabled());
  QVERIFY(releaseButton->isEnabled());
  QTest::mouseClick(downloadButton, Qt::LeftButton);
  QTest::mouseClick(releaseButton, Qt::LeftButton);
  QCOMPARE(downloads, 1);
  QCOMPARE(pages, 1);

  dialog.presentDownload({.status = {}, .path = "/tmp/fixture.tar.gz",
    .asset = asset});
  QVERIFY(openButton->isEnabled());
  QTest::mouseClick(openButton, Qt::LeftButton);
  QCOMPARE(files, 1);
  QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("updateStatusLabel"))
    ->text().contains(QStringLiteral("Verified")));
}

void UpdateDialogTest::failuresRemainRecoverable()
{
  genplusgx::ui::UpdateDialog dialog;
  dialog.setCurrentVersion("1.2.3");
  dialog.setBusy(true);
  QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("updateCheckButton"))
    ->isEnabled());
  dialog.presentCheckFailure("Synthetic signature rejection.");
  QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("updateCheckButton"))
    ->isEnabled());
  QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("updateDetailLabel"))
    ->text().contains(QStringLiteral("signature")));
  QVERIFY(!dialog.findChild<QPushButton*>(
    QStringLiteral("updateDownloadButton"))->isEnabled());
}

QTEST_MAIN(UpdateDialogTest)
#include "update_dialog_test.moc"
