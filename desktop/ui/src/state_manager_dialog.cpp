#include "genplusgx/ui/state_manager_dialog.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <utility>

namespace genplusgx::ui {
namespace {

QString stateText(StateSlotViewState state)
{
  switch (state) {
    case StateSlotViewState::empty:
      return StateManagerDialog::tr("Empty");
    case StateSlotViewState::available:
      return StateManagerDialog::tr("Available");
    case StateSlotViewState::invalid:
      return StateManagerDialog::tr("Invalid");
  }
  return StateManagerDialog::tr("Unknown");
}

QString timestampText(const StateSlotView& view)
{
  if (view.state != StateSlotViewState::available) {
    return {};
  }
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
    view.timestamp.time_since_epoch()).count();
  return QDateTime::fromMSecsSinceEpoch(milliseconds)
    .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

} // namespace

StateManagerDialog::StateManagerDialog(QWidget* parent)
  : QDialog(parent)
{
  setObjectName(QStringLiteral("stateManagerDialog"));
  setWindowTitle(tr("Save-State Manager"));
  setModal(false);
  resize(860, 520);

  auto* layout = new QVBoxLayout(this);
  auto* explanation = new QLabel(
    tr("Browse slots, attach a short name, and import or export validated "
       "Genesis Plus GX GUI state files."), this);
  explanation->setObjectName(QStringLiteral("stateManagerExplanationLabel"));
  explanation->setWordWrap(true);
  layout->addWidget(explanation);

  table_ = new QTableWidget(10, 7, this);
  table_->setObjectName(QStringLiteral("stateSlotTable"));
  table_->setAccessibleName(tr("Save-state slots"));
  table_->setIconSize(QSize{80, 60});
  table_->setHorizontalHeaderLabels({
    tr("Preview"), tr("Slot"), tr("Name"), tr("Saved"), tr("Frame"),
    tr("Size"), tr("Status")});
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);
  table_->verticalHeader()->setDefaultSectionSize(70);
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
  layout->addWidget(table_, 1);

  auto* nameLayout = new QFormLayout;
  nameEdit_ = new QLineEdit(this);
  nameEdit_->setObjectName(QStringLiteral("stateNameEdit"));
  nameEdit_->setMaxLength(96);
  nameEdit_->setAccessibleName(tr("Selected state name"));
  nameLayout->addRow(tr("State &name:"), nameEdit_);
  layout->addLayout(nameLayout);

  auto* actions = new QDialogButtonBox(this);
  actions->setObjectName(QStringLiteral("stateManagerActionButtons"));
  saveButton_ = actions->addButton(tr("Save / Replace"), QDialogButtonBox::ActionRole);
  loadButton_ = actions->addButton(tr("Load"), QDialogButtonBox::ActionRole);
  importButton_ = actions->addButton(tr("Import…"), QDialogButtonBox::ActionRole);
  exportButton_ = actions->addButton(tr("Export…"), QDialogButtonBox::ActionRole);
  renameButton_ = actions->addButton(tr("Rename"), QDialogButtonBox::ActionRole);
  deleteButton_ = actions->addButton(tr("Delete"), QDialogButtonBox::DestructiveRole);
  auto* closeButton = actions->addButton(QDialogButtonBox::Close);
  saveButton_->setObjectName(QStringLiteral("stateBrowserSaveButton"));
  loadButton_->setObjectName(QStringLiteral("stateBrowserLoadButton"));
  importButton_->setObjectName(QStringLiteral("stateBrowserImportButton"));
  exportButton_->setObjectName(QStringLiteral("stateBrowserExportButton"));
  renameButton_->setObjectName(QStringLiteral("stateBrowserRenameButton"));
  deleteButton_->setObjectName(QStringLiteral("stateBrowserDeleteButton"));
  closeButton->setObjectName(QStringLiteral("stateBrowserCloseButton"));
  layout->addWidget(actions);

  connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
    if (updating_ || table_->currentRow() < 0) {
      return;
    }
    setSelectedSlot(static_cast<std::uint32_t>(table_->currentRow()));
    if (selectionSink_) {
      selectionSink_(selectedSlot_);
    }
  });
  connect(saveButton_, &QPushButton::clicked, this,
    [this] { dispatch(StateUiOperation::save); });
  connect(loadButton_, &QPushButton::clicked, this,
    [this] { dispatch(StateUiOperation::load); });
  connect(importButton_, &QPushButton::clicked, this,
    [this] { dispatch(StateUiOperation::importFile); });
  connect(exportButton_, &QPushButton::clicked, this,
    [this] { dispatch(StateUiOperation::exportFile); });
  connect(renameButton_, &QPushButton::clicked, this,
    [this] { dispatch(StateUiOperation::rename); });
  connect(deleteButton_, &QPushButton::clicked, this,
    [this] { dispatch(StateUiOperation::remove); });
  connect(actions, &QDialogButtonBox::rejected, this, &QDialog::close);

  setViews(views_);
}

