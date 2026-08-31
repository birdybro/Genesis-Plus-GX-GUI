#include "genplusgx/ui/debug_tools_window.h"
#include "genplusgx/core_adapter.h"

#include <QAction>
#include <QColor>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QImage>
#include <QItemSelectionModel>
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

DebugValueWidth selectedWidth(const QComboBox& combo)
{
  return static_cast<DebugValueWidth>(combo.currentData().toUInt());
}

DebugValueFormat selectedFormat(const QCheckBox& signedValues)
{
  return signedValues.isChecked()
    ? DebugValueFormat::signedInteger
    : DebugValueFormat::unsignedInteger;
}

DebugValueEndian regionEndian(CoreDebugMemoryRegion region)
{
  return region == CoreDebugMemoryRegion::z80Ram
    ? DebugValueEndian::little : DebugValueEndian::big;
}

std::span<const std::uint8_t> snapshotMemory(
  const CoreDebugSnapshot& snapshot,
  CoreDebugMemoryRegion region)
{
  switch (region) {
    case CoreDebugMemoryRegion::m68kRam: return snapshot.m68kRam;
    case CoreDebugMemoryRegion::z80Ram: return snapshot.z80Ram;
    default: return {};
  }
}

QString analysisRegionName(CoreDebugMemoryRegion region)
{
  return region == CoreDebugMemoryRegion::z80Ram
    ? QStringLiteral("Z80 RAM") : QStringLiteral("68K RAM");
}

QString analysisValue(
  std::uint32_t value,
  DebugValueWidth width,
  DebugValueFormat format)
{
  const int digits = static_cast<int>(width) * 2;
  const auto interpreted = debugInterpretValue(value, width, format);
  if (format == DebugValueFormat::signedInteger) {
    return QStringLiteral("%1 (%2)").arg(hexadecimal(value, digits))
      .arg(static_cast<qlonglong>(interpreted));
  }
  return hexadecimal(value, digits);
}

bool comparisonUsesValue(DebugRamComparison comparison)
{
  return comparison == DebugRamComparison::equalTo ||
    comparison == DebugRamComparison::notEqualTo ||
    comparison == DebugRamComparison::greaterThan ||
    comparison == DebugRamComparison::lessThan;
}

