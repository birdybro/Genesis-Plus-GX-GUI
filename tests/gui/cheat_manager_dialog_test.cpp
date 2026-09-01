#include "genplusgx/ui/cheat_manager_dialog.h"
#include "genplusgx/ui/dialog_service.h"
#include "genplusgx/ui/main_window.h"

#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QtTest/QTest>

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr int enabledColumn = 0;
constexpr int codeColumn = 2;

std::filesystem::path temporaryPath(const QTemporaryDir& directory)
{
#if defined(Q_OS_WIN)
  return std::filesystem::path{directory.path().toStdWString()};
#else
  return std::filesystem::path{directory.path().toUtf8().constData()};
#endif
}

bool writeText(const std::filesystem::path& path, std::string_view text)
{
  return genplusgx::writeFileAtomically(path,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(text.data()), text.size()},
    genplusgx::cheats::maximumCheatImportBytes);
}

class FakeCheatDialogService final : public genplusgx::ui::DialogService {
public:
  std::optional<std::filesystem::path> importPath;

  std::optional<std::filesystem::path> chooseGame(
    QWidget*, const std::filesystem::path&) override
  {
    return std::nullopt;
  }

  std::optional<std::filesystem::path> chooseCheatImport(
    QWidget*, const std::filesystem::path&) override
  {
    return importPath;
  }

  void showError(QWidget*, const QString&, const QString&) override {}
};

} // namespace

class CheatManagerDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void validatesAppliesAndRemovesDefinitions();
  void reportsPersistenceFailureWithoutCommitting();
  void importsAndSearchesWithoutImplicitPatches();
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

