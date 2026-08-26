#include "genplusgx/emulation_worker.h"
#include "genplusgx/ui/main_window.h"

#include "synthetic_rom.h"

#include <QAction>
#include <QStatusBar>
#include <QTest>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

using namespace std::chrono_literals;

namespace {

std::optional<genplusgx::EmulationEvent> waitForOperation(
  genplusgx::EmulationWorker& worker,
  std::uint64_t operationId)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = worker.waitForEvent(100ms);
    if (event && event->operationId == operationId) {
      return event;
    }
  }
  return std::nullopt;
}

void synchronize(
  genplusgx::ui::MainWindow& window,
  const genplusgx::EmulationEvent& event)
{
  window.setEmulationControlState(
    event.workerState == genplusgx::EmulationWorkerState::paused,
    event.fastForward);
}

class EmulationControlsTest final : public QObject {
  Q_OBJECT

private slots:
  void actionsDriveLiveWorkerAndTrackCanonicalState();
  void holdHotkeyIsMomentaryAndFocusSafe();
  void rejectedCommandsRollBackOptimisticActionState();
};

void EmulationControlsTest::actionsDriveLiveWorkerAndTrackCanonicalState()
{
  genplusgx::test::TemporaryFixture game{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  genplusgx::EmulationWorker worker;
  QVERIFY(worker.start());
  const auto workerStarted = worker.waitForEvent(2s);
  QVERIFY(workerStarted.has_value());
  QCOMPARE(workerStarted->type, genplusgx::EmulationEventType::workerStarted);

  constexpr std::uint64_t loadOperation = 1U;
  QVERIFY(worker.submit(genplusgx::EmulationCommand::load(
    loadOperation, game.path())));
  const auto loaded = waitForOperation(worker, loadOperation);
  QVERIFY(loaded && loaded->succeeded());
  QCOMPARE(loaded->workerState, genplusgx::EmulationWorkerState::paused);

  genplusgx::ui::MainWindow window;
  std::uint64_t nextOperation = 100U;
  std::uint64_t submittedOperation = 0U;
  std::vector<genplusgx::ui::EmulationUiOperation> requested;
  window.setEmulationControlSink(
    [&worker, &nextOperation, &requested, &submittedOperation](
      genplusgx::ui::EmulationUiOperation operation, bool enabled) {
      using UiOperation = genplusgx::ui::EmulationUiOperation;
      using CommandType = genplusgx::EmulationCommandType;
      submittedOperation = ++nextOperation;
      requested.push_back(operation);
      genplusgx::EmulationCommand command;
      switch (operation) {
        case UiOperation::pause:
          command = genplusgx::EmulationCommand::simple(
            CommandType::pause, submittedOperation);
          break;
        case UiOperation::resume:
          command = genplusgx::EmulationCommand::simple(
            CommandType::resume, submittedOperation);
          break;
        case UiOperation::hardReset:
          command = genplusgx::EmulationCommand::simple(
            CommandType::hardReset, submittedOperation);
          break;
        case UiOperation::softReset:
          command = genplusgx::EmulationCommand::simple(
            CommandType::softReset, submittedOperation);
          break;
        case UiOperation::frameAdvance:
          command = genplusgx::EmulationCommand::simple(
            CommandType::frameAdvance, submittedOperation);
          break;
        case UiOperation::setFastForward:
          command = genplusgx::EmulationCommand::fastForward(
            submittedOperation, enabled);
          break;
      }
      return worker.submit(std::move(command)).ok();
    });
  window.setGameLoaded(game.path());
  synchronize(window, *loaded);
  window.show();
  QApplication::processEvents();

  auto* pause = window.findChild<QAction*>(QStringLiteral("pauseAction"));
  auto* reset = window.findChild<QAction*>(QStringLiteral("resetAction"));
  auto* softReset = window.findChild<QAction*>(QStringLiteral("softResetAction"));
  auto* fastForward =
    window.findChild<QAction*>(QStringLiteral("fastForwardAction"));
  auto* frameAdvance =
    window.findChild<QAction*>(QStringLiteral("frameAdvanceAction"));
  QVERIFY(pause && reset && softReset && fastForward && frameAdvance);
  QVERIFY(pause->isChecked());
  QVERIFY(pause->text().contains(QStringLiteral("Resume")));
  QVERIFY(frameAdvance->isEnabled());

  frameAdvance->trigger();
  auto completed = waitForOperation(worker, submittedOperation);
  QVERIFY(completed && completed->succeeded());
  QCOMPARE(completed->command,
    std::optional{genplusgx::EmulationCommandType::frameAdvance});
  QCOMPARE(completed->frameNumber, 1U);
  synchronize(window, *completed);

  pause->trigger();
  completed = waitForOperation(worker, submittedOperation);
  QVERIFY(completed && completed->succeeded());
  QCOMPARE(completed->command,
    std::optional{genplusgx::EmulationCommandType::resume});
  QCOMPARE(completed->workerState, genplusgx::EmulationWorkerState::running);
  synchronize(window, *completed);
  QVERIFY(!pause->isChecked());
  QVERIFY(!frameAdvance->isEnabled());

  reset->trigger();
  completed = waitForOperation(worker, submittedOperation);
  QVERIFY(completed && completed->succeeded());
  QCOMPARE(completed->command,
    std::optional{genplusgx::EmulationCommandType::hardReset});
  synchronize(window, *completed);

  softReset->trigger();
  completed = waitForOperation(worker, submittedOperation);
  QVERIFY(completed && completed->succeeded());
  QCOMPARE(completed->command,
    std::optional{genplusgx::EmulationCommandType::softReset});
  synchronize(window, *completed);

  fastForward->trigger();
  completed = waitForOperation(worker, submittedOperation);
  QVERIFY(completed && completed->succeeded());
  QCOMPARE(completed->command,
    std::optional{genplusgx::EmulationCommandType::setFastForward});
  QVERIFY(completed->fastForward);
  synchronize(window, *completed);
  QVERIFY(fastForward->isChecked());

  fastForward->trigger();
  completed = waitForOperation(worker, submittedOperation);
  QVERIFY(completed && completed->succeeded());
  QVERIFY(!completed->fastForward);
  synchronize(window, *completed);
  QVERIFY(!fastForward->isChecked());

  pause->trigger();
  completed = waitForOperation(worker, submittedOperation);
  QVERIFY(completed && completed->succeeded());
  QCOMPARE(completed->command,
    std::optional{genplusgx::EmulationCommandType::pause});
  synchronize(window, *completed);
  QVERIFY(pause->isChecked());
  QVERIFY(frameAdvance->isEnabled());

  QCOMPARE(requested.size(), std::size_t{7});
  window.setNoGameLoaded();
  QVERIFY(!pause->isEnabled());
  QVERIFY(!reset->isEnabled());
  QVERIFY(!softReset->isEnabled());
  QVERIFY(!fastForward->isEnabled());
  QVERIFY(!frameAdvance->isEnabled());

  QVERIFY(worker.submit(genplusgx::EmulationCommand::simple(
    genplusgx::EmulationCommandType::unloadGame, ++nextOperation)));
  completed = waitForOperation(worker, nextOperation);
  QVERIFY(completed && completed->succeeded());
  QVERIFY(worker.stop());
}

void EmulationControlsTest::holdHotkeyIsMomentaryAndFocusSafe()
{
  using Operation = genplusgx::ui::EmulationUiOperation;
  genplusgx::ui::MainWindow window;
  std::vector<std::pair<Operation, bool>> requests;
  window.setEmulationControlSink(
    [&requests](Operation operation, bool enabled) {
      requests.emplace_back(operation, enabled);
      return true;
    });
  window.setGameLoaded(std::filesystem::path{"synthetic.md"});
  window.setEmulationControlState(false, false);
  window.show();
  window.activateWindow();
  QApplication::processEvents();

  auto* toggle = window.findChild<QAction*>(QStringLiteral("fastForwardAction"));
  QVERIFY(toggle != nullptr);
  QCOMPARE(toggle->shortcut(), QKeySequence{Qt::Key_QuoteLeft});

  QTest::keyPress(&window, Qt::Key_Tab);
  QCOMPARE(requests.size(), std::size_t{1U});
  QCOMPARE(requests.back(), std::make_pair(Operation::setFastForward, true));
  QVERIFY(!toggle->isChecked());
  QTest::keyRelease(&window, Qt::Key_Tab);
  QCOMPARE(requests.size(), std::size_t{2U});
  QCOMPARE(requests.back(), std::make_pair(Operation::setFastForward, false));

  toggle->trigger();
  QCOMPARE(requests.size(), std::size_t{3U});
  QCOMPARE(requests.back(), std::make_pair(Operation::setFastForward, true));
  QVERIFY(toggle->isChecked());
  QTest::keyPress(&window, Qt::Key_Tab);
  QTest::keyRelease(&window, Qt::Key_Tab);
  QCOMPARE(requests.size(), std::size_t{3U});
  QVERIFY(toggle->isChecked());
  toggle->trigger();
  QCOMPARE(requests.size(), std::size_t{4U});
  QCOMPARE(requests.back(), std::make_pair(Operation::setFastForward, false));

  QTest::keyPress(&window, Qt::Key_Tab);
  QCOMPARE(requests.size(), std::size_t{5U});
  QEvent deactivate{QEvent::WindowDeactivate};
  QApplication::sendEvent(&window, &deactivate);
  QCOMPARE(requests.size(), std::size_t{6U});
  QCOMPARE(requests.back(), std::make_pair(Operation::setFastForward, false));
  QTest::keyRelease(&window, Qt::Key_Tab);
  QCOMPARE(requests.size(), std::size_t{6U});

  auto custom = genplusgx::input::defaultInputConfiguration();
  auto hold = std::ranges::find_if(custom.hotkeys, [](const auto& binding) {
    return binding.action ==
      genplusgx::input::EmulatorHotkeyAction::fastForwardHold;
  });
  QVERIFY(hold != custom.hotkeys.end());
  hold->keyCombination = QKeyCombination{
    Qt::ControlModifier, Qt::Key_G}.toCombined();
  window.setInputConfiguration(custom);
  QTest::keyPress(&window, Qt::Key_Tab);
  QTest::keyRelease(&window, Qt::Key_Tab);
  QCOMPARE(requests.size(), std::size_t{6U});
  QTest::keyPress(&window, Qt::Key_G, Qt::ControlModifier);
  QTest::keyRelease(&window, Qt::Key_G, Qt::ControlModifier);
  QCOMPARE(requests.size(), std::size_t{8U});
  QCOMPARE(requests[6U], std::make_pair(Operation::setFastForward, true));
  QCOMPARE(requests[7U], std::make_pair(Operation::setFastForward, false));

  window.setNoGameLoaded();
  QTest::keyPress(&window, Qt::Key_G, Qt::ControlModifier);
  QTest::keyRelease(&window, Qt::Key_G, Qt::ControlModifier);
  QCOMPARE(requests.size(), std::size_t{8U});
}

void EmulationControlsTest::rejectedCommandsRollBackOptimisticActionState()
{
  genplusgx::ui::MainWindow window;
  std::vector<genplusgx::ui::EmulationUiOperation> requests;
  window.setEmulationControlSink(
    [&requests](genplusgx::ui::EmulationUiOperation operation, bool) {
      requests.push_back(operation);
      return false;
    });
  window.setGameLoaded(std::filesystem::path{"synthetic.md"});
  window.setEmulationControlState(false, false);

  auto* pause = window.findChild<QAction*>(QStringLiteral("pauseAction"));
  auto* fastForward =
    window.findChild<QAction*>(QStringLiteral("fastForwardAction"));
  auto* frameAdvance =
    window.findChild<QAction*>(QStringLiteral("frameAdvanceAction"));
  QVERIFY(pause && fastForward && frameAdvance);
  QVERIFY(pause->isEnabled() && fastForward->isEnabled());
  QVERIFY(!frameAdvance->isEnabled());

  pause->trigger();
  QVERIFY(!pause->isChecked());
  QVERIFY(!frameAdvance->isEnabled());
  fastForward->trigger();
  QVERIFY(!fastForward->isChecked());
  QCOMPARE(requests.size(), std::size_t{2});
  QVERIFY(window.statusBar()->currentMessage().contains(
    QStringLiteral("could not be queued")));

  window.setEmulationControlSink({});
  QVERIFY(!pause->isEnabled());
  QVERIFY(!fastForward->isEnabled());
}

} // namespace

QTEST_MAIN(EmulationControlsTest)

#include "emulation_controls_test.moc"
