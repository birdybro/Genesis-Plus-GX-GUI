#include "genplusgx/ui/debug_tools_window.h"
#include "genplusgx/ui/main_window.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>

#include <memory>
#include <optional>
#include <vector>

namespace {

class DebugToolsWindowTest final : public QObject {
  Q_OBJECT

private slots:
  void developerMenuIsHiddenUntilExplicitlyEnabled();
  void workspacePresentsEveryLiveInspectionSurface();
  void pausedEditsControlsAndStateActionsUseTypedCallbacks();
};

std::shared_ptr<genplusgx::CoreDebugSnapshot> sampleSnapshot()
{
  auto snapshot = std::make_shared<genplusgx::CoreDebugSnapshot>();
  snapshot->frameNumber = 42U;
  snapshot->hardware = 0x80U;
  snapshot->romSize = 64U * 1024U;
  snapshot->m68kActive = true;
  snapshot->m68k.data[0] = 0x12345678U;
  snapshot->m68k.programCounter = 0x00000200U;
  snapshot->z80.af = 0xABCDU;
  snapshot->z80.programCounter = 0x1357U;
  snapshot->vdp.registers[1] = 0x74U;
  snapshot->vdp.registers[2] = 0x30U;
  snapshot->vdp.registers[4] = 0x07U;
  snapshot->vdp.registers[12] = 0x81U;
  snapshot->vdp.cram[0] = 0x01FFU;
  snapshot->vdp.cram[1] = 0x0007U;
  snapshot->vdp.vram[0] = 0x12U;
  snapshot->vdp.vram[1] = 0x34U;
  snapshot->vdp.spriteTable[0] = 0x01U;
  snapshot->vdp.spriteTable[1] = 0x00U;
  snapshot->sound.fmRegisters[0][0x22] = 0x08U;
  snapshot->sound.psgRegisters[0] = 0x0FE;
  snapshot->input.buttons[0] = 0x0040U;
  snapshot->input.analog[0] = {123, -456};
  snapshot->m68kRam[0] = 0x13U;
  snapshot->m68kRam[1] = 0x57U;
  return snapshot;
}

void DebugToolsWindowTest::developerMenuIsHiddenUntilExplicitlyEnabled()
{
  genplusgx::ui::MainWindow window;
  auto* menu = window.findChild<QMenu*>(QStringLiteral("developerToolsMenu"));
  auto* action = window.findChild<QAction*>(QStringLiteral("debugToolsAction"));
  QVERIFY(menu != nullptr);
  QVERIFY(action != nullptr);
  QVERIFY(!menu->menuAction()->isVisible());
  action->trigger();
  QVERIFY(window.findChild<genplusgx::ui::DebugToolsWindow*>(
    QStringLiteral("debugToolsWindow")) == nullptr);

  window.setAppearanceSettings({
    .theme = genplusgx::settings::ThemeMode::system,
    .developerToolsEnabled = true,
  });
  QVERIFY(menu->menuAction()->isVisible());
  action->trigger();
  QApplication::processEvents();
  auto* debugger = window.findChild<genplusgx::ui::DebugToolsWindow*>(
    QStringLiteral("debugToolsWindow"));
  QVERIFY(debugger != nullptr);

  window.setAppearanceSettings({
    .theme = genplusgx::settings::ThemeMode::system,
    .developerToolsEnabled = false,
  });
  QApplication::processEvents();
  QVERIFY(!menu->menuAction()->isVisible());
  QVERIFY(window.findChild<genplusgx::ui::DebugToolsWindow*>(
    QStringLiteral("debugToolsWindow")) == nullptr);
}

void DebugToolsWindowTest::workspacePresentsEveryLiveInspectionSurface()
{
  genplusgx::ui::DebugToolsWindow window;
  QVERIFY(!window.findChild<QAction*>(
    QStringLiteral("debugHardResetAction"))->isEnabled());
  QVERIFY(!window.findChild<QWidget*>(
    QStringLiteral("debugSaveStatesPage"))->isEnabled());
  std::vector<genplusgx::CoreDebugRequest> requests;
  window.setRequestSink([&requests](genplusgx::CoreDebugRequest request) {
    requests.push_back(std::move(request));
    return true;
  });
  window.setGameLoaded(true);
  QVERIFY(window.findChild<QAction*>(
    QStringLiteral("debugHardResetAction"))->isEnabled());
  QVERIFY(window.findChild<QWidget*>(
    QStringLiteral("debugSaveStatesPage"))->isEnabled());
  QCOMPARE(requests.size(), 1U);
  QCOMPARE(requests.back().type,
    genplusgx::CoreDebugRequestType::captureSnapshot);

  genplusgx::CoreDebugResponse capture;
  capture.type = genplusgx::CoreDebugRequestType::captureSnapshot;
  capture.snapshot = sampleSnapshot();
  window.presentResponse(std::move(capture));
  QCOMPARE(window.snapshot()->frameNumber, 42U);
  QCOMPARE(requests.size(), 2U);
  QCOMPARE(requests.back().type, genplusgx::CoreDebugRequestType::readMemory);
  QVERIFY(requests.back().size <=
    genplusgx::CoreAdapter::maximumDebugTransferBytes);

  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("debugToolsTabs"));
  QVERIFY(tabs != nullptr);
  QCOMPARE(tabs->count(), 7);
  QCOMPARE(tabs->tabText(0), QStringLiteral("CPU"));
  QCOMPARE(tabs->tabText(1), QStringLiteral("Memory"));
  QCOMPARE(tabs->tabText(2), QStringLiteral("VDP"));
  QCOMPARE(tabs->tabText(3), QStringLiteral("Sound"));
  QCOMPARE(tabs->tabText(4), QStringLiteral("Input"));
  QCOMPARE(tabs->tabText(5), QStringLiteral("Analysis"));
  QCOMPARE(tabs->tabText(6), QStringLiteral("States"));

