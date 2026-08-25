#include "genplusgx/emulation_worker.h"
#include "genplusgx/input/keyboard_input.h"
#include "synthetic_rom.h"

#include <QCoreApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QtTest/QTest>
#include <QWidget>

#include <chrono>
#include <cstdint>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

std::optional<genplusgx::EmulationEvent> waitForOperation(
  genplusgx::EmulationWorker& worker,
  std::uint64_t operationId)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = worker.waitForEvent(50ms);
    if (event && event->operationId == operationId) {
      return event;
    }
  }
  return std::nullopt;
}

bool submitAndSucceed(
  genplusgx::EmulationWorker& worker,
  genplusgx::EmulationCommand command,
  genplusgx::EmulationEvent* completed = nullptr)
{
  const auto operationId = command.operationId;
  if (!worker.submit(std::move(command))) {
    return false;
  }
  auto event = waitForOperation(worker, operationId);
  if (!event || !event->succeeded()) {
    return false;
  }
  if (completed != nullptr) {
    *completed = std::move(*event);
  }
  return true;
}

} // namespace

class KeyboardInputTest final : public QObject {
  Q_OBJECT

private slots:
  void defaultBindings();
  void logicalSnapshots();
  void eventFiltering();
  void workerPipeline();
};

void KeyboardInputTest::defaultBindings()
{
  const auto bindings = genplusgx::input::defaultGenesisKeyboardBindings();
  QCOMPARE(bindings.size(), 12U);
  std::set<int> keys;
  std::set<genplusgx::InputButton> buttons;
  for (const auto& binding : bindings) {
    keys.insert(binding.key);
    buttons.insert(binding.button);
  }
  QCOMPARE(keys.size(), bindings.size());
  QCOMPARE(buttons.size(), bindings.size());

  genplusgx::input::KeyboardInput input;
  QCOMPARE(input.keyForButton(genplusgx::InputButton::up), Qt::Key_Up);
  QCOMPARE(input.keyForButton(genplusgx::InputButton::down), Qt::Key_Down);
  QCOMPARE(input.keyForButton(genplusgx::InputButton::left), Qt::Key_Left);
  QCOMPARE(input.keyForButton(genplusgx::InputButton::right), Qt::Key_Right);
  QCOMPARE(input.keyForButton(genplusgx::InputButton::a), Qt::Key_Z);
  QCOMPARE(input.keyForButton(genplusgx::InputButton::b), Qt::Key_X);
  QCOMPARE(input.keyForButton(genplusgx::InputButton::c), Qt::Key_C);
  QCOMPARE(input.keyForButton(genplusgx::InputButton::x), Qt::Key_A);
  QCOMPARE(input.keyForButton(genplusgx::InputButton::y), Qt::Key_S);
  QCOMPARE(input.keyForButton(genplusgx::InputButton::z), Qt::Key_D);
  QCOMPARE(input.keyForButton(genplusgx::InputButton::start), Qt::Key_Return);
  QCOMPARE(input.keyForButton(genplusgx::InputButton::mode), Qt::Key_Shift);
}

void KeyboardInputTest::logicalSnapshots()
{
  genplusgx::input::KeyboardInput input;
  std::vector<genplusgx::InputSnapshot> published;
  input.setSnapshotSink(
    [&published](const genplusgx::InputSnapshot& snapshot) {
      published.push_back(snapshot);
    });

  QCOMPARE(input.snapshot().sequence, 1U);
  QVERIFY(input.snapshot().players[0].connected);
  QVERIFY(!input.pressKey(Qt::Key_F1));
  QVERIFY(input.pressKey(Qt::Key_Z));
  QVERIFY(genplusgx::hasButton(
    input.snapshot().players[0].buttons, genplusgx::InputButton::a));
  QCOMPARE(published.size(), 1U);
  const auto afterA = input.snapshot().sequence;
  QVERIFY(input.pressKey(Qt::Key_Z, true));
  QVERIFY(input.releaseKey(Qt::Key_Z, true));
  QCOMPARE(input.snapshot().sequence, afterA);

  QVERIFY(input.pressKey(Qt::Key_Left));
  QVERIFY(input.pressKey(Qt::Key_Right));
  QVERIFY(!genplusgx::hasButton(
    input.snapshot().players[0].buttons, genplusgx::InputButton::left));
  QVERIFY(genplusgx::hasButton(
    input.snapshot().players[0].buttons, genplusgx::InputButton::right));
  QVERIFY(input.releaseKey(Qt::Key_Right));
  QVERIFY(genplusgx::hasButton(
    input.snapshot().players[0].buttons, genplusgx::InputButton::left));

  QVERIFY(input.pressKey(Qt::Key_Up));
  QVERIFY(input.pressKey(Qt::Key_Down));
  QVERIFY(!genplusgx::hasButton(
    input.snapshot().players[0].buttons, genplusgx::InputButton::up));
  QVERIFY(genplusgx::hasButton(
    input.snapshot().players[0].buttons, genplusgx::InputButton::down));

  input.releaseAll();
  QCOMPARE(input.snapshot().players[0].buttons, 0U);
  const std::vector<int> sixButtonKeys{
    Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_A, Qt::Key_S, Qt::Key_D,
    Qt::Key_Return, Qt::Key_Shift};
  for (const auto key : sixButtonKeys) {
    QVERIFY(input.pressKey(key));
  }
  const auto buttonsNow = input.snapshot().players[0].buttons;
  QVERIFY(genplusgx::hasButton(buttonsNow, genplusgx::InputButton::a));
  QVERIFY(genplusgx::hasButton(buttonsNow, genplusgx::InputButton::b));
  QVERIFY(genplusgx::hasButton(buttonsNow, genplusgx::InputButton::c));
  QVERIFY(genplusgx::hasButton(buttonsNow, genplusgx::InputButton::x));
  QVERIFY(genplusgx::hasButton(buttonsNow, genplusgx::InputButton::y));
  QVERIFY(genplusgx::hasButton(buttonsNow, genplusgx::InputButton::z));
  QVERIFY(genplusgx::hasButton(buttonsNow, genplusgx::InputButton::start));
  QVERIFY(genplusgx::hasButton(buttonsNow, genplusgx::InputButton::mode));
}

