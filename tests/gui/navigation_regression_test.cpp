#include "genplusgx/ui/help_dialog.h"
#include "genplusgx/ui/main_window.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QTest>

namespace {

class NavigationRegressionTest final : public QObject {
  Q_OBJECT

private slots:
  void productionActionsHaveUniqueAutomationAndShortcutIdentities();
  void helpActionsOpenCompleteKeyboardAccessibleReferences();
};

void NavigationRegressionTest::productionActionsHaveUniqueAutomationAndShortcutIdentities()
{
  genplusgx::ui::MainWindow window;
  QSet<QString> objectNames;
  QSet<QString> shortcuts;
  int productionActionCount = 0;
  for (auto* action : window.findChildren<QAction*>()) {
    if (action->isSeparator() || action->menu() != nullptr) {
      continue;
    }
    ++productionActionCount;
    const auto name = action->objectName();
    QVERIFY2(!name.isEmpty(), action->text().toUtf8().constData());
    QVERIFY2(!objectNames.contains(name), name.toUtf8().constData());
    objectNames.insert(name);

    const auto shortcut = action->shortcut().toString(QKeySequence::PortableText);
    if (!shortcut.isEmpty()) {
      QVERIFY2(!shortcuts.contains(shortcut), shortcut.toUtf8().constData());
      shortcuts.insert(shortcut);
    }
  }
  QVERIFY(productionActionCount >= 60);
  QCOMPARE(objectNames.size(), productionActionCount);
  QVERIFY(shortcuts.size() >= 20);
}

void NavigationRegressionTest::helpActionsOpenCompleteKeyboardAccessibleReferences()
{
  genplusgx::ui::MainWindow window;
  window.show();
  QApplication::processEvents();

  auto* userGuideAction =
    window.findChild<QAction*>(QStringLiteral("userGuideAction"));
  auto* shortcutsAction =
    window.findChild<QAction*>(QStringLiteral("keyboardShortcutsAction"));
  QVERIFY(userGuideAction && shortcutsAction);

  userGuideAction->trigger();
  QApplication::processEvents();
  auto* guide = window.findChild<genplusgx::ui::HelpDialog*>(
    QStringLiteral("userGuideDialog"));
  QVERIFY(guide && guide->isVisible() && guide->isModal());
  auto* guideContent = guide->findChild<QPlainTextEdit*>(
    QStringLiteral("userGuideContent"));
  QVERIFY(guideContent && guideContent->isReadOnly());
  QVERIFY(!guideContent->accessibleName().isEmpty());
  QVERIFY(guideContent->toPlainText().contains(QStringLiteral("Sega CD")));
  QVERIFY(guideContent->toPlainText().contains(QStringLiteral("No ROMs")));

  userGuideAction->trigger();
  QApplication::processEvents();
  QCOMPARE(window.findChildren<genplusgx::ui::HelpDialog*>(
    QStringLiteral("userGuideDialog")).size(), 1);
  QTest::mouseClick(guide->findChild<QPushButton*>(
    QStringLiteral("closeUserGuideButton")), Qt::LeftButton);
  QApplication::processEvents();

  shortcutsAction->trigger();
  QApplication::processEvents();
  auto* shortcuts = window.findChild<genplusgx::ui::HelpDialog*>(
    QStringLiteral("keyboardShortcutsDialog"));
  QVERIFY(shortcuts && shortcuts->isVisible() && shortcuts->isModal());
  const QPointer<genplusgx::ui::HelpDialog> shortcutsGuard{shortcuts};
  auto* shortcutContent = shortcuts->findChild<QPlainTextEdit*>(
    QStringLiteral("keyboardShortcutsContent"));
  QVERIFY(shortcutContent && shortcutContent->isReadOnly());
  QVERIFY(!shortcutContent->accessibleName().isEmpty());
  QVERIFY(shortcutContent->toPlainText().contains(QStringLiteral("Open game")));
  QVERIFY(shortcutContent->toPlainText().contains(QStringLiteral("Fullscreen")));
  QVERIFY(shortcutContent->toPlainText().contains(QStringLiteral("Slow motion")));
  QTest::keyClick(shortcuts, Qt::Key_Escape);
  QApplication::processEvents();
  QVERIFY(shortcutsGuard.isNull() || !shortcutsGuard->isVisible());
}

} // namespace

QTEST_MAIN(NavigationRegressionTest)

#include "navigation_regression_test.moc"
