#include "genplusgx/ui/physical_media_dialog.h"

#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace genplusgx::ui {

PhysicalMediaDialog::PhysicalMediaDialog(QWidget* parent)
  : QDialog(parent)
{
  setObjectName(QStringLiteral("physicalMediaDialog"));
  setWindowTitle(tr("Open Physical Sega CD Disc"));
  setModal(false);
  resize(560, 340);

  auto* layout = new QVBoxLayout(this);
  auto* explanation = new QLabel(tr(
    "Select a local CD/DVD drive containing a Sega CD or Mega CD disc. "
    "The disc is read without modification into a private BIN/CUE snapshot before "
    "emulation starts; proprietary BIOS firmware is still required."), this);
  explanation->setObjectName(QStringLiteral("physicalMediaExplanationLabel"));
  explanation->setWordWrap(true);
  layout->addWidget(explanation);

  driveList_ = new QListWidget(this);
  driveList_->setObjectName(QStringLiteral("physicalMediaDriveList"));
  driveList_->setAccessibleName(tr("Available optical drives"));
  layout->addWidget(driveList_, 1);

  statusLabel_ = new QLabel(tr("Select Refresh to find optical drives."), this);
  statusLabel_->setObjectName(QStringLiteral("physicalMediaStatusLabel"));
  statusLabel_->setWordWrap(true);
  statusLabel_->setTextInteractionFlags(Qt::TextSelectableByKeyboard |
    Qt::TextSelectableByMouse);
  layout->addWidget(statusLabel_);

  progressBar_ = new QProgressBar(this);
  progressBar_->setObjectName(QStringLiteral("physicalMediaProgressBar"));
  progressBar_->setRange(0, 100);
  progressBar_->setValue(0);
  progressBar_->setVisible(false);
  layout->addWidget(progressBar_);

  auto* buttons = new QDialogButtonBox(this);
  buttons->setObjectName(QStringLiteral("physicalMediaButtonBox"));
  refreshButton_ = buttons->addButton(
    tr("&Refresh"), QDialogButtonBox::ActionRole);
  refreshButton_->setObjectName(QStringLiteral("physicalMediaRefreshButton"));
  importButton_ = buttons->addButton(
    tr("&Open Disc"), QDialogButtonBox::AcceptRole);
  importButton_->setObjectName(QStringLiteral("physicalMediaImportButton"));
  cancelButton_ = buttons->addButton(
    tr("&Cancel Import"), QDialogButtonBox::ActionRole);
  cancelButton_->setObjectName(QStringLiteral("physicalMediaCancelButton"));
  closeButton_ = buttons->addButton(QDialogButtonBox::Close);
  closeButton_->setObjectName(QStringLiteral("physicalMediaCloseButton"));
  layout->addWidget(buttons);

  connect(refreshButton_, &QPushButton::clicked,
    this, &PhysicalMediaDialog::beginDiscovery);
  connect(importButton_, &QPushButton::clicked,
    this, &PhysicalMediaDialog::requestImport);
  connect(cancelButton_, &QPushButton::clicked,
    this, &PhysicalMediaDialog::requestCancel);
  connect(closeButton_, &QPushButton::clicked, this, &QDialog::close);
  connect(driveList_, &QListWidget::itemSelectionChanged,
    this, &PhysicalMediaDialog::updateControls);
  connect(driveList_, &QListWidget::itemDoubleClicked,
    this, [this] { requestImport(); });
  updateControls();
}

void PhysicalMediaDialog::setActions(PhysicalMediaDialogActions actions)
{
  actions_ = std::move(actions);
  updateControls();
}

void PhysicalMediaDialog::beginDiscovery()
{
  if (state_ != State::idle || !actions_.discover) {
    return;
  }
  state_ = State::discovering;
  drives_.clear();
  driveList_->clear();
  statusLabel_->setText(tr("Searching for optical drives…"));
  progressBar_->setRange(0, 0);
  progressBar_->setVisible(true);
  updateControls();
  actions_.discover();
}

