#include "genplusgx/ui/debug_tools_window.h"
#include "genplusgx/core_adapter.h"

#include <QAction>
#include <QColor>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPixmap>
#include <QRegularExpression>
#include <QScrollArea>
#include <QShowEvent>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace genplusgx::ui {
namespace {

QTableWidget* makeTable(
  QWidget* parent,
  const char* objectName,
  int rows,
  const QStringList& headers)
{
  auto* table = new QTableWidget(rows, static_cast<int>(headers.size()), parent);
  table->setObjectName(QString::fromLatin1(objectName));
  table->setHorizontalHeaderLabels(headers);
  table->verticalHeader()->hide();
  table->setAlternatingRowColors(true);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->horizontalHeader()->setStretchLastSection(true);
  return table;
}

QTableWidgetItem* itemAt(QTableWidget& table, int row, int column)
{
  if (auto* item = table.item(row, column)) {
    return item;
  }
  auto* item = new QTableWidgetItem;
  table.setItem(row, column, item);
  return item;
}

QString hexadecimal(std::uint64_t value, int digits)
{
  return QStringLiteral("0x%1").arg(
    static_cast<qulonglong>(value), digits, 16, QLatin1Char('0')).toUpper();
}

bool parseHex(const QString& text, std::uint64_t maximum, std::uint64_t& value)
{
  auto normalized = text.trimmed();
  if (normalized.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
    normalized.remove(0, 2);
  }
  bool ok = false;
  const auto parsed = normalized.toULongLong(&ok, 16);
  if (!ok || parsed > maximum) {
    return false;
  }
  value = parsed;
  return true;
}

void setRegisterRow(
  QTableWidget& table,
  int row,
  const QString& name,
  std::uint64_t value,
  int digits,
  bool editable)
{
  auto* nameItem = itemAt(table, row, 0);
  nameItem->setText(name);
  nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
  auto* valueItem = itemAt(table, row, 1);
  valueItem->setText(hexadecimal(value, digits));
  valueItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  valueItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
    (editable ? Qt::ItemIsEditable : Qt::NoItemFlags));
}

template<std::size_t Size>
std::uint16_t vramWord(
  const std::array<std::uint8_t, Size>& memory,
  std::uint32_t address)
{
  static_assert(Size > 1U);
  const auto first = static_cast<std::size_t>(address) % Size;
  const auto second = static_cast<std::size_t>(address + 1U) % Size;
  return static_cast<std::uint16_t>(
    (static_cast<std::uint16_t>(memory[first]) << 8U) | memory[second]);
}

std::uint32_t tilePixel(
  const CoreDebugVdpState& vdp,
  std::uint16_t tile,
  int x,
  int y,
  std::uint8_t palette,
  bool horizontalFlip,
  bool verticalFlip)
{
  const int sourceX = horizontalFlip ? 7 - x : x;
  const int sourceY = verticalFlip ? 7 - y : y;
  const auto address = (static_cast<std::uint32_t>(tile & 0x07FFU) * 32U) +
    static_cast<std::uint32_t>(sourceY * 4 + sourceX / 2);
  const auto packed = vdp.vram[address & 0xFFFFU];
  const auto colorIndex = static_cast<std::uint8_t>(
    sourceX % 2 == 0 ? packed >> 4U : packed & 0x0FU);
  const auto cramIndex = static_cast<std::size_t>(palette & 0x03U) * 16U +
    colorIndex;
  return coreDebugCramColor(vdp.cram[cramIndex]);
}

QImage makeTileImage(const CoreDebugVdpState& vdp)
{
  constexpr int columns = 16;
  constexpr int rows = 16;
  QImage image(columns * 8, rows * 8, QImage::Format_ARGB32);
  for (int tile = 0; tile < columns * rows; ++tile) {
    for (int y = 0; y < 8; ++y) {
      for (int x = 0; x < 8; ++x) {
        image.setPixel((tile % columns) * 8 + x, (tile / columns) * 8 + y,
          tilePixel(vdp, static_cast<std::uint16_t>(tile), x, y, 0U,
            false, false));
      }
    }
  }
  return image;
}

QImage makePlaneImage(const CoreDebugVdpState& vdp, int plane)
{
  const std::array dimensions{32, 64, 32, 128};
  const auto widthCells = std::min(dimensions[vdp.registers[16] & 0x03U], 64);
  const auto heightCells = std::min(
    dimensions[(vdp.registers[16] >> 4U) & 0x03U], 64);
  std::uint32_t base = 0U;
  if (plane == 0) {
    base = static_cast<std::uint32_t>(vdp.registers[2] & 0x38U) << 10U;
  } else if (plane == 1) {
    base = static_cast<std::uint32_t>(vdp.registers[4] & 0x07U) << 13U;
  } else {
    base = static_cast<std::uint32_t>(vdp.registers[3] & 0x3EU) << 10U;
  }
  QImage image(widthCells * 8, heightCells * 8, QImage::Format_ARGB32);
  for (int cellY = 0; cellY < heightCells; ++cellY) {
    for (int cellX = 0; cellX < widthCells; ++cellX) {
      const auto entryAddress = base +
        static_cast<std::uint32_t>((cellY * widthCells + cellX) * 2);
      const auto entry = vramWord(vdp.vram, entryAddress);
      const auto tile = static_cast<std::uint16_t>(entry & 0x07FFU);
      const auto palette = static_cast<std::uint8_t>((entry >> 13U) & 0x03U);
      const bool horizontalFlip = (entry & 0x0800U) != 0U;
      const bool verticalFlip = (entry & 0x1000U) != 0U;
      for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
          image.setPixel(cellX * 8 + x, cellY * 8 + y,
            tilePixel(vdp, tile, x, y, palette,
              horizontalFlip, verticalFlip));
        }
      }
    }
  }
  return image;
}