void KeyboardInputTest::eventFiltering()
{
  QWidget target;
  genplusgx::input::KeyboardInput input;
  std::size_t publications = 0U;
  input.setSnapshotSink(
    [&publications](const genplusgx::InputSnapshot&) { ++publications; });
  input.attach(target);

  QKeyEvent shortcutPress{
    QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier};
  QCoreApplication::sendEvent(&target, &shortcutPress);
  QCOMPARE(publications, 0U);
  QCOMPARE(input.snapshot().players[0].buttons, 0U);

  QKeyEvent gameplayPress{QEvent::KeyPress, Qt::Key_X, Qt::NoModifier};
  QCoreApplication::sendEvent(&target, &gameplayPress);
  QCOMPARE(publications, 1U);
  QVERIFY(genplusgx::hasButton(
    input.snapshot().players[0].buttons, genplusgx::InputButton::b));

  QFocusEvent focusOut{QEvent::FocusOut};
  QCoreApplication::sendEvent(&target, &focusOut);
  QCOMPARE(publications, 2U);
  QCOMPARE(input.snapshot().players[0].buttons, 0U);
}

void KeyboardInputTest::workerPipeline()
{
  const genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".bin"};
  genplusgx::EmulationWorker worker;
  QVERIFY(worker.start());
  QVERIFY(worker.waitForEvent(2s).has_value());
  QVERIFY(submitAndSucceed(
    worker, genplusgx::EmulationCommand::load(1U, fixture.path())));

  genplusgx::input::KeyboardInput input;
  std::uint64_t operationId = 10U;
  std::uint64_t lastInputOperation = 0U;
  bool submissionFailed = false;
  input.setSnapshotSink(
    [&lastInputOperation, &operationId, &submissionFailed, &worker](
      const genplusgx::InputSnapshot& snapshot) {
      lastInputOperation = ++operationId;
      if (!worker.submit(genplusgx::EmulationCommand::updateInput(
            lastInputOperation, snapshot))) {
        submissionFailed = true;
      }
    });

  QVERIFY(input.pressKey(Qt::Key_X));
  QVERIFY(input.pressKey(Qt::Key_Right));
  QVERIFY(!submissionFailed);
  const auto pressedSequence = input.snapshot().sequence;
  const auto pressedOperation = lastInputOperation;
  const auto pressedUpdate = waitForOperation(worker, pressedOperation);
  QVERIFY(pressedUpdate && pressedUpdate->succeeded());
  genplusgx::EmulationEvent advanced;
  QVERIFY(submitAndSucceed(worker,
    genplusgx::EmulationCommand::simple(
      genplusgx::EmulationCommandType::frameAdvance, 100U), &advanced));
  QCOMPARE(advanced.appliedInputSequence, pressedSequence);
  QVERIFY(genplusgx::hasButton(
    input.snapshot().players[0].buttons, genplusgx::InputButton::b));
  QVERIFY(genplusgx::hasButton(
    input.snapshot().players[0].buttons, genplusgx::InputButton::right));

  input.releaseAll();
  const auto releasedSequence = input.snapshot().sequence;
  const auto releasedUpdate = waitForOperation(worker, lastInputOperation);
  QVERIFY(releasedUpdate && releasedUpdate->succeeded());
  QVERIFY(submitAndSucceed(worker,
    genplusgx::EmulationCommand::simple(
      genplusgx::EmulationCommandType::frameAdvance, 101U), &advanced));
  QCOMPARE(advanced.appliedInputSequence, releasedSequence);
  QCOMPARE(input.snapshot().players[0].buttons, 0U);
  QVERIFY(worker.stop());
}

QTEST_MAIN(KeyboardInputTest)

#include "keyboard_input_test.moc"
