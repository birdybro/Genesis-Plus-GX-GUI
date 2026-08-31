#include "genplusgx/emulation_worker.h"
#include "genplusgx/ui/debug_tools_window.h"
#include "synthetic_rom.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTest>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

namespace {

using namespace std::chrono_literals;

std::optional<genplusgx::EmulationEvent> waitForOperation(
  genplusgx::EmulationWorker& worker,
  std::uint64_t operationId)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = worker.waitForEvent(20ms);
    if (event && event->operationId == operationId) {
      return event;
    }
  }
  return std::nullopt;
}

bool routeUntil(
  genplusgx::EmulationWorker& worker,
  genplusgx::ui::DebugToolsWindow& window,
  const std::function<bool(const genplusgx::EmulationEvent&)>& predicate)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    QApplication::processEvents();
    auto event = worker.waitForEvent(20ms);
    if (!event) {
      continue;
    }
    if (event->workerState == genplusgx::EmulationWorkerState::paused ||
        event->workerState == genplusgx::EmulationWorkerState::running) {
      window.setPaused(
        event->workerState == genplusgx::EmulationWorkerState::paused);
    }
    if (event->type == genplusgx::EmulationEventType::debugResponse ||
        event->type == genplusgx::EmulationEventType::debugBreakpointHit) {
      window.presentResponse(std::move(event->debug));
    }
    if (predicate(*event)) {
      QApplication::processEvents();
      return true;
    }
  }
  return false;
}

class DebugToolsLiveTest final : public QObject {
  Q_OBJECT

private slots:
  void generatedRomDrivesLiveWorkspaceAndBreakpoint();
  void generatedZ80RomDrivesLiveBreakpoint();
};

void DebugToolsLiveTest::generatedRomDrivesLiveWorkspaceAndBreakpoint()
{
  const genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  genplusgx::EmulationWorker worker;
  QVERIFY(worker.start());
  auto started = worker.waitForEvent(3s);
  QVERIFY(started.has_value());
  QCOMPARE(started->type, genplusgx::EmulationEventType::workerStarted);
  QVERIFY(worker.submit(genplusgx::EmulationCommand::load(1U, fixture.path())));
  auto loaded = waitForOperation(worker, 1U);
  QVERIFY(loaded.has_value());
  QVERIFY(loaded->succeeded());

  genplusgx::ui::DebugToolsWindow window;
  std::uint64_t debugOperation = 100U;
  std::uint64_t latestDebugOperation = 0U;
  std::uint64_t controlOperation = 200U;
  window.setRequestSink(
    [&worker, &debugOperation, &latestDebugOperation](
      genplusgx::CoreDebugRequest request) {
      latestDebugOperation = ++debugOperation;
      return worker.submit(genplusgx::EmulationCommand::debug(
        latestDebugOperation, std::move(request))).ok();
    });
  window.setControlSink(
    [&worker, &controlOperation](genplusgx::ui::DebugControlOperation operation) {
      auto command = genplusgx::EmulationCommandType::pause;
      switch (operation) {
        case genplusgx::ui::DebugControlOperation::pause:
          command = genplusgx::EmulationCommandType::pause;
          break;
        case genplusgx::ui::DebugControlOperation::resume:
          command = genplusgx::EmulationCommandType::resume;
          break;
        case genplusgx::ui::DebugControlOperation::frameAdvance:
          command = genplusgx::EmulationCommandType::frameAdvance;
          break;
        case genplusgx::ui::DebugControlOperation::hardReset:
          command = genplusgx::EmulationCommandType::hardReset;
          break;
        case genplusgx::ui::DebugControlOperation::softReset:
          command = genplusgx::EmulationCommandType::softReset;
          break;
      }
      static_cast<void>(worker.submit(genplusgx::EmulationCommand::simple(
        command, ++controlOperation)));
    });
  window.setGameLoaded(true);
  window.setPaused(true);
  window.show();

  QVERIFY(routeUntil(worker, window, [&window](const auto&) {
    return window.snapshot() && window.snapshot()->m68kRam[0] == 0U;
  }));
  QVERIFY(routeUntil(worker, window, [&window](const auto&) {
    return !window.findChild<QPlainTextEdit*>(
      QStringLiteral("debugMemoryHexView"))->toPlainText().isEmpty();
  }));
  QCOMPARE(window.findChild<QTableWidget*>(
    QStringLiteral("debugM68kRegisterTable"))->item(16, 1)->text(),
    QStringLiteral("0X00000200"));

  auto* address = window.findChild<QSpinBox*>(
    QStringLiteral("debugBreakpointAddressSpin"));
  auto* add = window.findChild<QPushButton*>(
    QStringLiteral("debugBreakpointAddButton"));
  for (const int pc : {0x250, 0x256, 0x25A}) {
    address->setValue(pc);
    add->click();
    const auto operation = latestDebugOperation;
    QVERIFY(routeUntil(worker, window, [operation](const auto& event) {
      return event.operationId == operation && event.succeeded();
    }));
  }
  QCOMPARE(window.findChild<QTableWidget*>(
    QStringLiteral("debugBreakpointTable"))->rowCount(), 3);

  window.findChild<QAction*>(QStringLiteral("debugResumeAction"))->trigger();
  QVERIFY(routeUntil(worker, window, [](const auto& event) {
    return event.type == genplusgx::EmulationEventType::debugBreakpointHit;
  }));
  QCOMPARE(worker.state(), genplusgx::EmulationWorkerState::paused);
  QVERIFY(window.findChild<QAction*>(
    QStringLiteral("debugResumeAction"))->isEnabled());
  QVERIFY(routeUntil(worker, window, [&window](const auto&) {
    return window.snapshot() && window.snapshot()->frameNumber > 0U;
  }));
  QVERIFY(window.snapshot()->m68kRam[0] == 0x13U);

  window.close();
  QVERIFY(worker.stop());
}