void PhysicalMediaDialog::setDrives(
  std::vector<platform::PhysicalDrive> drives)
{
  drives.erase(std::remove_if(drives.begin(), drives.end(),
    [](const platform::PhysicalDrive& drive) { return !drive.valid(); }),
    drives.end());
  drives_ = std::move(drives);
  driveList_->clear();
  for (const auto& drive : drives_) {
    auto* item = new QListWidgetItem(
      QString::fromStdString(drive.displayName), driveList_);
    item->setToolTip(QString::fromStdString(drive.id));
  }
  if (!drives_.empty()) {
    driveList_->setCurrentRow(0);
    statusLabel_->setText(tr(
      "Insert a Sega CD or Mega CD disc, select its drive, then choose Open Disc."));
  } else {
    statusLabel_->setText(tr(
      "No optical drives were found. Connect a drive, insert a disc, and refresh."));
  }
  state_ = State::idle;
  progressBar_->setVisible(false);
  progressBar_->setRange(0, 100);
  progressBar_->setValue(0);
  updateControls();
}

void PhysicalMediaDialog::setImportStarted()
{
  state_ = State::importing;
  statusLabel_->setText(tr(
    "Reading the physical disc… Keep the drive connected until import finishes."));
  progressBar_->setRange(0, 100);
  progressBar_->setValue(0);
  progressBar_->setVisible(true);
  updateControls();
}

void PhysicalMediaDialog::setImportProgress(
  std::uint32_t completedSectors, std::uint32_t totalSectors)
{
  if (state_ != State::importing || totalSectors == 0U) {
    return;
  }
  const auto percent = std::min<std::uint64_t>(100U,
    (static_cast<std::uint64_t>(completedSectors) * 100U) / totalSectors);
  progressBar_->setRange(0, 100);
  progressBar_->setValue(static_cast<int>(percent));
  statusLabel_->setText(tr("Reading physical disc… %1% (%2 of %3 sectors)")
    .arg(percent)
    .arg(completedSectors)
    .arg(totalSectors));
}

void PhysicalMediaDialog::setOperationFailed(const std::string& detail)
{
  state_ = State::idle;
  progressBar_->setVisible(false);
  statusLabel_->setText(tr("Physical disc could not be opened: %1")
    .arg(QString::fromStdString(detail)));
  updateControls();
}

void PhysicalMediaDialog::setOperationCancelled()
{
  state_ = State::idle;
  progressBar_->setVisible(false);
  statusLabel_->setText(tr("Physical-disc import cancelled."));
  updateControls();
}

void PhysicalMediaDialog::setImportReady()
{
  state_ = State::idle;
  progressBar_->setValue(100);
  progressBar_->setVisible(false);
  statusLabel_->setText(tr("Physical disc imported; starting emulation…"));
  updateControls();
}

bool PhysicalMediaDialog::busy() const noexcept
{
  return state_ != State::idle;
}

void PhysicalMediaDialog::closeEvent(QCloseEvent* event)
{
  if (state_ == State::importing) {
    requestCancel();
    event->ignore();
    return;
  }
  if (state_ == State::discovering) {
    event->ignore();
    return;
  }
  QDialog::closeEvent(event);
}

void PhysicalMediaDialog::requestImport()
{
  if (state_ != State::idle || !actions_.importDisc) {
    return;
  }
  const auto row = driveList_->currentRow();
  if (row < 0 || static_cast<std::size_t>(row) >= drives_.size()) {
    return;
  }
  state_ = State::importing;
  updateControls();
  actions_.importDisc(drives_[static_cast<std::size_t>(row)].id);
}

void PhysicalMediaDialog::requestCancel()
{
  if (state_ != State::importing || !actions_.cancel) {
    return;
  }
  cancelButton_->setEnabled(false);
  statusLabel_->setText(tr("Cancelling physical-disc import…"));
  actions_.cancel();
}

void PhysicalMediaDialog::updateControls()
{
  const bool idle = state_ == State::idle;
  refreshButton_->setEnabled(idle && static_cast<bool>(actions_.discover));
  importButton_->setEnabled(idle && driveList_->currentRow() >= 0 &&
    static_cast<bool>(actions_.importDisc));
  cancelButton_->setVisible(state_ == State::importing);
  cancelButton_->setEnabled(
    state_ == State::importing && static_cast<bool>(actions_.cancel));
  closeButton_->setEnabled(idle);
  driveList_->setEnabled(idle);
}

} // namespace genplusgx::ui