QString formatMemory(
  std::span<const std::uint8_t> bytes,
  std::uint32_t baseAddress)
{
  QString output;
  for (std::size_t line = 0U; line < bytes.size(); line += 16U) {
    output += hexadecimal(baseAddress + static_cast<std::uint32_t>(line), 6);
    output += QStringLiteral(": ");
    QString characters;
    for (std::size_t column = 0U; column < 16U; ++column) {
      if (line + column < bytes.size()) {
        const auto value = bytes[line + column];
        output += QStringLiteral("%1 ").arg(value, 2, 16, QLatin1Char('0'))
          .toUpper();
        characters += value >= 0x20U && value <= 0x7EU
          ? QChar::fromLatin1(static_cast<char>(value)) : QChar{'.'};
      } else {
        output += QStringLiteral("   ");
        characters += QChar{' '};
      }
    }
    output += QStringLiteral(" |%1|\n").arg(characters);
  }
  return output;
}

} // namespace

DebugToolsWindow::DebugToolsWindow(QWidget* parent)
    : QMainWindow(parent)
{
  setObjectName(QStringLiteral("debugToolsWindow"));
  setWindowTitle(tr("Genesis Plus GX Debug Tools"));
  setAttribute(Qt::WA_DeleteOnClose);
  resize(1'080, 760);

  buildToolbar();
  auto* tabs = new QTabWidget(this);
  tabs->setObjectName(QStringLiteral("debugToolsTabs"));
  setCentralWidget(tabs);
  buildCpuPage();
  buildMemoryPage();
  buildVdpPage();
  buildSoundPage();
  buildInputPage();
  buildStatePage();

  refreshTimer_ = new QTimer(this);
  refreshTimer_->setObjectName(QStringLiteral("debugRefreshTimer"));
  refreshTimer_->setInterval(250);
  connect(refreshTimer_, &QTimer::timeout, this,
    &DebugToolsWindow::requestRefresh);
  setStatus(tr("Load a game to inspect emulator state."));
  setPaused(false);
}

void DebugToolsWindow::buildToolbar()
{
  auto* toolbar = addToolBar(tr("Debug controls"));
  toolbar->setObjectName(QStringLiteral("debugControlToolBar"));
  pauseAction_ = toolbar->addAction(tr("Pause"));
  pauseAction_->setObjectName(QStringLiteral("debugPauseAction"));
  resumeAction_ = toolbar->addAction(tr("Resume"));
  resumeAction_->setObjectName(QStringLiteral("debugResumeAction"));
  frameAdvanceAction_ = toolbar->addAction(tr("Step Frame"));
  frameAdvanceAction_->setObjectName(QStringLiteral("debugFrameAdvanceAction"));
  hardResetAction_ = toolbar->addAction(tr("Hard Reset"));
  hardResetAction_->setObjectName(QStringLiteral("debugHardResetAction"));
  softResetAction_ = toolbar->addAction(tr("Soft Reset"));
  softResetAction_->setObjectName(QStringLiteral("debugSoftResetAction"));
  refreshAction_ = toolbar->addAction(tr("Refresh"));
  refreshAction_->setObjectName(QStringLiteral("debugRefreshAction"));
  const auto control = [this](DebugControlOperation operation) {
    if (controlSink_) {
      controlSink_(operation);
    }
  };
  connect(pauseAction_, &QAction::triggered, this,
    [control] { control(DebugControlOperation::pause); });
  connect(resumeAction_, &QAction::triggered, this,
    [control] { control(DebugControlOperation::resume); });
  connect(frameAdvanceAction_, &QAction::triggered, this,
    [control] { control(DebugControlOperation::frameAdvance); });
  connect(hardResetAction_, &QAction::triggered, this,
    [control] { control(DebugControlOperation::hardReset); });
  connect(softResetAction_, &QAction::triggered, this,
    [control] { control(DebugControlOperation::softReset); });
  connect(refreshAction_, &QAction::triggered, this,
    &DebugToolsWindow::requestRefresh);
}

void DebugToolsWindow::buildCpuPage()
{
  auto* page = new QWidget(this);
  page->setObjectName(QStringLiteral("debugCpuPage"));
  auto* layout = new QVBoxLayout(page);
  auto* explanation = new QLabel(tr(
    "Register values are sampled between frames. Pause emulation before editing "
    "a value; press Enter to apply a hexadecimal edit."), page);
  explanation->setWordWrap(true);
  layout->addWidget(explanation);
  auto* splitter = new QSplitter(Qt::Horizontal, page);
  m68kRegisters_ = makeTable(
    splitter, "debugM68kRegisterTable", 20, {tr("68K register"), tr("Value")});
  z80Registers_ = makeTable(
    splitter, "debugZ80RegisterTable", 19, {tr("Z80 register"), tr("Value")});
  splitter->addWidget(m68kRegisters_);
  splitter->addWidget(z80Registers_);
  layout->addWidget(splitter);
  connect(m68kRegisters_, &QTableWidget::cellChanged,
    this, &DebugToolsWindow::applyM68kEdit);
  connect(z80Registers_, &QTableWidget::cellChanged,
    this, &DebugToolsWindow::applyZ80Edit);
  qobject_cast<QTabWidget*>(centralWidget())->addTab(page, tr("CPU"));
}

void DebugToolsWindow::buildMemoryPage()
{
  auto* page = new QWidget(this);
  page->setObjectName(QStringLiteral("debugMemoryPage"));
  auto* layout = new QVBoxLayout(page);
  auto* controls = new QFormLayout;
  memoryRegion_ = new QComboBox(page);
  memoryRegion_->setObjectName(QStringLiteral("debugMemoryRegionCombo"));
  for (const auto& region : coreDebugMemoryRegions(0U)) {
    memoryRegion_->addItem(QString::fromUtf8(region.name.data(),
      static_cast<qsizetype>(region.name.size())),
      static_cast<int>(region.region));
  }
  memoryOffset_ = new QSpinBox(page);
  memoryOffset_->setObjectName(QStringLiteral("debugMemoryOffsetSpin"));
  memoryOffset_->setDisplayIntegerBase(16);
  memoryOffset_->setPrefix(QStringLiteral("0x"));
  memoryOffset_->setMaximum(0x7FFFFFFF);
  memoryLength_ = new QSpinBox(page);
  memoryLength_->setObjectName(QStringLiteral("debugMemoryLengthSpin"));
  memoryLength_->setRange(1, static_cast<int>(CoreAdapter::maximumDebugTransferBytes));
  memoryLength_->setValue(256);
  controls->addRow(tr("&Region:"), memoryRegion_);
  controls->addRow(tr("&Offset:"), memoryOffset_);
  controls->addRow(tr("&Bytes:"), memoryLength_);
  layout->addLayout(controls);
  memoryView_ = new QPlainTextEdit(page);
  memoryView_->setObjectName(QStringLiteral("debugMemoryHexView"));
  memoryView_->setReadOnly(true);
  memoryView_->setLineWrapMode(QPlainTextEdit::NoWrap);
  memoryView_->setAccessibleName(tr("Memory hexadecimal view"));
  layout->addWidget(memoryView_);
  auto* writeRow = new QHBoxLayout;
  memoryWriteBytes_ = new QLineEdit(page);
  memoryWriteBytes_->setObjectName(QStringLiteral("debugMemoryWriteBytesEdit"));
  memoryWriteBytes_->setPlaceholderText(tr("Hex bytes, for example: 12 34 AB CD"));
  memoryWriteButton_ = new QPushButton(tr("Write at offset"), page);
  memoryWriteButton_->setObjectName(QStringLiteral("debugMemoryWriteButton"));
  writeRow->addWidget(memoryWriteBytes_);
  writeRow->addWidget(memoryWriteButton_);
  layout->addLayout(writeRow);
  connect(memoryRegion_, &QComboBox::currentIndexChanged,
    this, [this] { updateMemoryRegion(); requestMemory(); });
  connect(memoryOffset_, &QSpinBox::editingFinished,
    this, &DebugToolsWindow::requestMemory);
  connect(memoryLength_, &QSpinBox::editingFinished,
    this, &DebugToolsWindow::requestMemory);
  connect(memoryWriteButton_, &QPushButton::clicked,
    this, &DebugToolsWindow::writeMemory);
  qobject_cast<QTabWidget*>(centralWidget())->addTab(page, tr("Memory"));
}

void DebugToolsWindow::buildVdpPage()
{
  auto* page = new QTabWidget(this);
  page->setObjectName(QStringLiteral("debugVdpPage"));

  vdpRegisters_ = makeTable(
    page, "debugVdpRegisterTable", 32, {tr("Register"), tr("Value")});
  connect(vdpRegisters_, &QTableWidget::cellChanged,
    this, &DebugToolsWindow::applyVdpEdit);
  page->addTab(vdpRegisters_, tr("Registers"));

  palette_ = makeTable(page, "debugPaletteTable", 8,
    {tr("0"), tr("1"), tr("2"), tr("3"), tr("4"), tr("5"), tr("6"), tr("7")});
  page->addTab(palette_, tr("Palette"));

  auto* tileScroll = new QScrollArea(page);
  tileScroll->setObjectName(QStringLiteral("debugTileScrollArea"));
  tiles_ = new QLabel(tileScroll);
  tiles_->setObjectName(QStringLiteral("debugTileImage"));
  tiles_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  tileScroll->setWidget(tiles_);
  tileScroll->setWidgetResizable(true);
  page->addTab(tileScroll, tr("Tiles"));

  sprites_ = makeTable(page, "debugSpriteTable", 80,
    {tr("#"), tr("X"), tr("Y"), tr("Size"), tr("Link"), tr("Tile"), tr("Palette"), tr("Flags")});
  page->addTab(sprites_, tr("Sprites"));

  auto* planePage = new QWidget(page);
  auto* planeLayout = new QVBoxLayout(planePage);
  planeSelector_ = new QComboBox(planePage);
  planeSelector_->setObjectName(QStringLiteral("debugPlaneSelector"));
  planeSelector_->addItems({tr("Plane A"), tr("Plane B"), tr("Window")});
  planeLayout->addWidget(planeSelector_);
  auto* planeScroll = new QScrollArea(planePage);
  planeScroll->setObjectName(QStringLiteral("debugPlaneScrollArea"));
  planeImage_ = new QLabel(planeScroll);
  planeImage_->setObjectName(QStringLiteral("debugPlaneImage"));
  planeImage_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  planeScroll->setWidget(planeImage_);
  planeScroll->setWidgetResizable(true);
  planeLayout->addWidget(planeScroll);
  connect(planeSelector_, &QComboBox::currentIndexChanged,
    this, [this] { updateVdpViews(); });
  page->addTab(planePage, tr("Planes"));

  scroll_ = makeTable(page, "debugScrollTable", 20,
    {tr("Index"), tr("H A"), tr("H B"), tr("V A"), tr("V B")});
  page->addTab(scroll_, tr("Scroll"));
  qobject_cast<QTabWidget*>(centralWidget())->addTab(page, tr("VDP"));
}

void DebugToolsWindow::buildSoundPage()
{
  auto* page = new QSplitter(Qt::Vertical, this);
  page->setObjectName(QStringLiteral("debugSoundPage"));
  fmRegisters_ = makeTable(page, "debugFmRegisterTable", 256,
    {tr("Address"), tr("YM bank 0"), tr("YM bank 1")});
  psgRegisters_ = makeTable(page, "debugPsgRegisterTable", 8,
    {tr("PSG register"), tr("Value")});
  page->addWidget(fmRegisters_);
  page->addWidget(psgRegisters_);
  qobject_cast<QTabWidget*>(centralWidget())->addTab(page, tr("Sound"));
}

void DebugToolsWindow::buildInputPage()
{
  inputState_ = makeTable(this, "debugInputStateTable", 8,
    {tr("Port"), tr("Buttons"), tr("Analog X"), tr("Analog Y")});
  inputState_->setObjectName(QStringLiteral("debugInputPage"));
  qobject_cast<QTabWidget*>(centralWidget())->addTab(inputState_, tr("Input"));
}

void DebugToolsWindow::buildStatePage()
{
  statePage_ = new QWidget(this);
  statePage_->setObjectName(QStringLiteral("debugSaveStatesPage"));
  auto* layout = new QVBoxLayout(statePage_);
  auto* description = new QLabel(tr(
    "Debug state controls use the normal validated per-game state manager. "
    "Raw core payloads are never loaded across games."), statePage_);
  description->setWordWrap(true);
  layout->addWidget(description);
  stateSlot_ = new QComboBox(statePage_);
  stateSlot_->setObjectName(QStringLiteral("debugStateSlotCombo"));
  for (int slot = 0; slot < 10; ++slot) {
    stateSlot_->addItem(tr("Slot %1").arg(slot), slot);
  }
  layout->addWidget(stateSlot_);
  const auto addButton = [this, layout](
                           const QString& text,
                           const char* name,
                           DebugStateOperation operation) {
    auto* button = new QPushButton(text, statePage_);
    button->setObjectName(QString::fromLatin1(name));
    connect(button, &QPushButton::clicked, this, [this, operation] {
      if (stateSink_) {
        stateSink_(operation,
          static_cast<std::uint32_t>(stateSlot_->currentData().toUInt()));
      }
    });
    layout->addWidget(button);
  };
  addButton(tr("Save"), "debugSaveStateButton", DebugStateOperation::save);
  addButton(tr("Load"), "debugLoadStateButton", DebugStateOperation::load);
  addButton(tr("Delete"), "debugDeleteStateButton", DebugStateOperation::remove);
  layout->addStretch();
  qobject_cast<QTabWidget*>(centralWidget())->addTab(statePage_, tr("States"));
}

void DebugToolsWindow::setRequestSink(RequestSink sink)
{
  requestSink_ = std::move(sink);
}

void DebugToolsWindow::setControlSink(ControlSink sink)
{
  controlSink_ = std::move(sink);
}

void DebugToolsWindow::setStateSink(StateSink sink)
{
  stateSink_ = std::move(sink);
}

void DebugToolsWindow::setGameLoaded(bool loaded)
{
  gameLoaded_ = loaded;
  if (!loaded) {
    snapshot_.reset();
    snapshotPending_ = false;
    memoryPending_ = false;
    memoryView_->clear();
    setStatus(tr("Load a game to inspect emulator state."));
  } else {
    setStatus(tr("Waiting for emulator debug state…"));
    requestRefresh();
  }
  setPaused(paused_);
}

void DebugToolsWindow::setPaused(bool paused)
{
  paused_ = paused;
  pauseAction_->setEnabled(gameLoaded_ && !paused);
  resumeAction_->setEnabled(gameLoaded_ && paused);
  frameAdvanceAction_->setEnabled(gameLoaded_ && paused);
  hardResetAction_->setEnabled(gameLoaded_);
  softResetAction_->setEnabled(gameLoaded_);
  refreshAction_->setEnabled(gameLoaded_);
  memoryWriteButton_->setEnabled(gameLoaded_ && paused);
  statePage_->setEnabled(gameLoaded_);
  if (snapshot_) {
    updateCpuViews();
    updateVdpViews();
  }
}

void DebugToolsWindow::requestRefresh()
{
  if (!gameLoaded_ || snapshotPending_) {
    return;
  }
  CoreDebugRequest request;
  request.type = CoreDebugRequestType::captureSnapshot;
  if (submit(std::move(request))) {
    snapshotPending_ = true;
  }
}

void DebugToolsWindow::presentResponse(CoreDebugResponse response)
{
  if (!gameLoaded_) {
    if (response.type == CoreDebugRequestType::captureSnapshot) {
      snapshotPending_ = false;
    } else if (response.type == CoreDebugRequestType::readMemory) {
      memoryPending_ = false;
    }
    return;
  }
  if (response.type == CoreDebugRequestType::captureSnapshot) {
    snapshotPending_ = false;
    if (response.snapshot) {
      snapshot_ = std::move(response.snapshot);
      updateAllViews();
      setStatus(tr("Frame %1 • %2 • edits %3")
        .arg(static_cast<qulonglong>(snapshot_->frameNumber))
        .arg(snapshot_->vdp.pal ? tr("PAL") : tr("NTSC"))
        .arg(paused_ ? tr("enabled") : tr("locked while running")));
      requestMemory();
    }
    return;
  }
  if (response.type == CoreDebugRequestType::readMemory) {
    memoryPending_ = false;
    updateMemoryView(response);
    return;
  }
  setStatus(tr("Debug edit applied."), 2'000);
  requestRefresh();
  requestMemory();
}

void DebugToolsWindow::showRequestError(const std::string& detail)
{
  snapshotPending_ = false;
  memoryPending_ = false;
  setStatus(tr("Debug request failed: %1").arg(QString::fromStdString(detail)),
    6'000);
}

std::shared_ptr<const CoreDebugSnapshot> DebugToolsWindow::snapshot() const
{
  return snapshot_;
}

bool DebugToolsWindow::submit(CoreDebugRequest request)
{
  if (!requestSink_ || !requestSink_(std::move(request))) {
    setStatus(tr("The debug request queue is unavailable."), 5'000);
    return false;
  }
  return true;
}

void DebugToolsWindow::showEvent(QShowEvent* event)
{
  QMainWindow::showEvent(event);
  refreshTimer_->start();
  requestRefresh();
}

void DebugToolsWindow::hideEvent(QHideEvent* event)
{
  refreshTimer_->stop();
  QMainWindow::hideEvent(event);
}

void DebugToolsWindow::updateAllViews()
{
  if (!snapshot_) {
    return;
  }
  updating_ = true;
  updateCpuViews();
  updateMemoryRegion();
  updateVdpViews();
  updateSoundViews();
  updateInputView();
  updating_ = false;
}

void DebugToolsWindow::updateCpuViews()
{
  if (!snapshot_) {
    return;
  }
  const bool previousUpdating = updating_;
  updating_ = true;
  for (int index = 0; index < 8; ++index) {
    setRegisterRow(*m68kRegisters_, index,
      tr("D%1").arg(index), snapshot_->m68k.data[static_cast<std::size_t>(index)],
      8, paused_ && snapshot_->m68kActive);
    setRegisterRow(*m68kRegisters_, index + 8,
      tr("A%1").arg(index), snapshot_->m68k.address[static_cast<std::size_t>(index)],
      8, paused_ && snapshot_->m68kActive);
  }
  setRegisterRow(*m68kRegisters_, 16, tr("PC"),
    snapshot_->m68k.programCounter, 8, paused_ && snapshot_->m68kActive);
  setRegisterRow(*m68kRegisters_, 17, tr("SR"),
    snapshot_->m68k.status, 4, paused_ && snapshot_->m68kActive);
  setRegisterRow(*m68kRegisters_, 18, tr("USP"),
    snapshot_->m68k.userStackPointer, 8, paused_ && snapshot_->m68kActive);
  setRegisterRow(*m68kRegisters_, 19, tr("ISP"),
    snapshot_->m68k.interruptStackPointer, 8, paused_ && snapshot_->m68kActive);
  const std::array z80Names{
    "AF", "BC", "DE", "HL", "AF'", "BC'", "DE'", "HL'", "IX", "IY",
    "SP", "PC", "I", "R", "IM", "IFF1", "IFF2", "HALT", "BANK"};
  const std::array<std::uint64_t, 19> z80Values{
    snapshot_->z80.af, snapshot_->z80.bc, snapshot_->z80.de, snapshot_->z80.hl,
    snapshot_->z80.afAlternate, snapshot_->z80.bcAlternate,
    snapshot_->z80.deAlternate, snapshot_->z80.hlAlternate,
    snapshot_->z80.ix, snapshot_->z80.iy, snapshot_->z80.stackPointer,
    snapshot_->z80.programCounter, snapshot_->z80.interruptVector,
    snapshot_->z80.refresh, snapshot_->z80.interruptMode,
    snapshot_->z80.interruptFlipFlop1 ? 1U : 0U,
    snapshot_->z80.interruptFlipFlop2 ? 1U : 0U,
    snapshot_->z80.halted ? 1U : 0U, snapshot_->z80.bank};
  for (int row = 0; row < static_cast<int>(z80Names.size()); ++row) {
    const int digits = row < 12 ? 4 : (row == 18 ? 6 : 2);
    setRegisterRow(*z80Registers_, row,
      QString::fromLatin1(z80Names[static_cast<std::size_t>(row)]),
      z80Values[static_cast<std::size_t>(row)], digits, paused_);
  }
  updating_ = previousUpdating;
}

void DebugToolsWindow::updateMemoryRegion()
{
  const auto romSize = snapshot_ ? snapshot_->romSize : 0U;
  const auto regions = coreDebugMemoryRegions(romSize);
  const auto selected = memoryRegion_->currentData().toInt();
  const auto found = std::ranges::find_if(regions, [selected](const auto& region) {
    return static_cast<int>(region.region) == selected;
  });
  const auto size = found == regions.end() ? 0U : found->size;
  const auto maximum = size == 0U ? 0U : size - 1U;
  memoryOffset_->setMaximum(static_cast<int>(std::min<std::uint32_t>(
    maximum, static_cast<std::uint32_t>(std::numeric_limits<int>::max()))));
}

void DebugToolsWindow::requestMemory()
{
  if (!snapshot_ || memoryPending_) {
    return;
  }
  CoreDebugRequest request;
  request.type = CoreDebugRequestType::readMemory;
  request.region = static_cast<CoreDebugMemoryRegion>(
    memoryRegion_->currentData().toInt());
  request.offset = static_cast<std::uint32_t>(memoryOffset_->value());
  const auto regions = coreDebugMemoryRegions(snapshot_->romSize);
  const auto selected = std::ranges::find_if(regions, [&request](const auto& region) {
    return region.region == request.region;
  });
  if (selected == regions.end() || request.offset >= selected->size) {
    memoryView_->setPlainText(tr("The selected region is empty."));
    return;
  }
  request.size = std::min<std::uint32_t>(
    static_cast<std::uint32_t>(memoryLength_->value()),
    selected->size - request.offset);
  if (submit(std::move(request))) {
    memoryPending_ = true;
  }
}

void DebugToolsWindow::updateMemoryView(const CoreDebugResponse& response)
{
  if (response.region != static_cast<CoreDebugMemoryRegion>(
        memoryRegion_->currentData().toInt()) ||
      response.offset != static_cast<std::uint32_t>(memoryOffset_->value())) {
    requestMemory();
    return;
  }
  const auto regions = coreDebugMemoryRegions(snapshot_ ? snapshot_->romSize : 0U);
  const auto selected = std::ranges::find_if(regions, [&response](const auto& region) {
    return region.region == response.region;
  });
  const auto base = selected == regions.end() ? 0U : selected->displayBase;
  memoryView_->setPlainText(formatMemory(response.bytes, base + response.offset));
}

void DebugToolsWindow::writeMemory()
{
  if (!paused_) {
    setStatus(tr("Pause emulation before editing memory."), 4'000);
    return;
  }
  const auto parts = memoryWriteBytes_->text().split(
    QRegularExpression{QStringLiteral("[,\\s]+")}, Qt::SkipEmptyParts);
  if (parts.isEmpty() ||
      parts.size() > static_cast<qsizetype>(CoreAdapter::maximumDebugTransferBytes)) {
    setStatus(tr("Enter between 1 and 4096 hexadecimal bytes."), 4'000);
    return;
  }
  std::vector<std::uint8_t> bytes;
  bytes.reserve(static_cast<std::size_t>(parts.size()));
  for (const auto& part : parts) {
    std::uint64_t value = 0U;
    if (!parseHex(part, 0xFFU, value)) {
      setStatus(tr("Every memory value must be one hexadecimal byte."), 4'000);
      return;
    }
    bytes.push_back(static_cast<std::uint8_t>(value));
  }
  CoreDebugRequest request;
  request.type = CoreDebugRequestType::writeMemory;
  request.region = static_cast<CoreDebugMemoryRegion>(
    memoryRegion_->currentData().toInt());
  request.offset = static_cast<std::uint32_t>(memoryOffset_->value());
  request.bytes = std::move(bytes);
  static_cast<void>(submit(std::move(request)));
}

void DebugToolsWindow::updateVdpViews()
{
  if (!snapshot_) {
    return;
  }
  const bool previousUpdating = updating_;
  updating_ = true;
  for (int row = 0; row < 32; ++row) {
    setRegisterRow(*vdpRegisters_, row, tr("R%1").arg(row),
      snapshot_->vdp.registers[static_cast<std::size_t>(row)], 2, paused_);
  }
  for (int color = 0; color < 64; ++color) {
    auto* item = itemAt(*palette_, color / 8, color % 8);
    item->setText(QString::number(color));
    item->setTextAlignment(Qt::AlignCenter);
    const auto rgba = coreDebugCramColor(
      snapshot_->vdp.cram[static_cast<std::size_t>(color)]);
    item->setBackground(QColor::fromRgba(rgba));
    item->setForeground(QColor::fromRgba(rgba).lightness() < 128
      ? QColor{Qt::white} : QColor{Qt::black});
    item->setToolTip(hexadecimal(
      snapshot_->vdp.cram[static_cast<std::size_t>(color)], 3));
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
  }
  tiles_->setPixmap(QPixmap::fromImage(makeTileImage(snapshot_->vdp).scaled(
    256, 256, Qt::KeepAspectRatio, Qt::FastTransformation)));
  for (int sprite = 0; sprite < 80; ++sprite) {
    const auto offset = static_cast<std::uint32_t>(sprite * 8);
    const auto y = vramWord(snapshot_->vdp.spriteTable, offset) & 0x03FFU;
    const auto sizeLink = vramWord(snapshot_->vdp.spriteTable, offset + 2U);
    const auto attributes = vramWord(snapshot_->vdp.spriteTable, offset + 4U);
    const auto x = vramWord(snapshot_->vdp.spriteTable, offset + 6U) & 0x03FFU;
    const std::array values{
      QString::number(sprite), QString::number(x), QString::number(y),
      tr("%1×%2").arg(((sizeLink >> 10U) & 0x03U) + 1U)
        .arg(((sizeLink >> 8U) & 0x03U) + 1U),
      QString::number(sizeLink & 0x7FU),
      QString::number(attributes & 0x07FFU),
      QString::number((attributes >> 13U) & 0x03U),
      QStringLiteral("%1%2%3")
        .arg((attributes & 0x8000U) != 0U ? QStringLiteral("P") : QString{})
        .arg((attributes & 0x1000U) != 0U ? QStringLiteral("V") : QString{})
        .arg((attributes & 0x0800U) != 0U ? QStringLiteral("H") : QString{}),
    };
    for (int column = 0; column < static_cast<int>(values.size()); ++column) {
      auto* item = itemAt(*sprites_, sprite, column);
      item->setText(values[static_cast<std::size_t>(column)]);
      item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }
  }
  planeImage_->setPixmap(QPixmap::fromImage(
    makePlaneImage(snapshot_->vdp, planeSelector_->currentIndex())));
  const auto hscrollBase =
    static_cast<std::uint32_t>(snapshot_->vdp.registers[13] & 0x3FU) << 10U;
  for (int index = 0; index < 20; ++index) {
    const std::array values{
      QString::number(index),
      hexadecimal(vramWord(snapshot_->vdp.vram,
        hscrollBase + static_cast<std::uint32_t>(index * 4)), 4),
      hexadecimal(vramWord(snapshot_->vdp.vram,
        hscrollBase + static_cast<std::uint32_t>(index * 4 + 2)), 4),
      hexadecimal(snapshot_->vdp.vsram[static_cast<std::size_t>(index * 2)], 4),
      hexadecimal(snapshot_->vdp.vsram[static_cast<std::size_t>(index * 2 + 1)], 4),
    };
    for (int column = 0; column < static_cast<int>(values.size()); ++column) {
      auto* item = itemAt(*scroll_, index, column);
      item->setText(values[static_cast<std::size_t>(column)]);
      item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }
  }
  updating_ = previousUpdating;
}

void DebugToolsWindow::updateSoundViews()
{
  for (int address = 0; address < 256; ++address) {
    setRegisterRow(*fmRegisters_, address, hexadecimal(address, 2),
      snapshot_->sound.fmRegisters[0][static_cast<std::size_t>(address)],
      2, false);
    auto* bankOne = itemAt(*fmRegisters_, address, 2);
    bankOne->setText(hexadecimal(
      snapshot_->sound.fmRegisters[1][static_cast<std::size_t>(address)], 2));
    bankOne->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
  }
  const std::array names{
    "Tone 0", "Attenuation 0", "Tone 1", "Attenuation 1",
    "Tone 2", "Attenuation 2", "Noise", "Noise attenuation"};
  for (int row = 0; row < 8; ++row) {
    setRegisterRow(*psgRegisters_, row,
      QString::fromLatin1(names[static_cast<std::size_t>(row)]),
      static_cast<std::uint32_t>(
        snapshot_->sound.psgRegisters[static_cast<std::size_t>(row)]),
      4, false);
  }
}

void DebugToolsWindow::updateInputView()
{
  for (int player = 0; player < 8; ++player) {
    const std::array values{
      tr("Player %1").arg(player + 1),
      hexadecimal(snapshot_->input.buttons[static_cast<std::size_t>(player)], 4),
      QString::number(snapshot_->input.analog[static_cast<std::size_t>(player)][0]),
      QString::number(snapshot_->input.analog[static_cast<std::size_t>(player)][1]),
    };
    for (int column = 0; column < static_cast<int>(values.size()); ++column) {
      auto* item = itemAt(*inputState_, player, column);
      item->setText(values[static_cast<std::size_t>(column)]);
      item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }
  }
}

void DebugToolsWindow::applyM68kEdit(int row, int column)
{
  if (updating_ || column != 1 || !paused_ || !snapshot_) {
    return;
  }
  std::uint64_t value = 0U;
  const auto maximum = row == 17 ? 0xFFFFU : 0xFFFFFFFFU;
  if (!parseHex(m68kRegisters_->item(row, column)->text(), maximum, value)) {
    setStatus(tr("Invalid 68K hexadecimal register value."), 4'000);
    updateCpuViews();
    return;
  }
  CoreDebugRequest request;
  request.type = CoreDebugRequestType::setM68kRegisters;
  request.m68k = snapshot_->m68k;
  if (row < 8) {
    request.m68k.data[static_cast<std::size_t>(row)] =
      static_cast<std::uint32_t>(value);
  } else if (row < 16) {
    request.m68k.address[static_cast<std::size_t>(row - 8)] =
      static_cast<std::uint32_t>(value);
  } else if (row == 16) {
    request.m68k.programCounter = static_cast<std::uint32_t>(value);
  } else if (row == 17) {
    request.m68k.status = static_cast<std::uint32_t>(value);
  } else if (row == 18) {
    request.m68k.userStackPointer = static_cast<std::uint32_t>(value);
  } else if (row == 19) {
    request.m68k.interruptStackPointer = static_cast<std::uint32_t>(value);
  }
  static_cast<void>(submit(std::move(request)));
}

void DebugToolsWindow::applyZ80Edit(int row, int column)
{
  if (updating_ || column != 1 || !paused_ || !snapshot_) {
    return;
  }
  const auto maximum = row < 12 ? 0xFFFFU : (row == 18 ? 0xFFFFFFU : 0xFFU);
  std::uint64_t value = 0U;
  if (!parseHex(z80Registers_->item(row, column)->text(), maximum, value) ||
      (row == 14 && value > 2U) ||
      ((row >= 15 && row <= 17) && value > 1U)) {
    setStatus(tr("Invalid Z80 hexadecimal register value."), 4'000);
    updateCpuViews();
    return;
  }
  CoreDebugRequest request;
  request.type = CoreDebugRequestType::setZ80Registers;
  request.z80 = snapshot_->z80;
  const auto value16 = static_cast<std::uint16_t>(value);
  switch (row) {
    case 0: request.z80.af = value16; break;
    case 1: request.z80.bc = value16; break;
    case 2: request.z80.de = value16; break;
    case 3: request.z80.hl = value16; break;
    case 4: request.z80.afAlternate = value16; break;
    case 5: request.z80.bcAlternate = value16; break;
    case 6: request.z80.deAlternate = value16; break;
    case 7: request.z80.hlAlternate = value16; break;
    case 8: request.z80.ix = value16; break;
    case 9: request.z80.iy = value16; break;
    case 10: request.z80.stackPointer = value16; break;
    case 11: request.z80.programCounter = value16; break;
    case 12: request.z80.interruptVector = static_cast<std::uint8_t>(value); break;
    case 13: request.z80.refresh = static_cast<std::uint8_t>(value); break;
    case 14: request.z80.interruptMode = static_cast<std::uint8_t>(value); break;
    case 15: request.z80.interruptFlipFlop1 = value != 0U; break;
    case 16: request.z80.interruptFlipFlop2 = value != 0U; break;
    case 17: request.z80.halted = value != 0U; break;
    case 18: request.z80.bank = static_cast<std::uint32_t>(value); break;
    default: return;
  }
  static_cast<void>(submit(std::move(request)));
}

void DebugToolsWindow::applyVdpEdit(int row, int column)
{
  if (updating_ || column != 1 || !paused_ || !snapshot_) {
    return;
  }
  std::uint64_t value = 0U;
  if (!parseHex(vdpRegisters_->item(row, column)->text(), 0xFFU, value)) {
    setStatus(tr("Invalid VDP register byte."), 4'000);
    updateVdpViews();
    return;
  }
  CoreDebugRequest request;
  request.type = CoreDebugRequestType::setVdpRegister;
  request.vdpRegister = static_cast<std::uint8_t>(row);
  request.vdpValue = static_cast<std::uint8_t>(value);
  static_cast<void>(submit(std::move(request)));
}

void DebugToolsWindow::setStatus(const QString& text, int timeout)
{
  statusBar()->showMessage(text, timeout);
}

} // namespace genplusgx::ui
