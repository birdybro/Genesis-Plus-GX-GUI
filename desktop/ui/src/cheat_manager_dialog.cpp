#include "genplusgx/ui/cheat_manager_dialog.h"
#include "genplusgx/ui/dialog_service.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <utility>

namespace genplusgx::ui {
namespace {

constexpr int enabledColumn = 0;
constexpr int nameColumn = 1;
constexpr int codeColumn = 2;
constexpr int maximumDisplayedSearchResults = 1'024;
constexpr std::uint64_t cheatSearchClientToken = 0x4348454154ULL;

struct SearchProfile final {
  CoreDebugMemoryRegion region;
  DebugValueWidth width;
  DebugValueEndian endian;
  std::uint32_t displayBase;
  int addressDigits;
};

SearchProfile searchProfile(cheats::CheatSystem system)
{
  return system == cheats::CheatSystem::genesis
    ? SearchProfile{CoreDebugMemoryRegion::m68kRam, DebugValueWidth::word,
        DebugValueEndian::big, 0xFF0000U, 6}
    : SearchProfile{CoreDebugMemoryRegion::z80Ram, DebugValueWidth::byte,
        DebugValueEndian::little, 0xC000U, 4};
}

std::span<const std::uint8_t> searchMemory(
  const CoreDebugSnapshot& snapshot, cheats::CheatSystem system)
{
  if (system == cheats::CheatSystem::genesis) {
    return snapshot.m68kActive
      ? std::span<const std::uint8_t>{snapshot.m68kRam}
      : std::span<const std::uint8_t>{};
  }
  return !snapshot.m68kActive
    ? std::span<const std::uint8_t>{snapshot.z80Ram}
    : std::span<const std::uint8_t>{};
}

bool comparisonUsesValue(DebugRamComparison comparison)
{
  return comparison == DebugRamComparison::equalTo ||
    comparison == DebugRamComparison::notEqualTo ||
    comparison == DebugRamComparison::greaterThan ||
    comparison == DebugRamComparison::lessThan;
}

QString hexadecimal(std::uint32_t value, int digits)
{
  return QStringLiteral("0x%1")
    .arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

bool parseSearchValue(const QString& text,
  DebugValueWidth width,
  DebugValueFormat format,
  std::int64_t& output)
{
  const auto bits = static_cast<unsigned int>(width) * 8U;
  if (format == DebugValueFormat::signedInteger) {
    bool ok = false;
    const auto parsed = text.trimmed().toLongLong(&ok, 0);
    const auto minimum = -(std::int64_t{1} << (bits - 1U));
    const auto maximum = (std::int64_t{1} << (bits - 1U)) - 1;
    if (!ok || parsed < minimum || parsed > maximum) {
      return false;
    }
    output = parsed;
    return true;
  }
  bool ok = false;
  const auto parsed = text.trimmed().toULongLong(&ok, 0);
  const auto maximum = (std::uint64_t{1} << bits) - 1U;
  if (!ok || parsed > maximum) {
    return false;
  }
  output = static_cast<std::int64_t>(parsed);
  return true;
}

void appendDefinitionRow(
  QTableWidget& table, const cheats::CheatDefinition& entry)
{
  const int row = table.rowCount();
  table.insertRow(row);
  auto* enabled = new QTableWidgetItem;
  enabled->setFlags(
    (enabled->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
  enabled->setCheckState(entry.enabled ? Qt::Checked : Qt::Unchecked);
  enabled->setTextAlignment(Qt::AlignCenter);
  table.setItem(row, enabledColumn, enabled);
  table.setItem(row, nameColumn,
    new QTableWidgetItem(QString::fromStdString(entry.name)));
  table.setItem(row, codeColumn,
    new QTableWidgetItem(QString::fromStdString(entry.code)));
}

} // namespace

CheatManagerDialog::CheatManagerDialog(
  cheats::CheatSystem system, cheats::CheatConfiguration configuration, QWidget* parent)
    : QDialog(parent), system_(system), configuration_(std::move(configuration))
{
  setObjectName(QStringLiteral("cheatManagerDialog"));
  setWindowTitle(tr("Cheat Manager"));
  setModal(true);
  setMinimumSize(700, 440);

  auto* layout = new QVBoxLayout(this);
  auto* explanation = new QLabel(this);
  explanation->setObjectName(QStringLiteral("cheatManagerExplanation"));
  explanation->setWordWrap(true);
  explanation->setText(
    system_ == cheats::CheatSystem::genesis
      ? tr("Genesis / Mega Drive and Sega CD games accept Game Genie "
           "(XXXX-XXXX) and Action Replay (XXXXXX:XXXX) codes. Join "
           "multi-part codes with +.")
      : tr("8-bit systems accept Master System / Game Gear Game Genie, "
           "Action Replay, and Fusion RAM/ROM codes. Join multi-part codes with +."));
  layout->addWidget(explanation);

  tabs_ = new QTabWidget(this);
  tabs_->setObjectName(QStringLiteral("cheatManagerTabs"));
  auto* codesPage = new QWidget(tabs_);
  codesPage->setObjectName(QStringLiteral("cheatCodesPage"));
  auto* codesLayout = new QVBoxLayout(codesPage);

  table_ = new QTableWidget(codesPage);
  table_->setObjectName(QStringLiteral("cheatTable"));
  table_->setAccessibleName(tr("Per-game cheat definitions"));
  table_->setColumnCount(3);
  table_->setHorizontalHeaderLabels({tr("Enabled"), tr("Name"), tr("Code")});
  table_->horizontalHeader()->setSectionResizeMode(
    enabledColumn, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(nameColumn, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(codeColumn, QHeaderView::Stretch);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::DoubleClicked |
                          QAbstractItemView::EditKeyPressed |
                          QAbstractItemView::SelectedClicked);
  codesLayout->addWidget(table_, 1);

  auto* rowButtons = new QHBoxLayout;
  auto* addButton = new QPushButton(tr("&Add"), codesPage);
  addButton->setObjectName(QStringLiteral("addCheatButton"));
  auto* removeButton = new QPushButton(tr("&Remove"), codesPage);
  removeButton->setObjectName(QStringLiteral("removeCheatButton"));
  auto* importButton = new QPushButton(tr("&Import List…"), codesPage);
  importButton->setObjectName(QStringLiteral("importCheatListButton"));
  connect(addButton, &QPushButton::clicked, this, &CheatManagerDialog::addDefinition);
  connect(removeButton,
    &QPushButton::clicked,
    this,
    &CheatManagerDialog::removeSelectedDefinition);
  connect(importButton,
    &QPushButton::clicked,
    this,
    &CheatManagerDialog::importDefinitions);
  rowButtons->addWidget(addButton);
  rowButtons->addWidget(removeButton);
  rowButtons->addWidget(importButton);
  rowButtons->addStretch(1);
  codesLayout->addLayout(rowButtons);
  tabs_->addTab(codesPage, tr("Codes"));

  auto* searchPage = new QWidget(tabs_);
  searchPage->setObjectName(QStringLiteral("cheatSearchPage"));
  auto* searchLayout = new QVBoxLayout(searchPage);
  auto* searchExplanation = new QLabel(
    system_ == cheats::CheatSystem::genesis
      ? tr("Search 68K work RAM as 16-bit big-endian values. Results become "
           "disabled Action Replay codes until you explicitly enable and apply them.")
      : tr("Search the active 8 KiB work RAM as bytes. Results become disabled "
           "Fusion RAM codes until you explicitly enable and apply them."),
    searchPage);
  searchExplanation->setObjectName(QStringLiteral("cheatSearchExplanation"));
  searchExplanation->setWordWrap(true);
  searchLayout->addWidget(searchExplanation);
  auto* searchForm = new QFormLayout;
  searchComparison_ = new QComboBox(searchPage);
  searchComparison_->setObjectName(QStringLiteral("cheatSearchComparisonCombo"));
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
  for (const auto& [label, comparison] : comparisons) {
    searchComparison_->addItem(label, static_cast<int>(comparison));
  }
  searchSigned_ = new QCheckBox(tr("Interpret as signed"), searchPage);
  searchSigned_->setObjectName(QStringLiteral("cheatSearchSignedCheck"));
  searchValue_ = new QLineEdit(QStringLiteral("0"), searchPage);
  searchValue_->setObjectName(QStringLiteral("cheatSearchValueEdit"));
  searchValue_->setPlaceholderText(tr("Decimal or 0x-prefixed hexadecimal"));
  searchForm->addRow(tr("&Comparison:"), searchComparison_);
  searchForm->addRow(QString{}, searchSigned_);
  searchForm->addRow(tr("&Value:"), searchValue_);
  searchLayout->addLayout(searchForm);
  auto* searchButtons = new QHBoxLayout;
  searchNewButton_ = new QPushButton(tr("New Search"), searchPage);
  searchNewButton_->setObjectName(QStringLiteral("cheatSearchNewButton"));
  searchFilterButton_ = new QPushButton(tr("Filter"), searchPage);
  searchFilterButton_->setObjectName(QStringLiteral("cheatSearchFilterButton"));
  auto* resetSearchButton = new QPushButton(tr("Reset"), searchPage);
  resetSearchButton->setObjectName(QStringLiteral("cheatSearchResetButton"));
  searchAddButton_ = new QPushButton(tr("Add Selected as Cheat"), searchPage);
  searchAddButton_->setObjectName(QStringLiteral("cheatSearchAddButton"));
  searchButtons->addWidget(searchNewButton_);
  searchButtons->addWidget(searchFilterButton_);
  searchButtons->addWidget(resetSearchButton);
  searchButtons->addWidget(searchAddButton_);
  searchButtons->addStretch(1);
  searchLayout->addLayout(searchButtons);
  searchCount_ = new QLabel(tr("No active search."), searchPage);
  searchCount_->setObjectName(QStringLiteral("cheatSearchCountLabel"));
  searchLayout->addWidget(searchCount_);
  searchResults_ = new QTableWidget(searchPage);
  searchResults_->setObjectName(QStringLiteral("cheatSearchResultsTable"));
  searchResults_->setAccessibleName(tr("Cheat search results"));
  searchResults_->setColumnCount(3);
  searchResults_->setHorizontalHeaderLabels(
    {tr("Address"), tr("Current"), tr("Previous filter")});
  searchResults_->horizontalHeader()->setStretchLastSection(true);
  searchResults_->setSelectionBehavior(QAbstractItemView::SelectRows);
  searchResults_->setSelectionMode(QAbstractItemView::SingleSelection);
  searchResults_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  searchLayout->addWidget(searchResults_, 1);
  connect(searchNewButton_, &QPushButton::clicked,
    this, &CheatManagerDialog::requestNewSearch);
  connect(searchFilterButton_, &QPushButton::clicked,
    this, &CheatManagerDialog::requestSearchFilter);
  connect(resetSearchButton, &QPushButton::clicked,
    this, &CheatManagerDialog::resetSearch);
  connect(searchAddButton_, &QPushButton::clicked,
    this, &CheatManagerDialog::addSelectedSearchResult);
  connect(searchComparison_, &QComboBox::currentIndexChanged,
    this, &CheatManagerDialog::updateSearchValueControl);
  tabs_->addTab(searchPage, tr("Search RAM"));
  layout->addWidget(tabs_, 1);

  validationLabel_ = new QLabel(this);
  validationLabel_->setObjectName(QStringLiteral("cheatValidationLabel"));
  validationLabel_->setAccessibleName(tr("Cheat validation message"));
  validationLabel_->setWordWrap(true);
  validationLabel_->hide();
  layout->addWidget(validationLabel_);

  buttons_ = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
  buttons_->setObjectName(QStringLiteral("cheatManagerButtonBox"));
  buttons_->button(QDialogButtonBox::Ok)
    ->setObjectName(QStringLiteral("cheatOkButton"));
  buttons_->button(QDialogButtonBox::Cancel)
    ->setObjectName(QStringLiteral("cheatCancelButton"));
  buttons_->button(QDialogButtonBox::Apply)
    ->setObjectName(QStringLiteral("cheatApplyButton"));
  connect(
    buttons_->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
      static_cast<void>(applyChanges());
    });
  connect(buttons_->button(QDialogButtonBox::Ok), &QPushButton::clicked, this, [this] {
    if (applyChanges()) {
      accept();
    }
  });
  connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons_);

  populateTable();
  resetSearch();
  updateSearchValueControl();
}

void CheatManagerDialog::setConfigurationSink(ConfigurationSink sink)
{
  configurationSink_ = std::move(sink);
}

void CheatManagerDialog::setDialogService(
  std::shared_ptr<DialogService> service,
  std::filesystem::path initialDirectory)
{
  dialogService_ = std::move(service);
  if (!initialDirectory.empty() && initialDirectory.is_absolute()) {
    importDirectory_ = std::move(initialDirectory);
  }
}

void CheatManagerDialog::setDebugRequestSink(DebugRequestSink sink)
{
  debugRequestSink_ = std::move(sink);
}

void CheatManagerDialog::setConfiguration(cheats::CheatConfiguration configuration)
{
  if (!cheats::validateCheatConfiguration(system_, configuration)) {
    return;
  }
  configuration_ = std::move(configuration);
  populateTable();
}

void CheatManagerDialog::presentDebugResponse(CoreDebugResponse response)
{
  if (response.type != CoreDebugRequestType::captureSnapshot ||
      response.clientToken != cheatSearchClientToken ||
      pendingSearch_ == PendingSearch::none) {
    return;
  }
  const auto pending = pendingSearch_;
  pendingSearch_ = PendingSearch::none;
  searchNewButton_->setEnabled(true);
  searchFilterButton_->setEnabled(ramSearch_.active());
  if (!response.snapshot) {
    showValidationMessage(tr("The emulator returned an empty RAM snapshot."));
    return;
  }
  const auto memory = searchMemory(*response.snapshot, system_);
  if (memory.empty()) {
    showValidationMessage(tr(
      "The returned RAM snapshot does not match the loaded system."));
    return;
  }
  bool updated = false;
  if (pending == PendingSearch::begin) {
    const auto profile = searchProfile(system_);
    updated = ramSearch_.begin(memory, profile.width, profile.endian);
  } else {
    updated = ramSearch_.filter(
      memory, pendingComparison_, pendingFormat_, pendingValue_);
  }
  if (!updated) {
    showValidationMessage(tr("The RAM search could not process this snapshot."));
    return;
  }
  searchSnapshot_ = std::move(response.snapshot);
  validationLabel_->hide();
  updateSearchResults();
}

void CheatManagerDialog::showDebugRequestError(
  const std::string& detail, std::uint64_t clientToken)
{
  if (clientToken != cheatSearchClientToken ||
      pendingSearch_ == PendingSearch::none) {
    return;
  }
  pendingSearch_ = PendingSearch::none;
  searchNewButton_->setEnabled(true);
  searchFilterButton_->setEnabled(ramSearch_.active());
  showValidationMessage(tr("RAM snapshot failed: %1")
    .arg(QString::fromStdString(detail)));
}

const cheats::CheatConfiguration& CheatManagerDialog::configuration() const noexcept
{
  return configuration_;
}

bool CheatManagerDialog::applyChanges()
{
  auto candidate = configurationFromTable();
  const auto validated = cheats::validateCheatConfiguration(system_, candidate);
  if (!validated) {
    showValidationMessage(QString::fromStdString(validated.message));
    return false;
  }
  for (auto& entry : candidate.entries) {
    entry.code = cheats::parseCheatCode(system_, entry.code).normalizedCode;
  }
  if (configurationSink_) {
    const auto saved = configurationSink_(candidate);
    if (!saved) {
      showValidationMessage(QString::fromStdString(saved.message));
      return false;
    }
  }
  configuration_ = std::move(candidate);
  validationLabel_->hide();
  populateTable();
  return true;
}

cheats::CheatConfiguration CheatManagerDialog::configurationFromTable() const
{
  cheats::CheatConfiguration result;
  result.entries.reserve(static_cast<std::size_t>(table_->rowCount()));
  for (int row = 0; row < table_->rowCount(); ++row) {
    const auto* enabled = table_->item(row, enabledColumn);
    const auto* name = table_->item(row, nameColumn);
    const auto* code = table_->item(row, codeColumn);
    result.entries.push_back({
      .name = name == nullptr ? std::string{} : name->text().trimmed().toStdString(),
      .code = code == nullptr ? std::string{} : code->text().trimmed().toStdString(),
      .enabled = enabled != nullptr && enabled->checkState() == Qt::Checked,
    });
  }
  return result;
}

void CheatManagerDialog::populateTable()
{
  table_->setRowCount(0);
  for (const auto& entry : configuration_.entries) {
    const int row = table_->rowCount();
    table_->insertRow(row);
    auto* enabled = new QTableWidgetItem;
    enabled->setFlags(
      (enabled->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
    enabled->setCheckState(entry.enabled ? Qt::Checked : Qt::Unchecked);
    enabled->setTextAlignment(Qt::AlignCenter);
    table_->setItem(row, enabledColumn, enabled);
    table_->setItem(
      row, nameColumn, new QTableWidgetItem(QString::fromStdString(entry.name)));
    table_->setItem(
      row, codeColumn, new QTableWidgetItem(QString::fromStdString(entry.code)));
  }
}

void CheatManagerDialog::addDefinition()
{
  if (table_->rowCount() >= static_cast<int>(cheats::maximumCheatDefinitions)) {
    showValidationMessage(tr("A game can contain at most 150 cheat definitions."));
    return;
  }
  const int row = table_->rowCount();
  table_->insertRow(row);
  auto* enabled = new QTableWidgetItem;
  enabled->setFlags((enabled->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
  enabled->setCheckState(Qt::Unchecked);
  table_->setItem(row, enabledColumn, enabled);
  table_->setItem(row, nameColumn, new QTableWidgetItem(tr("New cheat")));
  table_->setItem(row, codeColumn, new QTableWidgetItem);
  table_->setCurrentCell(row, codeColumn);
  table_->editItem(table_->item(row, codeColumn));
}

void CheatManagerDialog::importDefinitions()
{
  if (!dialogService_) {
    showValidationMessage(tr("The cheat-list file chooser is unavailable."));
    return;
  }
  const auto selected = dialogService_->chooseCheatImport(this, importDirectory_);
  if (!selected) {
    return;
  }
  if (selected->has_parent_path()) {
    importDirectory_ = selected->parent_path();
  }
  const auto imported = cheats::importCheatList(system_, *selected);
  if (!imported.status) {
    showValidationMessage(QString::fromStdString(imported.status.message));
    return;
  }

  std::vector<cheats::CheatDefinition> additions;
  std::size_t duplicates = 0U;
  additions.reserve(imported.configuration.entries.size());
  const auto containsCode = [this, &additions](const std::string& normalized) {
    for (int row = 0; row < table_->rowCount(); ++row) {
      const auto* item = table_->item(row, codeColumn);
      if (item == nullptr) {
        continue;
      }
      const auto parsed = cheats::parseCheatCode(
        system_, item->text().trimmed().toStdString());
      if (parsed.status && parsed.normalizedCode == normalized) {
        return true;
      }
    }
    return std::ranges::any_of(additions, [&normalized](const auto& entry) {
      return entry.code == normalized;
    });
  };
  for (const auto& entry : imported.configuration.entries) {
    if (containsCode(entry.code)) {
      ++duplicates;
    } else {
      additions.push_back(entry);
    }
  }
  const auto existingCount = static_cast<std::size_t>(table_->rowCount());
  if (existingCount > cheats::maximumCheatDefinitions ||
      additions.size() > cheats::maximumCheatDefinitions - existingCount) {
    showValidationMessage(tr(
      "Import would exceed the 150-definition per-game limit; nothing was added."));
    return;
  }
  for (const auto& entry : additions) {
    appendDefinitionRow(*table_, entry);
  }
  const auto format = imported.format == cheats::CheatListFormat::retroArch
    ? tr("RetroArch") : tr("plain-text");
  showValidationMessage(tr(
    "Imported %1 disabled %2 entries; skipped %3 duplicate codes. Review them, "
    "then explicitly enable and Apply any desired cheat.")
      .arg(static_cast<qulonglong>(additions.size()))
      .arg(format)
      .arg(static_cast<qulonglong>(duplicates)));
}

void CheatManagerDialog::removeSelectedDefinition()
{
  const auto selected = table_->selectionModel()->selectedRows();
  if (selected.empty()) {
    return;
  }
  table_->removeRow(selected.front().row());
}

void CheatManagerDialog::requestNewSearch()
{
  static_cast<void>(requestSearchSnapshot(true));
}

void CheatManagerDialog::requestSearchFilter()
{
  if (!ramSearch_.active()) {
    showValidationMessage(tr("Start a new RAM search before filtering."));
    return;
  }
  pendingComparison_ = static_cast<DebugRamComparison>(
    searchComparison_->currentData().toUInt());
  pendingFormat_ = searchSigned_->isChecked()
    ? DebugValueFormat::signedInteger : DebugValueFormat::unsignedInteger;
  pendingValue_ = 0;
  if (comparisonUsesValue(pendingComparison_) &&
      !parseSearchValue(searchValue_->text(), searchProfile(system_).width,
        pendingFormat_, pendingValue_)) {
    showValidationMessage(tr("The RAM search value is outside the selected type."));
    return;
  }
  static_cast<void>(requestSearchSnapshot(false));
}

bool CheatManagerDialog::requestSearchSnapshot(bool begin)
{
  if (pendingSearch_ != PendingSearch::none) {
    return false;
  }
  if (!debugRequestSink_) {
    showValidationMessage(tr("The emulator RAM snapshot service is unavailable."));
    return false;
  }
  CoreDebugRequest request;
  request.type = CoreDebugRequestType::captureSnapshot;
  request.clientToken = cheatSearchClientToken;
  pendingSearch_ = begin ? PendingSearch::begin : PendingSearch::filter;
  searchNewButton_->setEnabled(false);
  searchFilterButton_->setEnabled(false);
  if (!debugRequestSink_(std::move(request))) {
    pendingSearch_ = PendingSearch::none;
    searchNewButton_->setEnabled(true);
    searchFilterButton_->setEnabled(ramSearch_.active());
    showValidationMessage(tr("The emulator debug command queue is unavailable."));
    return false;
  }
  searchCount_->setText(tr("Waiting for a frame-boundary RAM snapshot…"));
  return true;
}

void CheatManagerDialog::resetSearch()
{
  pendingSearch_ = PendingSearch::none;
  ramSearch_.clear();
  searchSnapshot_.reset();
  searchResults_->setRowCount(0);
  searchCount_->setText(tr("No active search."));
  searchNewButton_->setEnabled(true);
  searchFilterButton_->setEnabled(false);
  searchAddButton_->setEnabled(false);
}

void CheatManagerDialog::updateSearchValueControl()
{
  const auto comparison = static_cast<DebugRamComparison>(
    searchComparison_->currentData().toUInt());
  searchValue_->setEnabled(comparisonUsesValue(comparison));
}

void CheatManagerDialog::updateSearchResults()
{
  searchResults_->setRowCount(0);
  if (!ramSearch_.active() || !searchSnapshot_) {
    searchCount_->setText(tr("No active search."));
    searchFilterButton_->setEnabled(false);
    searchAddButton_->setEnabled(false);
    return;
  }
  const auto profile = searchProfile(system_);
  const auto memory = searchMemory(*searchSnapshot_, system_);
  const auto isEligible = [this](const DebugRamCandidate& candidate) {
    return system_ != cheats::CheatSystem::genesis ||
      (candidate.offset & 1U) == 0U;
  };
  const auto eligible = static_cast<std::size_t>(std::ranges::count_if(
    ramSearch_.candidates(), isEligible));
  std::size_t displayed = 0U;
  for (const auto& candidate : ramSearch_.candidates()) {
    if (!isEligible(candidate) ||
        displayed >= static_cast<std::size_t>(maximumDisplayedSearchResults)) {
      continue;
    }
    std::uint32_t current = 0U;
    if (!debugReadValue(memory, candidate.offset,
          profile.width, profile.endian, current)) {
      resetSearch();
      showValidationMessage(tr("A RAM search result exceeded the current snapshot."));
      return;
    }
    const int row = searchResults_->rowCount();
    searchResults_->insertRow(row);
    auto* address = new QTableWidgetItem(
      hexadecimal(profile.displayBase + candidate.offset, profile.addressDigits));
    address->setData(Qt::UserRole, static_cast<qulonglong>(candidate.offset));
    searchResults_->setItem(row, 0, address);
    const int valueDigits = static_cast<int>(profile.width) * 2;
    searchResults_->setItem(row, 1,
      new QTableWidgetItem(hexadecimal(current, valueDigits)));
    searchResults_->setItem(row, 2,
      new QTableWidgetItem(hexadecimal(candidate.previousValue, valueDigits)));
    ++displayed;
  }
  searchCount_->setText(eligible > displayed
    ? tr("%1 candidates; showing the first %2 aligned results.")
        .arg(static_cast<qulonglong>(eligible))
        .arg(static_cast<qulonglong>(displayed))
    : tr("%1 candidates.").arg(static_cast<qulonglong>(eligible)));
  searchFilterButton_->setEnabled(pendingSearch_ == PendingSearch::none);
  searchAddButton_->setEnabled(displayed != 0U);
  if (displayed != 0U) {
    searchResults_->selectRow(0);
  }
}

void CheatManagerDialog::addSelectedSearchResult()
{
  if (!searchSnapshot_ || table_->rowCount() >=
      static_cast<int>(cheats::maximumCheatDefinitions)) {
    showValidationMessage(tr(
      "A search result is unavailable or the 150-definition limit was reached."));
    return;
  }
  const auto selected = searchResults_->selectionModel()->selectedRows();
  if (selected.empty()) {
    showValidationMessage(tr("Select a RAM search result first."));
    return;
  }
  const auto offset = static_cast<std::uint32_t>(
    searchResults_->item(selected.front().row(), 0)
      ->data(Qt::UserRole).toULongLong());
  const auto profile = searchProfile(system_);
  const auto memory = searchMemory(*searchSnapshot_, system_);
  std::uint32_t value = 0U;
  if (!debugReadValue(memory, offset, profile.width, profile.endian, value)) {
    showValidationMessage(tr("The selected RAM result is no longer available."));
    return;
  }
  const auto code = cheats::makeRamCheatCode(system_, offset, value);
  if (!code.status) {
    showValidationMessage(QString::fromStdString(code.status.message));
    return;
  }
  appendDefinitionRow(*table_, {
    .name = QStringLiteral("RAM %1 = %2")
      .arg(hexadecimal(profile.displayBase + offset, profile.addressDigits),
        hexadecimal(value, static_cast<int>(profile.width) * 2))
      .toStdString(),
    .code = code.normalizedCode,
    .enabled = false,
  });
  const int row = table_->rowCount() - 1;
  table_->selectRow(row);
  tabs_->setCurrentIndex(0);
  showValidationMessage(tr(
    "Added a disabled RAM cheat. Explicitly enable it and choose Apply to patch memory."));
}

void CheatManagerDialog::showValidationMessage(const QString& message)
{
  validationLabel_->setText(message);
  validationLabel_->show();
  validationLabel_->setFocus(Qt::OtherFocusReason);
}

} // namespace genplusgx::ui