  auto* m68k = window.findChild<QTableWidget*>(
    QStringLiteral("debugM68kRegisterTable"));
  auto* z80 = window.findChild<QTableWidget*>(
    QStringLiteral("debugZ80RegisterTable"));
  auto* vdp = window.findChild<QTableWidget*>(
    QStringLiteral("debugVdpRegisterTable"));
  auto* palette = window.findChild<QTableWidget*>(
    QStringLiteral("debugPaletteTable"));
  auto* sprites = window.findChild<QTableWidget*>(
    QStringLiteral("debugSpriteTable"));
  auto* scroll = window.findChild<QTableWidget*>(
    QStringLiteral("debugScrollTable"));
  auto* fm = window.findChild<QTableWidget*>(
    QStringLiteral("debugFmRegisterTable"));
  auto* psg = window.findChild<QTableWidget*>(
    QStringLiteral("debugPsgRegisterTable"));
  auto* input = window.findChild<QTableWidget*>(
    QStringLiteral("debugInputPage"));
  QVERIFY(m68k && z80 && vdp && palette && sprites && scroll && fm && psg && input);
  QCOMPARE(m68k->item(0, 1)->text(), QStringLiteral("0X12345678"));
  QCOMPARE(z80->item(0, 1)->text(), QStringLiteral("0XABCD"));
  QCOMPARE(vdp->item(1, 1)->text(), QStringLiteral("0X74"));
  QCOMPARE(fm->item(0x22, 1)->text(), QStringLiteral("0X08"));
  QCOMPARE(psg->item(0, 1)->text(), QStringLiteral("0X00FE"));
  QCOMPARE(input->item(0, 1)->text(), QStringLiteral("0X0040"));
  QVERIFY(palette->item(0, 0)->background().color().isValid());
  QVERIFY(window.findChild<QLabel*>(QStringLiteral("debugTileImage"))
    ->pixmap().isNull() == false);
  QVERIFY(window.findChild<QLabel*>(QStringLiteral("debugPlaneImage"))
    ->pixmap().isNull() == false);

  genplusgx::CoreDebugResponse memory;
  memory.type = genplusgx::CoreDebugRequestType::readMemory;
  memory.region = requests.back().region;
  memory.offset = requests.back().offset;
  memory.bytes = {0x13U, 0x57U, 0x41U, 0x42U};
  window.presentResponse(std::move(memory));
  const auto text = window.findChild<QPlainTextEdit*>(
    QStringLiteral("debugMemoryHexView"))->toPlainText();
  QVERIFY(text.contains(QStringLiteral("13 57 41 42")));
  QVERIFY(text.contains(QStringLiteral(".WAB")));

  auto* analysisTabs = window.findChild<QTabWidget*>(
    QStringLiteral("debugAnalysisTabs"));
  QVERIFY(analysisTabs != nullptr);
  QCOMPARE(analysisTabs->count(), 3);
  QCOMPARE(analysisTabs->tabText(0), QStringLiteral("RAM Search"));
  QCOMPARE(analysisTabs->tabText(1), QStringLiteral("RAM Watch"));
  QCOMPARE(analysisTabs->tabText(2), QStringLiteral("Breakpoints"));

