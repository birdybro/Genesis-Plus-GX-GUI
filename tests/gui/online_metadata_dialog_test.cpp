#include "genplusgx/ui/main_window.h"
#include "genplusgx/ui/online_metadata_dialog.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTest>

#include <cstddef>

class OnlineMetadataDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void optInValidationPersistenceAndDefaults();
  void mainWindowExposesOneDialog();
};

void OnlineMetadataDialogTest::optInValidationPersistenceAndDefaults()
{
  using namespace genplusgx;
  ui::OnlineMetadataDialog dialog;
  std::size_t saveCount = 0U;
  library::OnlineMetadataSettings applied;
  dialog.setSettingsSink([&](const library::OnlineMetadataSettings& settings) {
    ++saveCount;
    applied = settings;
    return PersistenceStatus{};
  });
  dialog.show();
  QApplication::processEvents();

  auto* enabled = dialog.findChild<QCheckBox*>(
    QStringLiteral("onlineMetadataEnabledCheck"));
  auto* automatic = dialog.findChild<QCheckBox*>(
    QStringLiteral("onlineMetadataAutomaticCheck"));
  auto* artwork = dialog.findChild<QCheckBox*>(
    QStringLiteral("onlineMetadataArtworkCheck"));
  auto* provider = dialog.findChild<QComboBox*>(
    QStringLiteral("onlineMetadataProviderCombo"));
  auto* endpoint = dialog.findChild<QLineEdit*>(
    QStringLiteral("onlineMetadataEndpointEdit"));
  auto* language = dialog.findChild<QLineEdit*>(
    QStringLiteral("onlineMetadataLanguageEdit"));
  auto* region = dialog.findChild<QLineEdit*>(
    QStringLiteral("onlineMetadataRegionEdit"));
  auto* cache = dialog.findChild<QSpinBox*>(
    QStringLiteral("onlineMetadataCacheSpin"));
  auto* disclosure = dialog.findChild<QLabel*>(
    QStringLiteral("onlineMetadataDisclosureLabel"));
  auto* error = dialog.findChild<QLabel*>(
    QStringLiteral("onlineMetadataErrorLabel"));
  auto* buttons = dialog.findChild<QDialogButtonBox*>(
    QStringLiteral("onlineMetadataButtonBox"));
  QVERIFY(enabled != nullptr);
  QVERIFY(automatic != nullptr);
  QVERIFY(artwork != nullptr);
  QVERIFY(provider != nullptr);
  QVERIFY(endpoint != nullptr);
  QVERIFY(language != nullptr);
  QVERIFY(region != nullptr);
  QVERIFY(cache != nullptr);
  QVERIFY(disclosure != nullptr);
  QVERIFY(error != nullptr);
  QVERIFY(buttons != nullptr);
  QVERIFY(!enabled->isChecked());
  QVERIFY(!automatic->isEnabled());
  QVERIFY(disclosure->text().contains(QStringLiteral("no ROM bytes")));
  QVERIFY(disclosure->text().contains(QStringLiteral("CC BY-SA 4.0")));
  QVERIFY(!endpoint->accessibleName().isEmpty());

  enabled->setChecked(true);
  automatic->setChecked(true);
  artwork->setChecked(true);
  endpoint->setText(QStringLiteral("http://insecure.example.test"));
  QTest::mouseClick(buttons->button(QDialogButtonBox::Apply), Qt::LeftButton);
  QCOMPARE(saveCount, std::size_t{0});
  QVERIFY(error->isVisible());
  QVERIFY(error->text().contains(QStringLiteral("HTTPS")));

  provider->setCurrentIndex(provider->findData(static_cast<int>(
    library::OnlineMetadataProvider::licensedManifest)));
  endpoint->setText(QStringLiteral("https://metadata.example.test/api"));
  language->setText(QStringLiteral("JA"));
  region->setText(QStringLiteral("JP"));
  cache->setValue(256);
  QTest::mouseClick(buttons->button(QDialogButtonBox::Apply), Qt::LeftButton);
  QCOMPARE(saveCount, std::size_t{1});
  QVERIFY(applied.enabled);
  QVERIFY(applied.automaticLookup);
  QVERIFY(applied.downloadArtwork);
  QCOMPARE(applied.provider,
    library::OnlineMetadataProvider::licensedManifest);
  QCOMPARE(applied.endpoint,
    std::string{"https://metadata.example.test/api"});
  QCOMPARE(applied.preferredLanguage, std::string{"ja"});
  QCOMPARE(applied.preferredRegion, std::string{"jp"});
  QCOMPARE(applied.cacheMegabytes, std::uint32_t{256});
  QVERIFY(!error->isVisible());
  QVERIFY(disclosure->text().contains(QStringLiteral("SHA-256")));

  QTest::mouseClick(dialog.findChild<QPushButton*>(
    QStringLiteral("onlineMetadataRestoreDefaultsButton")), Qt::LeftButton);
  QVERIFY(!enabled->isChecked());
  QCOMPARE(endpoint->text(), QStringLiteral("https://gamedb.retronian.com"));
  QCOMPARE(cache->value(), 128);
}

void OnlineMetadataDialogTest::mainWindowExposesOneDialog()
{
  using namespace genplusgx;
  ui::MainWindow window;
  auto settings = library::defaultOnlineMetadataSettings();
  settings.enabled = true;
  window.setOnlineMetadataSettings(settings);
  std::size_t saveCount = 0U;
  window.setOnlineMetadataSettingsSink(
    [&saveCount](const library::OnlineMetadataSettings&) {
      ++saveCount;
      return PersistenceStatus{};
    });
  window.show();
  auto* action = window.findChild<QAction*>(
    QStringLiteral("onlineMetadataAction"));
  QVERIFY(action != nullptr);
  QVERIFY(action->isEnabled());
  action->trigger();
  QApplication::processEvents();
  auto* dialog = window.findChild<ui::OnlineMetadataDialog*>(
    QStringLiteral("onlineMetadataDialog"));
  QVERIFY(dialog != nullptr);
  QVERIFY(dialog->isVisible());
  QVERIFY(dialog->findChild<QCheckBox*>(
    QStringLiteral("onlineMetadataEnabledCheck"))->isChecked());
  action->trigger();
  QApplication::processEvents();
  QCOMPARE(window.findChildren<ui::OnlineMetadataDialog*>(
    QStringLiteral("onlineMetadataDialog")).size(), 1);
  QCOMPARE(saveCount, std::size_t{0});
}

QTEST_MAIN(OnlineMetadataDialogTest)
#include "online_metadata_dialog_test.moc"
