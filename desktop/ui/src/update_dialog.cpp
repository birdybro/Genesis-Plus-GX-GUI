#include "genplusgx/ui/update_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace genplusgx::ui {

UpdateDialog::UpdateDialog(QWidget* parent) : QDialog(parent)
{
  setObjectName(QStringLiteral("updateDialog"));
  setWindowTitle(tr("Signed Application Updates"));
  setModal(false);
  resize(620, 420);

  auto* root = new QVBoxLayout(this);
  auto* security = new QGroupBox(tr("Verification and privacy"), this);
  auto* securityLayout = new QVBoxLayout(security);
  trustLabel_ = new QLabel(security);
  trustLabel_->setObjectName(QStringLiteral("updateTrustLabel"));
  trustLabel_->setWordWrap(true);
  trustLabel_->setText(tr(
    "Release metadata must carry a valid Ed25519 signature from the public key "
    "built into this application. Downloads are written atomically and must "
    "match the signed byte length and SHA-256 digest before they can be opened. "
    "The application never silently installs or replaces itself."));
  securityLayout->addWidget(trustLabel_);
  root->addWidget(security);

  automatic_ = new QCheckBox(tr("Check automatically at startup (at most once daily)"),
    this);
  automatic_->setObjectName(QStringLiteral("updateAutomaticCheck"));
  automatic_->setAccessibleDescription(tr(
    "Disabled by default. Checks only this project's public GitHub releases."));
  root->addWidget(automatic_);

  statusLabel_ = new QLabel(this);
  statusLabel_->setObjectName(QStringLiteral("updateStatusLabel"));
  statusLabel_->setWordWrap(true);
  statusLabel_->setTextInteractionFlags(
    Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
  root->addWidget(statusLabel_);
  detailLabel_ = new QLabel(this);
  detailLabel_->setObjectName(QStringLiteral("updateDetailLabel"));
  detailLabel_->setWordWrap(true);
  detailLabel_->setTextInteractionFlags(
    Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
  root->addWidget(detailLabel_);

  auto* actions = new QHBoxLayout;
  checkButton_ = new QPushButton(tr("Check Now"), this);
  checkButton_->setObjectName(QStringLiteral("updateCheckButton"));
  downloadButton_ = new QPushButton(tr("Download Verified Package"), this);
  downloadButton_->setObjectName(QStringLiteral("updateDownloadButton"));
  releasePageButton_ = new QPushButton(tr("Release Page"), this);
  releasePageButton_->setObjectName(QStringLiteral("updateReleasePageButton"));
  openPackageButton_ = new QPushButton(tr("Open Download"), this);
  openPackageButton_->setObjectName(QStringLiteral("updateOpenPackageButton"));
  actions->addWidget(checkButton_);
  actions->addWidget(downloadButton_);
  actions->addWidget(releasePageButton_);
  actions->addWidget(openPackageButton_);
  root->addLayout(actions);
  root->addStretch();

  auto* buttons = new QDialogButtonBox(
    QDialogButtonBox::Apply | QDialogButtonBox::Close, this);
  buttons->setObjectName(QStringLiteral("updateButtonBox"));
  buttons->button(QDialogButtonBox::Apply)->setObjectName(
    QStringLiteral("updateApplyButton"));
  buttons->button(QDialogButtonBox::Close)->setObjectName(
    QStringLiteral("updateCloseButton"));
  root->addWidget(buttons);

  connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
    this, [this] { static_cast<void>(saveSettings()); });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(checkButton_, &QPushButton::clicked, this, [this] {
    if (checkSink_) {
      checkSink_();
    }
  });
  connect(downloadButton_, &QPushButton::clicked, this, [this] {
    if (downloadSink_ && checkResult_ && checkResult_->asset) {
      downloadSink_(*checkResult_->asset);
    }
  });
  connect(releasePageButton_, &QPushButton::clicked, this, [this] {
    if (urlSink_ && checkResult_) {
      static_cast<void>(urlSink_(checkResult_->manifest.releasePage));
    }
  });
  connect(openPackageButton_, &QPushButton::clicked, this, [this] {
    if (fileSink_ && downloadResult_) {
      static_cast<void>(fileSink_(downloadResult_->path));
    }
  });
  refresh();
}

void UpdateDialog::setSettings(updates::Settings settings)
{
  settings_ = std::move(settings);
  automatic_->setChecked(settings_.automaticChecks);
  refresh();
}

void UpdateDialog::setCurrentVersion(std::string version)
{
  currentVersion_ = std::move(version);
  refresh();
}

void UpdateDialog::setSettingsSink(SettingsSink sink) { settingsSink_ = std::move(sink); }
void UpdateDialog::setCheckSink(CheckSink sink) { checkSink_ = std::move(sink); }
void UpdateDialog::setDownloadSink(DownloadSink sink) { downloadSink_ = std::move(sink); }
void UpdateDialog::setUrlSink(UrlSink sink) { urlSink_ = std::move(sink); }
void UpdateDialog::setFileSink(FileSink sink) { fileSink_ = std::move(sink); }

void UpdateDialog::setBusy(bool busy, bool downloading)
{
  busy_ = busy;
  downloading_ = downloading;
  refresh();
}

void UpdateDialog::presentCheck(updates::CheckResult result)
{
  checkResult_ = std::move(result);
  downloadResult_.reset();
  busy_ = false;
  downloading_ = false;
  refresh();
}

void UpdateDialog::presentCheckFailure(const std::string& detail)
{
  checkResult_.reset();
  downloadResult_.reset();
  busy_ = false;
  downloading_ = false;
  statusLabel_->setText(tr("Update verification failed."));
  detailLabel_->setText(QString::fromStdString(detail));
  refresh();
}

void UpdateDialog::presentDownload(updates::DownloadResult result)
{
  downloadResult_ = std::move(result);
  busy_ = false;
  downloading_ = false;
  refresh();
}

void UpdateDialog::presentDownloadFailure(const std::string& detail)
{
  downloadResult_.reset();
  busy_ = false;
  downloading_ = false;
  statusLabel_->setText(tr("The package was not accepted."));
  detailLabel_->setText(QString::fromStdString(detail));
  refresh();
}

bool UpdateDialog::saveSettings()
{
  auto edited = settings_;
  edited.automaticChecks = automatic_->isChecked();
  if (settingsSink_) {
    const auto saved = settingsSink_(edited);
    if (!saved) {
      statusLabel_->setText(tr("Update settings could not be saved."));
      detailLabel_->setText(QString::fromStdString(saved.message));
      return false;
    }
  }
  settings_ = std::move(edited);
  statusLabel_->setText(tr("Update settings saved."));
  return true;
}

void UpdateDialog::refresh()
{
  checkButton_->setEnabled(!busy_);
  downloadButton_->setEnabled(!busy_ && checkResult_ &&
    checkResult_->updateAvailable && checkResult_->asset.has_value());
  releasePageButton_->setEnabled(!busy_ && checkResult_.has_value());
  openPackageButton_->setEnabled(!busy_ && downloadResult_.has_value());
  if (busy_) {
    statusLabel_->setText(downloading_ ? tr("Downloading and verifying package…")
                                      : tr("Downloading and verifying signed manifest…"));
    detailLabel_->setText(tr("No package will be offered unless every verification succeeds."));
  } else if (downloadResult_) {
    statusLabel_->setText(tr("Verified package downloaded."));
    detailLabel_->setText(tr("Saved to: %1\nOpen it to hand installation to your operating system.")
      .arg(QString::fromStdString(downloadResult_->path.string())));
  } else if (checkResult_) {
    statusLabel_->setText(checkResult_->updateAvailable
      ? tr("A verified update is available: %1 (installed: %2)")
          .arg(QString::fromStdString(checkResult_->manifest.version.toString()),
            QString::fromStdString(currentVersion_))
      : tr("Genesis Plus GX GUI %1 is up to date.")
          .arg(QString::fromStdString(currentVersion_)));
    detailLabel_->setText(tr("Signature key: %1\nPublished: %2")
      .arg(QString::fromStdString(checkResult_->manifest.keyId),
        QString::fromStdString(checkResult_->manifest.publishedAt)));
  } else if (statusLabel_->text().isEmpty()) {
    statusLabel_->setText(tr("Installed version: %1")
      .arg(QString::fromStdString(currentVersion_)));
    detailLabel_->setText(tr("Select Check Now to contact the project's GitHub release endpoint."));
  }
}

} // namespace genplusgx::ui