void DebugToolsLiveTest::generatedZ80RomDrivesLiveBreakpoint()
{
  constexpr std::uint8_t marker = 0xA7U;
  const genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeZ80RamMarkerRom(marker), ".sms"};
  genplusgx::EmulationWorker worker;
  QVERIFY(worker.start());
  auto started = worker.waitForEvent(3s);
  QVERIFY(started.has_value());
  QCOMPARE(started->type, genplusgx::EmulationEventType::workerStarted);
  QVERIFY(worker.submit(genplusgx::EmulationCommand::load(1U, fixture.path())));
  auto loaded = waitForOperation(worker, 1U);
  QVERIFY(loaded.has_value());
  QVERIFY(loaded->succeeded());

  genplusgx::ui::DebugToolsWindow window;
  std::uint64_t debugOperation = 300U;
  std::uint64_t latestDebugOperation = 0U;
  std::uint64_t controlOperation = 400U;
  window.setRequestSink(
    [&worker, &debugOperation, &latestDebugOperation](
      genplusgx::CoreDebugRequest request) {
      latestDebugOperation = ++debugOperation;
      return worker.submit(genplusgx::EmulationCommand::debug(
        latestDebugOperation, std::move(request))).ok();
    });
  window.setControlSink(
    [&worker, &controlOperation](genplusgx::ui::DebugControlOperation operation) {
      if (operation == genplusgx::ui::DebugControlOperation::resume) {
        static_cast<void>(worker.submit(genplusgx::EmulationCommand::simple(
          genplusgx::EmulationCommandType::resume, ++controlOperation)));
      }
    });
  window.setGameLoaded(true);
  window.setPaused(true);
  window.show();

  QVERIFY(routeUntil(worker, window, [&window](const auto&) {
    return window.snapshot() && !window.snapshot()->m68kActive;
  }));
  QCOMPARE(window.findChild<QComboBox*>(
    QStringLiteral("debugMemoryRegionCombo"))->currentData().toInt(),
    static_cast<int>(genplusgx::CoreDebugMemoryRegion::z80Ram));
  auto* cpu = window.findChild<QComboBox*>(
    QStringLiteral("debugBreakpointCpuCombo"));
  QCOMPARE(cpu->currentData().toInt(),
    static_cast<int>(genplusgx::CoreDebugCpu::z80));
  cpu->setCurrentIndex(cpu->findData(
    static_cast<int>(genplusgx::CoreDebugCpu::z80)));
  auto* address = window.findChild<QSpinBox*>(
    QStringLiteral("debugBreakpointAddressSpin"));
  address->setValue(0x0009);
  window.findChild<QPushButton*>(
    QStringLiteral("debugBreakpointAddButton"))->click();
  const auto operation = latestDebugOperation;
  QVERIFY(routeUntil(worker, window, [operation](const auto& event) {
    return event.operationId == operation && event.succeeded();
  }));

  window.findChild<QAction*>(QStringLiteral("debugResumeAction"))->trigger();
  QVERIFY(routeUntil(worker, window, [](const auto& event) {
    return event.type == genplusgx::EmulationEventType::debugBreakpointHit &&
      event.debug.breakpointHit &&
      event.debug.breakpointHit->cpu == genplusgx::CoreDebugCpu::z80 &&
      event.debug.breakpointHit->address == 0x0009U;
  }));
  QCOMPARE(worker.state(), genplusgx::EmulationWorkerState::paused);
  QVERIFY(routeUntil(worker, window, [&window](const auto&) {
    return window.snapshot() && window.snapshot()->frameNumber > 0U;
  }));
  QCOMPARE(window.snapshot()->z80Ram[0], marker);
  QVERIFY(routeUntil(worker, window, [&window](const auto&) {
    return window.findChild<QPlainTextEdit*>(
      QStringLiteral("debugMemoryHexView"))->toPlainText().contains(
        QStringLiteral("A7"));
  }));

  window.close();
  QVERIFY(worker.stop());
}

} // namespace

QTEST_MAIN(DebugToolsLiveTest)
#include "debug_tools_live_test.moc"
