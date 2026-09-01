#include "genplusgx/ui/archive_entry_dialog.h"

#include "genplusgx/ui/dialog_service.h"

#include <QAbstractItemView>
#include <QByteArray>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QVariant>

namespace genplusgx::ui {

ArchiveEntryDialog::ArchiveEntryDialog(
  const std::filesystem::path& archivePath,
  const std::vector<ArchivedGameEntry>& entries,
  QWidget* parent)
  : QDialog(parent)
{
  setObjectName(QStringLiteral("archiveEntryDialog"));
  setWindowTitle(tr("Choose Game from Archive"));
  setModal(true);
  resize(560, 360);

  auto* layout = new QVBoxLayout(this);
  auto* explanation = new QLabel(
    tr("%1 contains more than one supported game. Choose the image to open.")
      .arg(pathToQString(archivePath.filename())),
    this);
  explanation->setObjectName(QStringLiteral("archiveEntryExplanation"));
  explanation->setWordWrap(true);
  layout->addWidget(explanation);

  entries_ = new QListWidget(this);
  entries_->setObjectName(QStringLiteral("archiveEntryList"));
  entries_->setAccessibleName(tr("Games in archive"));
  entries_->setSelectionMode(QAbstractItemView::SingleSelection);
  for (const auto& entry : entries) {
    auto* item = new QListWidgetItem(
      tr("%1  (%2 KiB)")
        .arg(QString::fromUtf8(entry.name))
        .arg((entry.uncompressedSize + 1'023U) / 1'024U),
      entries_);
    item->setData(Qt::UserRole,
      QByteArray{entry.name.data(), static_cast<qsizetype>(entry.name.size())});
    item->setToolTip(QString::fromUtf8(entry.name));
  }
  if (entries_->count() > 0) {
    entries_->setCurrentRow(0);
  }
  layout->addWidget(entries_, 1);

  auto* buttons = new QDialogButtonBox(
    QDialogButtonBox::Open | QDialogButtonBox::Cancel, this);
  buttons->setObjectName(QStringLiteral("archiveEntryButtons"));
  if (auto* open = buttons->button(QDialogButtonBox::Open)) {
    open->setObjectName(QStringLiteral("archiveEntryOpenButton"));
    open->setEnabled(entries_->currentItem() != nullptr);
    connect(entries_, &QListWidget::currentItemChanged, this,
      [open](QListWidgetItem* current) { open->setEnabled(current != nullptr); });
  }
  if (auto* cancel = buttons->button(QDialogButtonBox::Cancel)) {
    cancel->setObjectName(QStringLiteral("archiveEntryCancelButton"));
  }
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(entries_, &QListWidget::itemDoubleClicked, this,
    [this](QListWidgetItem*) { accept(); });
  layout->addWidget(buttons);
}

std::optional<std::string> ArchiveEntryDialog::selectedEntry() const
{
  const auto* selected = entries_ == nullptr ? nullptr : entries_->currentItem();
  if (selected == nullptr) {
    return std::nullopt;
  }
  const auto value = selected->data(Qt::UserRole).toByteArray();
  return value.isEmpty()
    ? std::nullopt
    : std::optional<std::string>{value.toStdString()};
}

} // namespace genplusgx::ui