  window.findChild<QLineEdit*>(QStringLiteral("debugRamSearchValueEdit"))
    ->setText(QStringLiteral("0x13"));
  window.findChild<QPushButton*>(
    QStringLiteral("debugRamSearchNewButton"))->click();
  auto* searchResults = window.findChild<QTableWidget*>(
    QStringLiteral("debugRamSearchResultsTable"));
  QCOMPARE(searchResults->rowCount(), 1'024);
  window.findChild<QPushButton*>(
    QStringLiteral("debugRamSearchFilterButton"))->click();
  QCOMPARE(searchResults->rowCount(), 1);
  QCOMPARE(searchResults->item(0, 0)->text(), QStringLiteral("0XFF0000"));
  QCOMPARE(searchResults->item(0, 1)->text(), QStringLiteral("0X13"));

  window.findChild<QPushButton*>(QStringLiteral("debugWatchAddButton"))->click();
  auto* watches = window.findChild<QTableWidget*>(QStringLiteral("debugWatchTable"));
  QCOMPARE(watches->rowCount(), 1);
  QCOMPARE(watches->item(0, 1)->text(), QStringLiteral("0XFF0000"));
  QCOMPARE(watches->item(0, 3)->text(), QStringLiteral("0X13"));

  window.findChild<QSpinBox*>(QStringLiteral("debugBreakpointAddressSpin"))
    ->setValue(0x200);
  window.findChild<QPushButton*>(
    QStringLiteral("debugBreakpointAddButton"))->click();
  QCOMPARE(requests.back().type,
    genplusgx::CoreDebugRequestType::setFrameBreakpoints);
  QCOMPARE(requests.back().breakpoints.size(), 1U);
  QCOMPARE(requests.back().breakpoints.front().address, 0x200U);
  auto* breakpointTable = window.findChild<QTableWidget*>(
    QStringLiteral("debugBreakpointTable"));
  QCOMPARE(breakpointTable->rowCount(), 1);
  QCOMPARE(breakpointTable->item(0, 0)->text(), QStringLiteral("68000"));

  genplusgx::CoreDebugResponse hit;
  hit.type = genplusgx::CoreDebugRequestType::setFrameBreakpoints;
  hit.breakpointHit = genplusgx::CoreDebugBreakpoint{
    genplusgx::CoreDebugCpu::m68k, 0x200U};
  window.presentResponse(std::move(hit));
  QCOMPARE(analysisTabs->currentIndex(), 2);
  QVERIFY(window.findChild<QAction*>(
    QStringLiteral("debugResumeAction"))->isEnabled());
}

void DebugToolsWindowTest::pausedEditsControlsAndStateActionsUseTypedCallbacks()
{
  genplusgx::ui::DebugToolsWindow window;
  std::vector<genplusgx::CoreDebugRequest> requests;
  std::optional<genplusgx::ui::DebugControlOperation> control;
  std::optional<std::pair<genplusgx::ui::DebugStateOperation, std::uint32_t>> state;
  window.setRequestSink([&requests](genplusgx::CoreDebugRequest request) {
    requests.push_back(std::move(request));
    return true;
  });
  window.setControlSink([&control](auto operation) { control = operation; });
  window.setStateSink([&state](auto operation, auto slot) {
    state = std::pair{operation, slot};
  });
  window.setGameLoaded(true);
  genplusgx::CoreDebugResponse capture;
  capture.type = genplusgx::CoreDebugRequestType::captureSnapshot;
  capture.snapshot = sampleSnapshot();
  window.presentResponse(std::move(capture));
  window.setPaused(true);

  auto* write = window.findChild<QPushButton*>(
    QStringLiteral("debugMemoryWriteButton"));
  QVERIFY(write->isEnabled());
  window.findChild<QLineEdit*>(QStringLiteral("debugMemoryWriteBytesEdit"))
    ->setText(QStringLiteral("12 34 ab cd"));
  QTest::mouseClick(write, Qt::LeftButton);
  QCOMPARE(requests.back().type, genplusgx::CoreDebugRequestType::writeMemory);
  QCOMPARE(requests.back().bytes,
    std::vector<std::uint8_t>({0x12U, 0x34U, 0xABU, 0xCDU}));

  auto* m68k = window.findChild<QTableWidget*>(
    QStringLiteral("debugM68kRegisterTable"));
  m68k->item(0, 1)->setText(QStringLiteral("DEADBEEF"));
  QCOMPARE(requests.back().type,
    genplusgx::CoreDebugRequestType::setM68kRegisters);
  QCOMPARE(requests.back().m68k.data[0], 0xDEADBEEFU);

  auto* vdp = window.findChild<QTableWidget*>(
    QStringLiteral("debugVdpRegisterTable"));
  vdp->item(7, 1)->setText(QStringLiteral("2A"));
  QCOMPARE(requests.back().type,
    genplusgx::CoreDebugRequestType::setVdpRegister);
  QCOMPARE(requests.back().vdpRegister, 7U);
  QCOMPARE(requests.back().vdpValue, 0x2AU);

  window.findChild<QAction*>(QStringLiteral("debugFrameAdvanceAction"))->trigger();
  QCOMPARE(control, genplusgx::ui::DebugControlOperation::frameAdvance);
  window.findChild<QComboBox*>(QStringLiteral("debugStateSlotCombo"))
    ->setCurrentIndex(4);
  QTest::mouseClick(window.findChild<QPushButton*>(
    QStringLiteral("debugSaveStateButton")), Qt::LeftButton);
  QVERIFY(state.has_value());
  QCOMPARE(state->first, genplusgx::ui::DebugStateOperation::save);
  QCOMPARE(state->second, 4U);

  window.setPaused(false);
  QVERIFY(!write->isEnabled());
  QVERIFY((m68k->item(0, 1)->flags() & Qt::ItemIsEditable) == 0);
}

} // namespace

QTEST_MAIN(DebugToolsWindowTest)
#include "debug_tools_window_test.moc"
