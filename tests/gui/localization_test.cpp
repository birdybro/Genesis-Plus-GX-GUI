#include "genplusgx/localization/localization.h"
#include "genplusgx/ui/appearance_settings_dialog.h"
#include "genplusgx/ui/main_window.h"
#include "genplusgx/ui/settings_dialog.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

namespace {

class LocalizationGuiTest final : public QObject {
  Q_OBJECT

private slots:
  void pseudoLocaleTranslatesWithoutChangingAutomationIdentity();
  void englishFallbackAndRightToLeftLayoutRemainUsable();
};

QString stageCatalog(QTemporaryDir& directory)
{
  const auto source = qEnvironmentVariable("GENPLUSGX_TEST_PSEUDO_QM");
  if (source.isEmpty()) {
    return {};
  }
  const auto destination = QDir{directory.path()}.filePath(
    QStringLiteral("genplusgx_en_XA.qm"));
  return QFile::copy(source, destination) ? destination : QString{};
}

void LocalizationGuiTest::pseudoLocaleTranslatesWithoutChangingAutomationIdentity()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  QVERIFY(!stageCatalog(directory).isEmpty());
  genplusgx::localization::TranslationManager translations{
    *qApp, {directory.path()}};
  QVERIFY(translations.apply(genplusgx::localization::pseudoLanguage));
  QApplication::setLayoutDirection(Qt::LeftToRight);

  genplusgx::ui::MainWindow window;
  window.resize(1280, 900);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));

  auto* fileMenu = window.findChild<QMenu*>(QStringLiteral("fileMenu"));
  auto* openGame = window.findChild<QAction*>(QStringLiteral("openGameAction"));
  auto* settings = window.findChild<QAction*>(QStringLiteral("settingsAction"));
  QVERIFY(fileMenu != nullptr);
  QVERIFY(openGame != nullptr);
  QVERIFY(settings != nullptr);
  QVERIFY(fileMenu->title().startsWith(QStringLiteral("⟦")));
  QVERIFY(fileMenu->title() != QStringLiteral("&File"));
  QVERIFY(openGame->text().startsWith(QStringLiteral("⟦")));
  QCOMPARE(openGame->objectName(), QStringLiteral("openGameAction"));
  const auto namedActions = window.findChildren<QAction*>();
  for (const auto* action : namedActions) {
    if (!action->objectName().endsWith(QStringLiteral("Action")) ||
        action->text().isEmpty()) {
      continue;
    }
    const bool permittedTechnicalIdentity =
      action->text().size() == 1 && action->text().front().isDigit();
    QVERIFY2(action->text().startsWith(QStringLiteral("⟦")) ||
        permittedTechnicalIdentity,
      qPrintable(QStringLiteral("Named action remained untranslated: %1 = %2")
          .arg(action->objectName(), action->text())));
  }
  auto* canvas = window.findChild<QWidget*>(QStringLiteral("emulatorCanvas"));
  auto* emptyCanvas = window.findChild<QLabel*>(
    QStringLiteral("emptyCanvasLabel"));
  QVERIFY(canvas != nullptr);
  QVERIFY(emptyCanvas != nullptr);
  QVERIFY(canvas->accessibleName().startsWith(QStringLiteral("⟦")));
  QVERIFY(emptyCanvas->text().startsWith(QStringLiteral("⟦")));

  settings->trigger();
  QApplication::processEvents();
  auto* settingsDialog = window.findChild<genplusgx::ui::SettingsDialog*>(
    QStringLiteral("settingsDialog"));
  QVERIFY(settingsDialog != nullptr);
  auto* categories = settingsDialog->findChild<QListWidget*>(
    QStringLiteral("settingsCategoryList"));
  QVERIFY(categories != nullptr);
  QCOMPARE(categories->count(), 8);
  QVERIFY(categories->item(0)->text().startsWith(QStringLiteral("⟦")));
  QVERIFY(settingsDialog->sizeHint().width() <= 1'400);

  QTest::mouseClick(settingsDialog->findChild<QPushButton*>(
    QStringLiteral("configureAppearanceButton")), Qt::LeftButton);
  QApplication::processEvents();
  auto* appearance = window.findChild<
    genplusgx::ui::AppearanceSettingsDialog*>(
      QStringLiteral("appearanceSettingsDialog"));
  QVERIFY(appearance != nullptr);
  auto* language = appearance->findChild<QComboBox*>(
    QStringLiteral("languageCombo"));
  auto* languageLabel = appearance->findChild<QLabel*>(
    QStringLiteral("languageLabel"));
  QVERIFY(language != nullptr);
  QVERIFY(languageLabel != nullptr);
  QCOMPARE(language->count(), 3);
  QCOMPARE(language->itemData(0).toString(), QStringLiteral("system"));
  QCOMPARE(language->itemData(1).toString(), QStringLiteral("en"));
  QCOMPARE(language->itemData(2).toString(), QStringLiteral("en_XA"));
  QCOMPARE(languageLabel->buddy(), language);
  QVERIFY(languageLabel->text().startsWith(QStringLiteral("⟦")));
  QVERIFY(appearance->sizeHint().width() <= 1'400);

  const auto wordWrappedLabels = appearance->findChildren<QLabel*>();
  QVERIFY(std::ranges::any_of(wordWrappedLabels, [](const auto* label) {
    return label->wordWrap();
  }));
  appearance->reject();
  settingsDialog->reject();
}

void LocalizationGuiTest::englishFallbackAndRightToLeftLayoutRemainUsable()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  genplusgx::localization::TranslationManager translations{
    *qApp, {directory.path()}};
  QVERIFY(translations.apply(
    genplusgx::localization::systemLanguage, {QStringLiteral("ar-EG")}));
  QVERIFY(translations.usedEnglishFallback());
  QVERIFY(translations.effectiveLanguage() == std::string{"en"});

  QApplication::setLayoutDirection(Qt::RightToLeft);
  genplusgx::ui::MainWindow window;
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  QCOMPARE(window.layoutDirection(), Qt::RightToLeft);
  auto* fileMenu = window.findChild<QMenu*>(QStringLiteral("fileMenu"));
  auto* settings = window.findChild<QAction*>(QStringLiteral("settingsAction"));
  QVERIFY(fileMenu != nullptr);
  QVERIFY(settings != nullptr);
  QCOMPARE(fileMenu->title(), QStringLiteral("&File"));
  QCOMPARE(fileMenu->objectName(), QStringLiteral("fileMenu"));

  settings->trigger();
  QApplication::processEvents();
  auto* settingsDialog = window.findChild<genplusgx::ui::SettingsDialog*>(
    QStringLiteral("settingsDialog"));
  QVERIFY(settingsDialog != nullptr);
  QCOMPARE(settingsDialog->layoutDirection(), Qt::RightToLeft);
  auto* categories = settingsDialog->findChild<QListWidget*>(
    QStringLiteral("settingsCategoryList"));
  QVERIFY(categories != nullptr);
  QCOMPARE(categories->currentRow(), 0);
  QTest::keyClick(categories, Qt::Key_Down);
  QCOMPARE(categories->currentRow(), 1);
  QCOMPARE(settingsDialog->findChild<QPushButton*>(
    QStringLiteral("configureVideoButton"))->objectName(),
    QStringLiteral("configureVideoButton"));
  settingsDialog->reject();
  QApplication::setLayoutDirection(Qt::LeftToRight);
}

} // namespace

QTEST_MAIN(LocalizationGuiTest)
#include "localization_test.moc"
