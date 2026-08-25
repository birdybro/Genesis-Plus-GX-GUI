#include "genplusgx/ui/cheat_manager_dialog.h"
#include "genplusgx/ui/main_window.h"

#include <QAction>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QtTest/QTest>

#include <vector>

class CheatManagerDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void validatesAppliesAndRemovesDefinitions();
  void reportsPersistenceFailureWithoutCommitting();
  void mainWindowOpensOnlyForReadyGameSession();
};

void CheatManagerDialogTest::validatesAppliesAndRemovesDefinitions()
{
  using namespace genplusgx;
  ui::CheatManagerDialog dialog{
    cheats::CheatSystem::genesis,
    {.entries = {{
       .name = "Existing",
       .code = "123456:ABCD",
       .enabled = false,
     }}},
  };
  std::vector<cheats::CheatConfiguration> applied;
  dialog.setConfigurationSink([&applied](const auto& configuration) {
    applied.push_back(configuration);
    return PersistenceStatus{};
  });
  dialog.show();
  QApplication::processEvents();

  auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("cheatTable"));
  auto* add = dialog.findChild<QPushButton*>(QStringLiteral("addCheatButton"));
  auto* remove = dialog.findChild<QPushButton*>(QStringLiteral("removeCheatButton"));
  auto* apply = dialog.findChild<QPushButton*>(QStringLiteral("cheatApplyButton"));
  auto* validation = dialog.findChild<QLabel*>(QStringLiteral("cheatValidationLabel"));
  QVERIFY(table != nullptr);
  QVERIFY(add != nullptr);
  QVERIFY(remove != nullptr);
  QVERIFY(apply != nullptr);
  QVERIFY(validation != nullptr);
  QCOMPARE(table->rowCount(), 1);
  QCOMPARE(table->item(0, 0)->checkState(), Qt::Unchecked);

  QTest::mouseClick(add, Qt::LeftButton);
  QCOMPARE(table->rowCount(), 2);
  table->item(1, 0)->setCheckState(Qt::Checked);
  table->item(1, 1)->setText(QStringLiteral("Infinite lives"));
  table->item(1, 2)->setText(QStringLiteral(" baaa-aaaa "));
  QTest::mouseClick(apply, Qt::LeftButton);
  QCOMPARE(applied.size(), 1U);
  QCOMPARE(applied.front().entries.size(), 2U);
  QCOMPARE(dialog.configuration().entries[1].code, std::string{"BAAA-AAAA"});
  QCOMPARE(table->item(1, 2)->text(), QStringLiteral("BAAA-AAAA"));
  QVERIFY(!validation->isVisible());

  table->item(1, 2)->setText(QStringLiteral("INVALID"));
  QTest::mouseClick(apply, Qt::LeftButton);
  QCOMPARE(applied.size(), 1U);
  QVERIFY(validation->isVisible());
  QVERIFY(validation->text().contains(QStringLiteral("supported format")));

  table->selectRow(1);
  QTest::mouseClick(remove, Qt::LeftButton);
  QCOMPARE(table->rowCount(), 1);
  QTest::mouseClick(apply, Qt::LeftButton);
  QCOMPARE(applied.size(), 2U);
  QCOMPARE(applied.back().entries.size(), 1U);
}

void CheatManagerDialogTest::reportsPersistenceFailureWithoutCommitting()
{
  genplusgx::ui::CheatManagerDialog dialog{
    genplusgx::cheats::CheatSystem::masterSystem,
    {.entries = {{
       .name = "Valid",
       .code = "C000:7F",
       .enabled = true,
     }}},
  };
  dialog.setConfigurationSink([](const auto&) {
    return genplusgx::PersistenceStatus{
      .error = genplusgx::PersistenceError::fileWriteFailed,
      .message = "Synthetic write failure",
    };
  });
  dialog.show();
  auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("cheatTable"));
  table->item(0, 1)->setText(QStringLiteral("Changed"));
  QTest::mouseClick(
    dialog.findChild<QPushButton*>(QStringLiteral("cheatApplyButton")), Qt::LeftButton);
  const auto* validation =
    dialog.findChild<QLabel*>(QStringLiteral("cheatValidationLabel"));
  QVERIFY(validation->isVisible());
  QCOMPARE(validation->text(), QStringLiteral("Synthetic write failure"));
  QCOMPARE(dialog.configuration().entries.front().name, std::string{"Valid"});
}

void CheatManagerDialogTest::mainWindowOpensOnlyForReadyGameSession()
{
  genplusgx::ui::MainWindow window;
  window.show();
  auto* action = window.findChild<QAction*>(QStringLiteral("cheatsAction"));
  QVERIFY(action != nullptr);
  QVERIFY(!action->isEnabled());

  window.setGameLoaded("synthetic.bin");
  QVERIFY(!action->isEnabled());
  genplusgx::cheats::CheatConfiguration initial{
    .entries = {{
      .name = "Marker",
      .code = "123456:ABCD",
      .enabled = false,
    }},
  };
  std::vector<genplusgx::cheats::CheatConfiguration> applied;
  window.setCheatConfigurationSink([&applied](const auto& configuration) {
    applied.push_back(configuration);
    return genplusgx::PersistenceStatus{};
  });
  window.setCheatSession(genplusgx::cheats::CheatSystem::genesis, initial);
  QVERIFY(action->isEnabled());
  action->trigger();
  QApplication::processEvents();

  auto* dialog = window.findChild<genplusgx::ui::CheatManagerDialog*>(
    QStringLiteral("cheatManagerDialog"));
  QVERIFY(dialog != nullptr);
  QVERIFY(dialog->isVisible());
  auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("cheatTable"));
  table->item(0, 0)->setCheckState(Qt::Checked);
  QTest::mouseClick(dialog->findChild<QPushButton*>(QStringLiteral("cheatApplyButton")),
    Qt::LeftButton);
  QCOMPARE(applied.size(), 1U);
  QVERIFY(applied.front().entries.front().enabled);

  window.setGameLoading("replacement.bin");
  QApplication::processEvents();
  QVERIFY(!action->isEnabled());
  QVERIFY(window.findChild<genplusgx::ui::CheatManagerDialog*>(
            QStringLiteral("cheatManagerDialog")) == nullptr);

  window.setGameLoaded("synthetic.bin");
  QVERIFY(action->isEnabled());
  action->trigger();
  QApplication::processEvents();
  QVERIFY(window.findChild<genplusgx::ui::CheatManagerDialog*>(
            QStringLiteral("cheatManagerDialog")) != nullptr);

  window.clearCheatSession();
  QApplication::processEvents();
  QVERIFY(!action->isEnabled());
  QVERIFY(window.findChild<genplusgx::ui::CheatManagerDialog*>(
            QStringLiteral("cheatManagerDialog")) == nullptr);
}

QTEST_MAIN(CheatManagerDialogTest)

#include "cheat_manager_dialog_test.moc"