bool parseAnalysisValue(
  const QString& text,
  DebugValueWidth width,
  DebugValueFormat format,
  std::int64_t& output)
{
  const auto normalized = text.trimmed();
  const auto bits = static_cast<unsigned int>(width) * 8U;
  const auto maximumUnsigned = bits == 32U
    ? std::numeric_limits<std::uint32_t>::max()
    : ((std::uint32_t{1U} << bits) - 1U);
  if (normalized.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
    bool ok = false;
    const auto raw = normalized.mid(2).toULongLong(&ok, 16);
    if (!ok || raw > maximumUnsigned) {
      return false;
    }
    output = debugInterpretValue(
      static_cast<std::uint32_t>(raw), width, format);
    return true;
  }
  bool ok = false;
  const auto parsed = normalized.toLongLong(&ok, 10);
  if (!ok) {
    return false;
  }
  if (format == DebugValueFormat::unsignedInteger) {
    if (parsed < 0 || static_cast<std::uint64_t>(parsed) > maximumUnsigned) {
      return false;
    }
  } else {
    const auto minimum = -(std::int64_t{1} << (bits - 1U));
    const auto maximum = (std::int64_t{1} << (bits - 1U)) - 1;
    if (parsed < minimum || parsed > maximum) {
      return false;
    }
  }
  output = parsed;
  return true;
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
  tabs_ = new QTabWidget(this);
  tabs_->setObjectName(QStringLiteral("debugToolsTabs"));
  setCentralWidget(tabs_);
  buildCpuPage();
  buildMemoryPage();
  buildVdpPage();
  buildSoundPage();
  buildInputPage();
  buildAnalysisPage();
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

void DebugToolsWindow::buildAnalysisPage()
{
  analysisPage_ = new QWidget(this);
  analysisPage_->setObjectName(QStringLiteral("debugAnalysisPage"));
  auto* layout = new QVBoxLayout(analysisPage_);
  auto* explanation = new QLabel(tr(
    "RAM analysis uses immutable frame snapshots. Program-counter breakpoints "
    "are checked after each complete frame and pause the emulation worker on a match."),
    analysisPage_);
  explanation->setWordWrap(true);
  layout->addWidget(explanation);
  analysisTabs_ = new QTabWidget(analysisPage_);
  analysisTabs_->setObjectName(QStringLiteral("debugAnalysisTabs"));
  layout->addWidget(analysisTabs_);

  auto* searchPage = new QWidget(analysisTabs_);
  searchPage->setObjectName(QStringLiteral("debugRamSearchPage"));
  auto* searchLayout = new QVBoxLayout(searchPage);
  auto* searchForm = new QFormLayout;
  ramSearchRegion_ = new QComboBox(searchPage);
  ramSearchRegion_->setObjectName(QStringLiteral("debugRamSearchRegionCombo"));
  ramSearchRegion_->addItem(tr("68K RAM"),
    static_cast<int>(CoreDebugMemoryRegion::m68kRam));
  ramSearchRegion_->addItem(tr("Z80 RAM"),
    static_cast<int>(CoreDebugMemoryRegion::z80Ram));
  ramSearchWidth_ = new QComboBox(searchPage);
  ramSearchWidth_->setObjectName(QStringLiteral("debugRamSearchWidthCombo"));
  ramSearchWidth_->addItem(tr("8-bit"), static_cast<int>(DebugValueWidth::byte));
  ramSearchWidth_->addItem(tr("16-bit"), static_cast<int>(DebugValueWidth::word));
  ramSearchWidth_->addItem(tr("32-bit"),
    static_cast<int>(DebugValueWidth::longWord));
  ramSearchSigned_ = new QCheckBox(tr("Interpret as signed"), searchPage);
  ramSearchSigned_->setObjectName(QStringLiteral("debugRamSearchSignedCheck"));
  ramSearchComparison_ = new QComboBox(searchPage);
  ramSearchComparison_->setObjectName(
    QStringLiteral("debugRamSearchComparisonCombo"));
  const std::array comparisons{
    std::pair{tr("Equal to"), DebugRamComparison::equalTo},
    std::pair{tr("Not equal to"), DebugRamComparison::notEqualTo},
    std::pair{tr("Changed"), DebugRamComparison::changed},
    std::pair{tr("Unchanged"), DebugRamComparison::unchanged},
    std::pair{tr("Increased"), DebugRamComparison::increased},
    std::pair{tr("Decreased"), DebugRamComparison::decreased},
    std::pair{tr("Greater than"), DebugRamComparison::greaterThan},
    std::pair{tr("Less than"), DebugRamComparison::lessThan},
  };
  for (const auto& [name, comparison] : comparisons) {
    ramSearchComparison_->addItem(name, static_cast<int>(comparison));
  }
  ramSearchValue_ = new QLineEdit(searchPage);
  ramSearchValue_->setObjectName(QStringLiteral("debugRamSearchValueEdit"));
  ramSearchValue_->setText(QStringLiteral("0"));
  ramSearchValue_->setPlaceholderText(tr("Decimal or 0x-prefixed hexadecimal"));
  searchForm->addRow(tr("&Region:"), ramSearchRegion_);
  searchForm->addRow(tr("&Width:"), ramSearchWidth_);
  searchForm->addRow(QString{}, ramSearchSigned_);
  searchForm->addRow(tr("&Comparison:"), ramSearchComparison_);
  searchForm->addRow(tr("&Value:"), ramSearchValue_);
  searchLayout->addLayout(searchForm);
  auto* searchButtons = new QHBoxLayout;
  auto* newSearch = new QPushButton(tr("New Search"), searchPage);
  newSearch->setObjectName(QStringLiteral("debugRamSearchNewButton"));
  auto* filterSearch = new QPushButton(tr("Filter"), searchPage);
  filterSearch->setObjectName(QStringLiteral("debugRamSearchFilterButton"));
  auto* resetSearch = new QPushButton(tr("Reset"), searchPage);
  resetSearch->setObjectName(QStringLiteral("debugRamSearchResetButton"));
  searchButtons->addWidget(newSearch);
  searchButtons->addWidget(filterSearch);
  searchButtons->addWidget(resetSearch);
  searchButtons->addStretch();
  searchLayout->addLayout(searchButtons);
  ramSearchCount_ = new QLabel(tr("No active search."), searchPage);
  ramSearchCount_->setObjectName(QStringLiteral("debugRamSearchCountLabel"));
  searchLayout->addWidget(ramSearchCount_);
  ramSearchResults_ = makeTable(searchPage, "debugRamSearchResultsTable", 0,
    {tr("Address"), tr("Current"), tr("Previous filter")});
  searchLayout->addWidget(ramSearchResults_);
  connect(newSearch, &QPushButton::clicked,
    this, &DebugToolsWindow::beginRamSearch);
  connect(filterSearch, &QPushButton::clicked,
    this, &DebugToolsWindow::filterRamSearch);
  connect(resetSearch, &QPushButton::clicked,
    this, &DebugToolsWindow::resetRamSearch);
  connect(ramSearchComparison_, &QComboBox::currentIndexChanged,
    this, &DebugToolsWindow::updateSearchValueControl);
  const auto invalidateSearch = [this] {
    if (ramSearch_.active()) {
      resetRamSearch();
      setStatus(tr("Search layout changed; start a new RAM search."), 3'000);
    }
  };
  connect(ramSearchRegion_, &QComboBox::currentIndexChanged,
    this, invalidateSearch);
  connect(ramSearchWidth_, &QComboBox::currentIndexChanged,
    this, invalidateSearch);
  connect(ramSearchSigned_, &QCheckBox::toggled,
    this, invalidateSearch);
  analysisTabs_->addTab(searchPage, tr("RAM Search"));

  auto* watchPage = new QWidget(analysisTabs_);
  watchPage->setObjectName(QStringLiteral("debugRamWatchPage"));
  auto* watchLayout = new QVBoxLayout(watchPage);
  auto* watchControls = new QHBoxLayout;
  watchRegion_ = new QComboBox(watchPage);
  watchRegion_->setObjectName(QStringLiteral("debugWatchRegionCombo"));
  watchRegion_->addItem(tr("68K RAM"),
    static_cast<int>(CoreDebugMemoryRegion::m68kRam));
  watchRegion_->addItem(tr("Z80 RAM"),
    static_cast<int>(CoreDebugMemoryRegion::z80Ram));
  watchAddress_ = new QSpinBox(watchPage);
  watchAddress_->setObjectName(QStringLiteral("debugWatchAddressSpin"));
  watchAddress_->setDisplayIntegerBase(16);
  watchAddress_->setPrefix(QStringLiteral("0x"));
  watchWidth_ = new QComboBox(watchPage);
  watchWidth_->setObjectName(QStringLiteral("debugWatchWidthCombo"));
  watchWidth_->addItem(tr("8-bit"), static_cast<int>(DebugValueWidth::byte));
  watchWidth_->addItem(tr("16-bit"), static_cast<int>(DebugValueWidth::word));
  watchWidth_->addItem(tr("32-bit"), static_cast<int>(DebugValueWidth::longWord));
  watchSigned_ = new QCheckBox(tr("Signed"), watchPage);
  watchSigned_->setObjectName(QStringLiteral("debugWatchSignedCheck"));
  auto* addWatch = new QPushButton(tr("Add Watch"), watchPage);
  addWatch->setObjectName(QStringLiteral("debugWatchAddButton"));
  auto* removeWatch = new QPushButton(tr("Remove Selected"), watchPage);
  removeWatch->setObjectName(QStringLiteral("debugWatchRemoveButton"));
  watchControls->addWidget(watchRegion_);
  watchControls->addWidget(watchAddress_);
  watchControls->addWidget(watchWidth_);
  watchControls->addWidget(watchSigned_);
  watchControls->addWidget(addWatch);
  watchControls->addWidget(removeWatch);
  watchLayout->addLayout(watchControls);
  watchTable_ = makeTable(watchPage, "debugWatchTable", 0,
    {tr("Region"), tr("Address"), tr("Type"), tr("Value"), tr("Changed")});
  watchLayout->addWidget(watchTable_);
  connect(watchRegion_, &QComboBox::currentIndexChanged,
    this, &DebugToolsWindow::updateWatchAddressRange);
  connect(watchWidth_, &QComboBox::currentIndexChanged,
    this, &DebugToolsWindow::updateWatchAddressRange);
  connect(addWatch, &QPushButton::clicked,
    this, &DebugToolsWindow::addMemoryWatch);
  connect(removeWatch, &QPushButton::clicked,
    this, &DebugToolsWindow::removeMemoryWatch);
  updateWatchAddressRange();
  analysisTabs_->addTab(watchPage, tr("RAM Watch"));

  auto* breakpointPage = new QWidget(analysisTabs_);
  breakpointPage->setObjectName(QStringLiteral("debugBreakpointPage"));
  auto* breakpointLayout = new QVBoxLayout(breakpointPage);
  auto* breakpointNote = new QLabel(tr(
    "Frame-boundary breakpoints compare the selected CPU program counter after "
    "each completed frame. They do not stop in the middle of an instruction."),
    breakpointPage);
  breakpointNote->setWordWrap(true);
  breakpointLayout->addWidget(breakpointNote);
  auto* breakpointControls = new QHBoxLayout;
  breakpointCpu_ = new QComboBox(breakpointPage);
  breakpointCpu_->setObjectName(QStringLiteral("debugBreakpointCpuCombo"));
  breakpointCpu_->addItem(tr("68000"), static_cast<int>(CoreDebugCpu::m68k));
  breakpointCpu_->addItem(tr("Z80"), static_cast<int>(CoreDebugCpu::z80));
  breakpointAddress_ = new QSpinBox(breakpointPage);
  breakpointAddress_->setObjectName(QStringLiteral("debugBreakpointAddressSpin"));
  breakpointAddress_->setDisplayIntegerBase(16);
  breakpointAddress_->setPrefix(QStringLiteral("0x"));
  auto* addBreakpoint = new QPushButton(tr("Add Breakpoint"), breakpointPage);
  addBreakpoint->setObjectName(QStringLiteral("debugBreakpointAddButton"));
  auto* removeBreakpoint = new QPushButton(
    tr("Remove Selected"), breakpointPage);
  removeBreakpoint->setObjectName(QStringLiteral("debugBreakpointRemoveButton"));
  breakpointControls->addWidget(breakpointCpu_);
  breakpointControls->addWidget(breakpointAddress_);
  breakpointControls->addWidget(addBreakpoint);
  breakpointControls->addWidget(removeBreakpoint);
  breakpointControls->addStretch();
  breakpointLayout->addLayout(breakpointControls);
  breakpointTable_ = makeTable(breakpointPage, "debugBreakpointTable", 0,
    {tr("CPU"), tr("Program counter")});
  breakpointLayout->addWidget(breakpointTable_);
  connect(breakpointCpu_, &QComboBox::currentIndexChanged,
    this, &DebugToolsWindow::updateBreakpointAddressRange);
  connect(addBreakpoint, &QPushButton::clicked,
    this, &DebugToolsWindow::addFrameBreakpoint);
  connect(removeBreakpoint, &QPushButton::clicked,
    this, &DebugToolsWindow::removeFrameBreakpoint);
  updateBreakpointAddressRange();
  analysisTabs_->addTab(breakpointPage, tr("Breakpoints"));

  updateSearchValueControl();
  tabs_->addTab(analysisPage_, tr("Analysis"));
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
    ramSearch_.clear();
    watches_.clear();
    frameBreakpoints_.clear();
    updateRamSearchTable();
    updateMemoryWatches();
    breakpointTable_->setRowCount(0);
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
  analysisPage_->setEnabled(gameLoaded_);
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
      const bool firstSnapshot = !snapshot_;
      auto nextSnapshot = std::move(response.snapshot);
      if (firstSnapshot) {
        const auto activeRam = nextSnapshot->m68kActive
          ? CoreDebugMemoryRegion::m68kRam : CoreDebugMemoryRegion::z80Ram;
        const auto activeCpu = nextSnapshot->m68kActive
          ? CoreDebugCpu::m68k : CoreDebugCpu::z80;
        const auto selectData = [](QComboBox& combo, int itemData) {
          const auto index = combo.findData(itemData);
          if (index >= 0) {
            combo.setCurrentIndex(index);
          }
        };
        selectData(*memoryRegion_, static_cast<int>(activeRam));
        selectData(*ramSearchRegion_, static_cast<int>(activeRam));
        selectData(*watchRegion_, static_cast<int>(activeRam));
        selectData(*breakpointCpu_, static_cast<int>(activeCpu));
      }
      snapshot_ = std::move(nextSnapshot);
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
  if (response.type == CoreDebugRequestType::setFrameBreakpoints) {
    if (response.breakpointHit) {
      setPaused(true);
      tabs_->setCurrentWidget(analysisPage_);
      analysisTabs_->setCurrentIndex(2);
      const auto cpu = response.breakpointHit->cpu == CoreDebugCpu::m68k
        ? tr("68000") : tr("Z80");
      const int digits = response.breakpointHit->cpu == CoreDebugCpu::m68k ? 6 : 4;
      setStatus(tr("%1 frame-boundary breakpoint hit at %2.")
        .arg(cpu, hexadecimal(response.breakpointHit->address, digits)));
      requestRefresh();
    } else {
      setStatus(tr("Frame-boundary breakpoint list updated."), 2'000);
    }
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
  updateRamSearchTable();
  updateMemoryWatches();
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

void DebugToolsWindow::beginRamSearch()
{
  if (!snapshot_) {
    setStatus(tr("Load a game and wait for a debug snapshot first."), 4'000);
    return;
  }
  ramSearchMemoryRegion_ = static_cast<CoreDebugMemoryRegion>(
    ramSearchRegion_->currentData().toInt());
  ramSearchFormat_ = selectedFormat(*ramSearchSigned_);
  const auto memory = snapshotMemory(*snapshot_, ramSearchMemoryRegion_);
  if (!ramSearch_.begin(memory, selectedWidth(*ramSearchWidth_),
        regionEndian(ramSearchMemoryRegion_))) {
    setStatus(tr("The selected RAM search cannot be initialized."), 4'000);
    return;
  }
  updateRamSearchTable();
  setStatus(tr("RAM search started with %1 candidates.")
    .arg(static_cast<qulonglong>(ramSearch_.candidates().size())), 3'000);
}

void DebugToolsWindow::filterRamSearch()
{
  if (!snapshot_ || !ramSearch_.active()) {
    setStatus(tr("Start a new RAM search before filtering."), 4'000);
    return;
  }
  const auto comparison = static_cast<DebugRamComparison>(
    ramSearchComparison_->currentData().toInt());
  std::int64_t value = 0;
  if (comparisonUsesValue(comparison) &&
      !parseAnalysisValue(ramSearchValue_->text(), ramSearch_.width(),
        ramSearchFormat_, value)) {
    setStatus(tr("The RAM search value is outside the selected type."), 4'000);
    return;
  }
  if (!ramSearch_.filter(snapshotMemory(*snapshot_, ramSearchMemoryRegion_),
        comparison, ramSearchFormat_, value)) {
    setStatus(tr("The current snapshot cannot refine this RAM search."), 4'000);
    return;
  }
  updateRamSearchTable();
  setStatus(tr("RAM search refined to %1 candidates.")
    .arg(static_cast<qulonglong>(ramSearch_.candidates().size())), 3'000);
}

void DebugToolsWindow::resetRamSearch()
{
  ramSearch_.clear();
  updateRamSearchTable();
}

void DebugToolsWindow::updateRamSearchTable()
{
  constexpr std::size_t maximumDisplayedCandidates = 1'024U;
  if (!ramSearch_.active() || !snapshot_) {
    ramSearchCount_->setText(tr("No active search."));
    ramSearchResults_->setRowCount(0);
    return;
  }
  const auto& candidates = ramSearch_.candidates();
  const auto displayed = std::min(candidates.size(), maximumDisplayedCandidates);
  ramSearchCount_->setText(candidates.size() > displayed
    ? tr("%1 candidates; showing the first %2.")
        .arg(static_cast<qulonglong>(candidates.size()))
        .arg(static_cast<qulonglong>(displayed))
    : tr("%1 candidates.").arg(static_cast<qulonglong>(candidates.size())));
  ramSearchResults_->setRowCount(static_cast<int>(displayed));
  const auto memory = snapshotMemory(*snapshot_, ramSearchMemoryRegion_);
  const auto base = ramSearchMemoryRegion_ == CoreDebugMemoryRegion::m68kRam
    ? 0x00FF'0000U : 0U;
  for (std::size_t row = 0U; row < displayed; ++row) {
    const auto& candidate = candidates[row];
    std::uint32_t current = 0U;
    static_cast<void>(debugReadValue(memory, candidate.offset,
      ramSearch_.width(), ramSearch_.endian(), current));
    const std::array values{
      hexadecimal(base + candidate.offset, ramSearchMemoryRegion_ ==
        CoreDebugMemoryRegion::m68kRam ? 6 : 4),
      analysisValue(current, ramSearch_.width(), ramSearchFormat_),
      analysisValue(candidate.previousValue,
        ramSearch_.width(), ramSearchFormat_),
    };
    for (int column = 0; column < static_cast<int>(values.size()); ++column) {
      auto* item = itemAt(*ramSearchResults_, static_cast<int>(row), column);
      item->setText(values[static_cast<std::size_t>(column)]);
      item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }
  }
}

void DebugToolsWindow::updateSearchValueControl()
{
  const auto comparison = static_cast<DebugRamComparison>(
    ramSearchComparison_->currentData().toInt());
  ramSearchValue_->setEnabled(comparisonUsesValue(comparison));
}

void DebugToolsWindow::updateWatchAddressRange()
{
  const auto region = static_cast<CoreDebugMemoryRegion>(
    watchRegion_->currentData().toInt());
  const auto regionSize = region == CoreDebugMemoryRegion::z80Ram
    ? 0x2000U : 0x10000U;
  const auto bytes = static_cast<std::uint32_t>(selectedWidth(*watchWidth_));
  watchAddress_->setMaximum(static_cast<int>(regionSize - bytes));
}

void DebugToolsWindow::addMemoryWatch()
{
  constexpr std::size_t maximumWatches = 256U;
  if (watches_.size() >= maximumWatches) {
    setStatus(tr("At most 256 RAM watches may be active."), 4'000);
    return;
  }
  DebugMemoryWatch watch{
    .region = static_cast<CoreDebugMemoryRegion>(
      watchRegion_->currentData().toInt()),
    .offset = static_cast<std::uint32_t>(watchAddress_->value()),
    .width = selectedWidth(*watchWidth_),
    .format = selectedFormat(*watchSigned_),
  };
  const auto duplicate = std::ranges::find_if(watches_, [&watch](const auto& item) {
    return item.region == watch.region && item.offset == watch.offset &&
      item.width == watch.width && item.format == watch.format;
  });
  if (duplicate != watches_.end()) {
    setStatus(tr("That RAM watch already exists."), 3'000);
    return;
  }
  watches_.push_back(watch);
  updateMemoryWatches();
}

void DebugToolsWindow::removeMemoryWatch()
{
  auto rows = watchTable_->selectionModel()->selectedRows();
  std::ranges::sort(rows, [](const QModelIndex& left, const QModelIndex& right) {
    return left.row() > right.row();
  });
  for (const auto& index : rows) {
    if (index.row() >= 0 &&
        static_cast<std::size_t>(index.row()) < watches_.size()) {
      watches_.erase(watches_.begin() + index.row());
    }
  }
  updateMemoryWatches();
}

void DebugToolsWindow::updateMemoryWatches()
{
  watchTable_->setRowCount(static_cast<int>(watches_.size()));
  for (std::size_t row = 0U; row < watches_.size(); ++row) {
    auto& watch = watches_[row];
    std::uint32_t current = 0U;
    const bool available = snapshot_ && debugReadValue(
      snapshotMemory(*snapshot_, watch.region), watch.offset,
      watch.width, regionEndian(watch.region), current);
    const bool changed = available && watch.initialized &&
      current != watch.previousValue;
    const auto base = watch.region == CoreDebugMemoryRegion::m68kRam
      ? 0x00FF'0000U : 0U;
    const std::array values{
      analysisRegionName(watch.region),
      hexadecimal(base + watch.offset,
        watch.region == CoreDebugMemoryRegion::m68kRam ? 6 : 4),
      tr("%1-bit %2")
        .arg(static_cast<int>(watch.width) * 8)
        .arg(watch.format == DebugValueFormat::signedInteger
          ? tr("signed") : tr("unsigned")),
      available ? analysisValue(current, watch.width, watch.format)
                : tr("Unavailable"),
      changed ? tr("Yes") : tr("No"),
    };
    for (int column = 0; column < static_cast<int>(values.size()); ++column) {
      auto* item = itemAt(*watchTable_, static_cast<int>(row), column);
      item->setText(values[static_cast<std::size_t>(column)]);
      item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }
    if (available) {
      watch.previousValue = current;
      watch.initialized = true;
    }
  }
}

void DebugToolsWindow::updateBreakpointAddressRange()
{
  const auto cpu = static_cast<CoreDebugCpu>(
    breakpointCpu_->currentData().toInt());
  breakpointAddress_->setMaximum(
    cpu == CoreDebugCpu::m68k ? 0x00FF'FFFF : 0x0000'FFFF);
}

void DebugToolsWindow::addFrameBreakpoint()
{
  if (frameBreakpoints_.size() >= maximumCoreDebugBreakpoints) {
    setStatus(tr("At most 64 frame-boundary breakpoints may be active."), 4'000);
    return;
  }
  const CoreDebugBreakpoint breakpoint{
    .cpu = static_cast<CoreDebugCpu>(breakpointCpu_->currentData().toInt()),
    .address = static_cast<std::uint32_t>(breakpointAddress_->value()),
  };
  if (std::ranges::find(frameBreakpoints_, breakpoint) != frameBreakpoints_.end()) {
    setStatus(tr("That frame-boundary breakpoint already exists."), 3'000);
    return;
  }
  frameBreakpoints_.push_back(breakpoint);
  updateBreakpointTable();
  if (!submitFrameBreakpoints()) {
    frameBreakpoints_.pop_back();
    updateBreakpointTable();
  }
}

void DebugToolsWindow::removeFrameBreakpoint()
{
  const auto previous = frameBreakpoints_;
  auto rows = breakpointTable_->selectionModel()->selectedRows();
  std::ranges::sort(rows, [](const QModelIndex& left, const QModelIndex& right) {
    return left.row() > right.row();
  });
  for (const auto& index : rows) {
    if (index.row() >= 0 &&
        static_cast<std::size_t>(index.row()) < frameBreakpoints_.size()) {
      frameBreakpoints_.erase(frameBreakpoints_.begin() + index.row());
    }
  }
  updateBreakpointTable();
  if (!submitFrameBreakpoints()) {
    frameBreakpoints_ = previous;
    updateBreakpointTable();
  }
}

void DebugToolsWindow::updateBreakpointTable()
{
  breakpointTable_->setRowCount(static_cast<int>(frameBreakpoints_.size()));
  for (std::size_t row = 0U; row < frameBreakpoints_.size(); ++row) {
    const auto& breakpoint = frameBreakpoints_[row];
    const std::array values{
      breakpoint.cpu == CoreDebugCpu::m68k ? tr("68000") : tr("Z80"),
      hexadecimal(breakpoint.address,
        breakpoint.cpu == CoreDebugCpu::m68k ? 6 : 4),
    };
    for (int column = 0; column < static_cast<int>(values.size()); ++column) {
      auto* item = itemAt(*breakpointTable_, static_cast<int>(row), column);
      item->setText(values[static_cast<std::size_t>(column)]);
      item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }
  }
}

bool DebugToolsWindow::submitFrameBreakpoints()
{
  CoreDebugRequest request;
  request.type = CoreDebugRequestType::setFrameBreakpoints;
  request.breakpoints = frameBreakpoints_;
  return submit(std::move(request));
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
