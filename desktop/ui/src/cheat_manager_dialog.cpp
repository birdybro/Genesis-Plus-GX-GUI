#include "genplusgx/ui/cheat_manager_dialog.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <algorithm>
#include <utility>

namespace genplusgx::ui {
namespace {

constexpr int enabledColumn = 0;
constexpr int nameColumn = 1;
constexpr int codeColumn = 2;

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

  table_ = new QTableWidget(this);
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
  layout->addWidget(table_, 1);

  auto* rowButtons = new QHBoxLayout;
  auto* addButton = new QPushButton(tr("&Add"), this);
  addButton->setObjectName(QStringLiteral("addCheatButton"));
  auto* removeButton = new QPushButton(tr("&Remove"), this);
  removeButton->setObjectName(QStringLiteral("removeCheatButton"));
  connect(addButton, &QPushButton::clicked, this, &CheatManagerDialog::addDefinition);
  connect(removeButton,
    &QPushButton::clicked,
    this,
    &CheatManagerDialog::removeSelectedDefinition);
  rowButtons->addWidget(addButton);
  rowButtons->addWidget(removeButton);
  rowButtons->addStretch(1);
  layout->addLayout(rowButtons);

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
}

void CheatManagerDialog::setConfigurationSink(ConfigurationSink sink)
{
  configurationSink_ = std::move(sink);
}

void CheatManagerDialog::setConfiguration(cheats::CheatConfiguration configuration)
{
  if (!cheats::validateCheatConfiguration(system_, configuration)) {
    return;
  }
  configuration_ = std::move(configuration);
  populateTable();
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

void CheatManagerDialog::removeSelectedDefinition()
{
  const auto selected = table_->selectionModel()->selectedRows();
  if (selected.empty()) {
    return;
  }
  table_->removeRow(selected.front().row());
}

void CheatManagerDialog::showValidationMessage(const QString& message)
{
  validationLabel_->setText(message);
  validationLabel_->show();
  validationLabel_->setFocus(Qt::OtherFocusReason);
}

} // namespace genplusgx::ui