void StateManagerDialog::setViews(const std::array<StateSlotView, 10>& views)
{
  views_ = views;
  updating_ = true;
  for (std::size_t row = 0U; row < views_.size(); ++row) {
    const auto& view = views_[row];
    auto* preview = new QTableWidgetItem;
    if (!view.thumbnailPng.empty()) {
      QPixmap thumbnail;
      const auto bytes = QByteArray::fromRawData(
        reinterpret_cast<const char*>(view.thumbnailPng.data()),
        static_cast<qsizetype>(view.thumbnailPng.size()));
      if (thumbnail.loadFromData(bytes, "PNG")) {
        preview->setIcon(QIcon{thumbnail});
      }
    }
    table_->setItem(static_cast<int>(row), 0, preview);
    table_->setItem(static_cast<int>(row), 1,
      new QTableWidgetItem(QString::number(view.slot)));
    table_->setItem(static_cast<int>(row), 2,
      new QTableWidgetItem(QString::fromStdString(view.name)));
    table_->setItem(static_cast<int>(row), 3,
      new QTableWidgetItem(timestampText(view)));
    table_->setItem(static_cast<int>(row), 4,
      new QTableWidgetItem(view.state == StateSlotViewState::available
        ? QString::number(static_cast<qulonglong>(view.emulatedFrameNumber))
        : QString{}));
    table_->setItem(static_cast<int>(row), 5,
      new QTableWidgetItem(view.state == StateSlotViewState::available
        ? tr("%1 KiB").arg(static_cast<qulonglong>(
            (view.payloadBytes + 1023U) / 1024U))
        : QString{}));
    auto* status = new QTableWidgetItem(stateText(view.state));
    status->setToolTip(QString::fromStdString(view.detail));
    table_->setItem(static_cast<int>(row), 6, status);
  }
  table_->selectRow(static_cast<int>(selectedSlot_));
  updating_ = false;
  updateSelection();
}

void StateManagerDialog::setSelectedSlot(std::uint32_t slot)
{
  if (slot >= views_.size()) {
    return;
  }
  selectedSlot_ = slot;
  updating_ = true;
  table_->selectRow(static_cast<int>(slot));
  updating_ = false;
  updateSelection();
}

std::uint32_t StateManagerDialog::selectedSlot() const noexcept
{
  return selectedSlot_;
}

void StateManagerDialog::setSessionReady(bool ready)
{
  sessionReady_ = ready;
  updateButtons();
}

void StateManagerDialog::setBusy(bool busy)
{
  busy_ = busy;
  updateButtons();
}

void StateManagerDialog::setOperationSink(OperationSink sink)
{
  operationSink_ = std::move(sink);
  updateButtons();
}

void StateManagerDialog::setSelectionSink(SelectionSink sink)
{
  selectionSink_ = std::move(sink);
}

void StateManagerDialog::dispatch(StateUiOperation operation)
{
  if (!operationSink_ || busy_ || !sessionReady_) {
    return;
  }
  operationSink_({
    .operation = operation,
    .slot = selectedSlot_,
    .path = {},
    .name = nameEdit_->text().trimmed().toUtf8().toStdString(),
  });
}

void StateManagerDialog::updateSelection()
{
  nameEdit_->setText(QString::fromStdString(views_[selectedSlot_].name));
  updateButtons();
}

void StateManagerDialog::updateButtons()
{
  const bool ready = sessionReady_ && !busy_ && static_cast<bool>(operationSink_);
  const auto state = views_[selectedSlot_].state;
  saveButton_->setEnabled(ready);
  importButton_->setEnabled(ready);
  loadButton_->setEnabled(ready && state == StateSlotViewState::available);
  exportButton_->setEnabled(ready && state == StateSlotViewState::available);
  renameButton_->setEnabled(ready && state == StateSlotViewState::available);
  deleteButton_->setEnabled(ready && state != StateSlotViewState::empty);
  nameEdit_->setEnabled(ready);
  table_->setEnabled(!busy_);
}

} // namespace genplusgx::ui
