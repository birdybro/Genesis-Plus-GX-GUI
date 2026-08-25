#include "genplusgx/ui/bios_settings_dialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>
#include <utility>

namespace genplusgx::ui {
namespace {

std::size_t slotIndex(platform::BiosSlot slot) noexcept
{
  const auto index = static_cast<std::size_t>(slot);
  return index < platform::biosSlotCount ? index : 0U;
}

QString pathString(const std::filesystem::path& path)
{
#if defined(_WIN32)
  return QString::fromStdWString(path.native());
#else
  return QString::fromUtf8(path.native());
#endif
}

std::filesystem::path filesystemPath(const QString& path)
{
#if defined(_WIN32)
  return std::filesystem::path{path.toStdWString()};
#else
  return std::filesystem::path{path.toUtf8().toStdString()};
#endif
}

QString biosObjectName(
  std::string_view prefix,
  const platform::BiosDescriptor& descriptor)
{
  return QStringLiteral("%1_%2").arg(
    QString::fromLatin1(prefix.data(), static_cast<qsizetype>(prefix.size())),
    QString::fromLatin1(
      descriptor.key.data(), static_cast<qsizetype>(descriptor.key.size())));
}

QString fileSizeText(std::uintmax_t bytes)
{
  if (bytes == 0U) {
    return {};
  }
  if ((bytes % 1'024U) == 0U) {
    return QObject::tr("%1 KiB").arg(bytes / 1'024U);
  }
  return QObject::tr("%1 bytes").arg(bytes);
}

} // namespace

BiosSettingsDialog::BiosSettingsDialog(
  platform::BiosSnapshot snapshot,
  QWidget* parent)
  : QDialog(parent), snapshot_(std::move(snapshot))
{
  setObjectName(QStringLiteral("biosSettingsDialog"));
  setWindowTitle(tr("BIOS Settings"));
  setModal(false);
  resize(760, 680);

  auto* root = new QVBoxLayout(this);
  auto* explanation = new QLabel(
    tr("Choose firmware files that you legally obtained. The application never "
       "downloads or bundles proprietary Sega firmware. A checksum identifies "
       "the file but is not an authenticity guarantee."), this);
  explanation->setObjectName(QStringLiteral("biosOwnershipNoticeLabel"));
  explanation->setWordWrap(true);
  root->addWidget(explanation);

  auto* scroll = new QScrollArea(this);
  scroll->setObjectName(QStringLiteral("biosSettingsScrollArea"));
  scroll->setWidgetResizable(true);
  auto* contents = new QWidget(scroll);
  contents->setObjectName(QStringLiteral("biosSettingsContents"));
  auto* list = new QVBoxLayout(contents);

  for (const auto& descriptor : platform::biosDescriptors()) {
    const auto index = slotIndex(descriptor.slot);
    auto* group = new QGroupBox(
      QStringLiteral("%1 — %2").arg(
        QString::fromUtf8(descriptor.displayName),
        tr("expected region: %1").arg(QString::fromUtf8(
          descriptor.expectedRegion))),
      contents);
    group->setObjectName(biosObjectName("biosGroup", descriptor));
    auto* groupLayout = new QVBoxLayout(group);
    auto* pathRow = new QHBoxLayout;
    pathEdits_[index] = new QLineEdit(group);
    pathEdits_[index]->setObjectName(biosObjectName("biosPath", descriptor));
    pathEdits_[index]->setReadOnly(true);
    pathEdits_[index]->setAccessibleName(
      tr("Configured path for %1").arg(QString::fromUtf8(descriptor.displayName)));
    pathRow->addWidget(pathEdits_[index], 1);
    auto* browseButton = new QPushButton(tr("Browse…"), group);
    browseButton->setObjectName(biosObjectName("biosBrowse", descriptor));
    connect(browseButton, &QPushButton::clicked, this,
      [this, slot = descriptor.slot] { browse(slot); });
    pathRow->addWidget(browseButton);
    auto* clearButton = new QPushButton(tr("Clear"), group);
    clearButton->setObjectName(biosObjectName("biosClear", descriptor));
    connect(clearButton, &QPushButton::clicked, this,
      [this, slot = descriptor.slot] { clear(slot); });
    pathRow->addWidget(clearButton);
    groupLayout->addLayout(pathRow);

    statusLabels_[index] = new QLabel(group);
    statusLabels_[index]->setObjectName(biosObjectName("biosStatus", descriptor));
    statusLabels_[index]->setWordWrap(true);
    groupLayout->addWidget(statusLabels_[index]);
    detailLabels_[index] = new QLabel(group);
    detailLabels_[index]->setObjectName(biosObjectName("biosDetails", descriptor));
    detailLabels_[index]->setWordWrap(true);
    groupLayout->addWidget(detailLabels_[index]);

    auto* checksumForm = new QFormLayout;
    checksumEdits_[index] = new QLineEdit(group);
    checksumEdits_[index]->setObjectName(biosObjectName("biosChecksum", descriptor));
    checksumEdits_[index]->setReadOnly(true);
    checksumEdits_[index]->setAccessibleName(
      tr("SHA-256 checksum for %1").arg(QString::fromUtf8(descriptor.displayName)));
    checksumForm->addRow(tr("SHA-256:"), checksumEdits_[index]);
    groupLayout->addLayout(checksumForm);
    list->addWidget(group);
  }
  list->addStretch();
  scroll->setWidget(contents);
  root->addWidget(scroll, 1);

  auto* buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
      QDialogButtonBox::Apply | QDialogButtonBox::RestoreDefaults,
    Qt::Horizontal, this);
  buttons->setObjectName(QStringLiteral("biosSettingsButtonBox"));
  buttons->button(QDialogButtonBox::Ok)->setObjectName(
    QStringLiteral("okBiosSettingsButton"));
  buttons->button(QDialogButtonBox::Cancel)->setObjectName(
    QStringLiteral("cancelBiosSettingsButton"));
  buttons->button(QDialogButtonBox::Apply)->setObjectName(
    QStringLiteral("applyBiosSettingsButton"));
  buttons->button(QDialogButtonBox::RestoreDefaults)->setObjectName(
    QStringLiteral("restoreBiosDefaultsButton"));
  connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
    this, &BiosSettingsDialog::apply);
  connect(buttons->button(QDialogButtonBox::RestoreDefaults),
    &QPushButton::clicked, this, &BiosSettingsDialog::restoreDefaults);
  connect(buttons, &QDialogButtonBox::accepted, this, [this] {
    apply();
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);
  setSnapshot(snapshot_);
}

void BiosSettingsDialog::setConfigurationSink(ConfigurationSink sink)
{
  configurationSink_ = std::move(sink);
}

void BiosSettingsDialog::setFilePicker(FilePicker picker)
{
  filePicker_ = std::move(picker);
}

void BiosSettingsDialog::setSnapshot(const platform::BiosSnapshot& snapshot)
{
  snapshot_ = snapshot;
  for (const auto& descriptor : platform::biosDescriptors()) {
    refresh(descriptor.slot);
  }
}

const platform::BiosSnapshot& BiosSettingsDialog::snapshot() const noexcept
{
  return snapshot_;
}

void BiosSettingsDialog::browse(platform::BiosSlot slot)
{
  const auto& current = snapshot_.configuration.path(slot);
  std::optional<std::filesystem::path> selected;
  if (filePicker_) {
    selected = filePicker_(slot, current);
  } else {
    const auto filename = QFileDialog::getOpenFileName(
      this, tr("Choose firmware file"), pathString(current),
      tr("Firmware images (*.bin *.rom *.bios);;All files (*)"));
    if (!filename.isEmpty()) {
      selected = filesystemPath(filename);
    }
  }
  if (!selected) {
    return;
  }
  snapshot_.configuration.setPath(slot, std::move(*selected));
  snapshot_.validation[slotIndex(slot)] = platform::validateBios(
    slot, snapshot_.configuration.path(slot));
  refresh(slot);
}

void BiosSettingsDialog::clear(platform::BiosSlot slot)
{
  snapshot_.configuration.setPath(slot, {});
  snapshot_.validation[slotIndex(slot)] = platform::validateBios(slot, {});
  refresh(slot);
}

void BiosSettingsDialog::refresh(platform::BiosSlot slot)
{
  const auto index = slotIndex(slot);
  const auto& validation = snapshot_.validation[index];
  pathEdits_[index]->setText(pathString(snapshot_.configuration.path(slot)));
  checksumEdits_[index]->setText(QString::fromStdString(validation.sha256));

  QString status;
  switch (validation.state) {
    case platform::BiosValidationState::notConfigured:
      status = tr("Not configured");
      break;
    case platform::BiosValidationState::missing:
      status = tr("Missing");
      break;
    case platform::BiosValidationState::notRegularFile:
      status = tr("Not a file");
      break;
    case platform::BiosValidationState::unreadable:
      status = tr("Unreadable");
      break;
    case platform::BiosValidationState::pathTooLong:
      status = tr("Path too long");
      break;
    case platform::BiosValidationState::invalidSize:
      status = tr("Invalid size");
      break;
    case platform::BiosValidationState::invalidContent:
      status = tr("Invalid content");
      break;
    case platform::BiosValidationState::valid:
      status = tr("Valid");
      break;
  }
  statusLabels_[index]->setText(
    tr("Status: %1 — %2").arg(status, QString::fromStdString(validation.message)));
  statusLabels_[index]->setProperty("biosValid", validation.valid());
  const auto size = fileSizeText(validation.fileSize);
  detailLabels_[index]->setText(validation.detectedType.empty()
    ? (size.isEmpty() ? tr("No firmware details available.") : tr("Size: %1").arg(size))
    : tr("Detected type: %1 · Size: %2").arg(
        QString::fromStdString(validation.detectedType), size));
}

void BiosSettingsDialog::apply()
{
  if (configurationSink_) {
    configurationSink_(snapshot_.configuration);
  }
}

void BiosSettingsDialog::restoreDefaults()
{
  snapshot_.configuration = {};
  for (const auto& descriptor : platform::biosDescriptors()) {
    snapshot_.validation[slotIndex(descriptor.slot)] =
      platform::validateBios(descriptor.slot, {});
    refresh(descriptor.slot);
  }
}

} // namespace genplusgx::ui