void CheatManagerDialogTest::importsAndSearchesWithoutImplicitPatches()
{
  using namespace genplusgx;
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto listPath = temporaryPath(temporary) / "local.cht";
  QVERIFY(writeText(listPath, R"(
cheats = 2
cheat0_desc = "Infinite lives"
cheat0_code = "BAAA-AAAA"
cheat0_enable = "true"
cheat1_desc = "Alternate mode"
cheat1_code = "123456:ABCD"
cheat1_enable = "false"
)"));

  ui::CheatManagerDialog dialog{cheats::CheatSystem::genesis, {}};
  auto dialogs = std::make_shared<FakeCheatDialogService>();
  dialogs->importPath = listPath;
  dialog.setDialogService(dialogs, temporaryPath(temporary));
  std::vector<cheats::CheatConfiguration> applied;
  dialog.setConfigurationSink([&applied](const auto& configuration) {
    applied.push_back(configuration);
    return PersistenceStatus{};
  });
  std::vector<CoreDebugRequest> requests;
  dialog.setDebugRequestSink([&requests](CoreDebugRequest request) {
    requests.push_back(std::move(request));
    return true;
  });
  dialog.show();
  QApplication::processEvents();

  auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("cheatTable"));
  auto* validation = dialog.findChild<QLabel*>(
    QStringLiteral("cheatValidationLabel"));
  QVERIFY(table != nullptr && validation != nullptr);
  QTest::mouseClick(dialog.findChild<QPushButton*>(
    QStringLiteral("importCheatListButton")), Qt::LeftButton);
  QCOMPARE(table->rowCount(), 2);
  QCOMPARE(table->item(0, enabledColumn)->checkState(), Qt::Unchecked);
  QCOMPARE(table->item(1, enabledColumn)->checkState(), Qt::Unchecked);
  QVERIFY(validation->text().contains(QStringLiteral("Imported 2 disabled")));
  QVERIFY(applied.empty());

  QTest::mouseClick(dialog.findChild<QPushButton*>(
    QStringLiteral("importCheatListButton")), Qt::LeftButton);
  QCOMPARE(table->rowCount(), 2);
  QVERIFY(validation->text().contains(QStringLiteral("skipped 2 duplicate")));
  QVERIFY(applied.empty());

  auto* newSearch = dialog.findChild<QPushButton*>(
    QStringLiteral("cheatSearchNewButton"));
  auto* filterSearch = dialog.findChild<QPushButton*>(
    QStringLiteral("cheatSearchFilterButton"));
  auto* results = dialog.findChild<QTableWidget*>(
    QStringLiteral("cheatSearchResultsTable"));
  QVERIFY(newSearch != nullptr && filterSearch != nullptr && results != nullptr);
  QTest::mouseClick(newSearch, Qt::LeftButton);
  QCOMPARE(requests.size(), 1U);
  QCOMPARE(requests.back().type, CoreDebugRequestType::captureSnapshot);
  QVERIFY(requests.back().clientToken != 0U);

  auto snapshot = std::make_shared<CoreDebugSnapshot>();
  snapshot->m68kActive = true;
  snapshot->m68kRam[0x20U] = 0x12U;
  snapshot->m68kRam[0x21U] = 0x34U;
  CoreDebugResponse wrongClient;
  wrongClient.type = CoreDebugRequestType::captureSnapshot;
  wrongClient.snapshot = snapshot;
  wrongClient.clientToken = requests.back().clientToken + 1U;
  dialog.presentDebugResponse(std::move(wrongClient));
  QCOMPARE(results->rowCount(), 0);

  CoreDebugResponse initial;
  initial.type = CoreDebugRequestType::captureSnapshot;
  initial.snapshot = snapshot;
  initial.clientToken = requests.back().clientToken;
  dialog.presentDebugResponse(std::move(initial));
  QCOMPARE(results->rowCount(), 1'024);
  QVERIFY(filterSearch->isEnabled());

  dialog.findChild<QLineEdit*>(QStringLiteral("cheatSearchValueEdit"))
    ->setText(QStringLiteral("0x1234"));
  QTest::mouseClick(filterSearch, Qt::LeftButton);
  QCOMPARE(requests.size(), 2U);
  CoreDebugResponse filtered;
  filtered.type = CoreDebugRequestType::captureSnapshot;
  filtered.snapshot = snapshot;
  filtered.clientToken = requests.back().clientToken;
  dialog.presentDebugResponse(std::move(filtered));
  QCOMPARE(results->rowCount(), 1);
  QCOMPARE(results->item(0, 0)->text(), QStringLiteral("0XFF0020"));
  QCOMPARE(results->item(0, 1)->text(), QStringLiteral("0X1234"));

  results->selectRow(0);
  QTest::mouseClick(dialog.findChild<QPushButton*>(
    QStringLiteral("cheatSearchAddButton")), Qt::LeftButton);
  QCOMPARE(table->rowCount(), 3);
  QCOMPARE(table->item(2, enabledColumn)->checkState(), Qt::Unchecked);
  QCOMPARE(table->item(2, codeColumn)->text(), QStringLiteral("FF0020:1234"));
  QVERIFY(applied.empty());

  table->item(2, enabledColumn)->setCheckState(Qt::Checked);
  QTest::mouseClick(dialog.findChild<QPushButton*>(
    QStringLiteral("cheatApplyButton")), Qt::LeftButton);
  QCOMPARE(applied.size(), 1U);
  QCOMPARE(applied.back().entries.size(), 3U);
  QVERIFY(!applied.back().entries[0].enabled);
  QVERIFY(applied.back().entries[2].enabled);

  const auto invalidPath = temporaryPath(temporary) / "invalid.txt";
  QVERIFY(writeText(invalidPath, "Unsafe | not-a-code\n"));
  dialogs->importPath = invalidPath;
  QTest::mouseClick(dialog.findChild<QPushButton*>(
    QStringLiteral("importCheatListButton")), Qt::LeftButton);
  QCOMPARE(table->rowCount(), 3);
  QVERIFY(validation->text().contains(QStringLiteral("invalid for the loaded system")));
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
  std::vector<genplusgx::CoreDebugRequest> debugRequests;
  window.setDebugRequestSink([&debugRequests](genplusgx::CoreDebugRequest request) {
    debugRequests.push_back(std::move(request));
    return true;
  });
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
  QTest::mouseClick(dialog->findChild<QPushButton*>(
    QStringLiteral("cheatSearchNewButton")), Qt::LeftButton);
  QCOMPARE(debugRequests.size(), 1U);
  auto snapshot = std::make_shared<genplusgx::CoreDebugSnapshot>();
  snapshot->m68kActive = true;
  genplusgx::CoreDebugResponse snapshotResponse;
  snapshotResponse.type = genplusgx::CoreDebugRequestType::captureSnapshot;
  snapshotResponse.clientToken = debugRequests.back().clientToken;
  snapshotResponse.snapshot = std::move(snapshot);
  window.presentDebugResponse(std::move(snapshotResponse));
  QCOMPARE(dialog->findChild<QTableWidget*>(
    QStringLiteral("cheatSearchResultsTable"))->rowCount(), 1'024);
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
