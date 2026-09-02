#include "genplusgx/ui/online_metadata_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <utility>

namespace genplusgx::ui {

OnlineMetadataDialog::OnlineMetadataDialog(QWidget* parent) : QDialog(parent)
{
  setObjectName(QStringLiteral("onlineMetadataDialog"));
  setWindowTitle(tr("Online Metadata and Artwork"));
  setModal(false);
  resize(620, 560);

  auto* root = new QVBoxLayout(this);
  auto* privacy = new QGroupBox(tr("Privacy and licensing"), this);
  auto* privacyLayout = new QVBoxLayout(privacy);
  disclosure_ = new QLabel(privacy);
  disclosure_->setObjectName(QStringLiteral("onlineMetadataDisclosureLabel"));
  disclosure_->setWordWrap(true);
  disclosure_->setTextFormat(Qt::RichText);
  disclosure_->setOpenExternalLinks(true);
  disclosure_->setTextInteractionFlags(Qt::TextBrowserInteraction);
  privacyLayout->addWidget(disclosure_);
  root->addWidget(privacy);

  auto* options = new QGroupBox(tr("Provider"), this);
  auto* form = new QFormLayout(options);
  enabled_ = new QCheckBox(tr("Enable online metadata lookup"), options);
  enabled_->setObjectName(QStringLiteral("onlineMetadataEnabledCheck"));
  automatic_ = new QCheckBox(
    tr("Automatically look up newly scanned games"), options);
  automatic_->setObjectName(QStringLiteral("onlineMetadataAutomaticCheck"));
  artwork_ = new QCheckBox(
    tr("Download artwork only when an approved license is declared"), options);
  artwork_->setObjectName(QStringLiteral("onlineMetadataArtworkCheck"));
  provider_ = new QComboBox(options);
  provider_->setObjectName(QStringLiteral("onlineMetadataProviderCombo"));
  provider_->addItem(tr("Retronian GameDB (Genesis metadata)"),
    static_cast<int>(library::OnlineMetadataProvider::retronian));
  provider_->addItem(tr("Licensed Manifest API v1"),
    static_cast<int>(library::OnlineMetadataProvider::licensedManifest));
  endpoint_ = new QLineEdit(options);
  endpoint_->setObjectName(QStringLiteral("onlineMetadataEndpointEdit"));
  endpoint_->setAccessibleName(tr("Metadata provider HTTPS endpoint"));
  language_ = new QLineEdit(options);
  language_->setObjectName(QStringLiteral("onlineMetadataLanguageEdit"));
  language_->setMaxLength(2);
  language_->setPlaceholderText(QStringLiteral("en"));
  region_ = new QLineEdit(options);
  region_->setObjectName(QStringLiteral("onlineMetadataRegionEdit"));
  region_->setMaxLength(2);
  region_->setPlaceholderText(tr("Optional, for example us"));
  cacheSize_ = new QSpinBox(options);
  cacheSize_->setObjectName(QStringLiteral("onlineMetadataCacheSpin"));
  cacheSize_->setRange(16, 2'048);
  cacheSize_->setSuffix(tr(" MiB"));
  form->addRow(enabled_);
  form->addRow(tr("Service:"), provider_);
  form->addRow(tr("HTTPS endpoint:"), endpoint_);
  form->addRow(tr("Preferred language:"), language_);
  form->addRow(tr("Preferred region:"), region_);
  form->addRow(tr("Cache limit:"), cacheSize_);
  form->addRow(automatic_);
  form->addRow(artwork_);
  root->addWidget(options);

  errorLabel_ = new QLabel(this);
  errorLabel_->setObjectName(QStringLiteral("onlineMetadataErrorLabel"));
  errorLabel_->setWordWrap(true);
  errorLabel_->setStyleSheet(QStringLiteral("color: palette(highlight);"));
  errorLabel_->setVisible(false);
  root->addWidget(errorLabel_);
  root->addStretch();

  buttons_ = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel |
      QDialogButtonBox::RestoreDefaults,
    this);
  buttons_->setObjectName(QStringLiteral("onlineMetadataButtonBox"));
  restoreDefaults_ = buttons_->button(QDialogButtonBox::RestoreDefaults);
  restoreDefaults_->setObjectName(
    QStringLiteral("onlineMetadataRestoreDefaultsButton"));
  root->addWidget(buttons_);

  connect(enabled_, &QCheckBox::toggled,
    this, &OnlineMetadataDialog::updateEnabledState);
  connect(provider_, &QComboBox::currentIndexChanged, this, [this] {
    if (static_cast<library::OnlineMetadataProvider>(provider_->currentData().toInt()) ==
        library::OnlineMetadataProvider::retronian &&
        (endpoint_->text().isEmpty() ||
         endpoint_->text().contains(QStringLiteral("retronian")))) {
      endpoint_->setText(QStringLiteral("https://gamedb.retronian.com"));
    }
    updateEnabledState();
  });
  connect(buttons_->button(QDialogButtonBox::Apply), &QPushButton::clicked,
    this, [this] { static_cast<void>(applySettings()); });
  connect(buttons_, &QDialogButtonBox::accepted, this, [this] {
    if (applySettings()) {
      accept();
    }
  });
  connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(restoreDefaults_, &QPushButton::clicked, this, [this] {
    settings_ = library::defaultOnlineMetadataSettings();
    loadSettings();
  });
  loadSettings();
}

void OnlineMetadataDialog::setSettings(
  library::OnlineMetadataSettings settings)
{
  settings_ = std::move(settings);
  loadSettings();
}

void OnlineMetadataDialog::setSettingsSink(SettingsSink sink)
{
  settingsSink_ = std::move(sink);
}

void OnlineMetadataDialog::loadSettings()
{
  enabled_->setChecked(settings_.enabled);
  automatic_->setChecked(settings_.automaticLookup);
  artwork_->setChecked(settings_.downloadArtwork);
  const auto index = provider_->findData(static_cast<int>(settings_.provider));
  provider_->setCurrentIndex(index < 0 ? 0 : index);
  endpoint_->setText(QString::fromStdString(settings_.endpoint));
  language_->setText(QString::fromStdString(settings_.preferredLanguage));
  region_->setText(QString::fromStdString(settings_.preferredRegion));
  cacheSize_->setValue(static_cast<int>(settings_.cacheMegabytes));
  errorLabel_->clear();
  errorLabel_->setVisible(false);
  updateEnabledState();
}

void OnlineMetadataDialog::updateEnabledState()
{
  const bool enabled = enabled_->isChecked();
  provider_->setEnabled(enabled);
  endpoint_->setEnabled(enabled);
  language_->setEnabled(enabled);
  region_->setEnabled(enabled);
  cacheSize_->setEnabled(enabled);
  automatic_->setEnabled(enabled);
  artwork_->setEnabled(enabled);
  const auto provider = static_cast<library::OnlineMetadataProvider>(
    provider_->currentData().toInt());
  if (provider == library::OnlineMetadataProvider::retronian) {
    disclosure_->setText(tr(
      "Disabled by default. Retronian lookup downloads a static Genesis hash "
      "index and sends no ROM bytes, file paths, or filenames. Matched metadata "
      "is <a href=\"https://creativecommons.org/licenses/by-sa/4.0/\">"
      "CC BY-SA 4.0</a> and is attributed to Retronian GameDB contributors. "
      "Linked thumbnail images are rejected unless they independently declare "
      "an approved license."));
  } else {
    disclosure_->setText(tr(
      "Disabled by default. The custom provider receives the selected game's "
      "system identifier and SHA-256 hash, never ROM bytes or local paths. "
      "Responses and artwork are accepted only with explicit attribution and "
      "an approved Creative Commons or public-domain license."));
  }
}

library::OnlineMetadataSettings OnlineMetadataDialog::editedSettings() const
{
  return {
    .enabled = enabled_->isChecked(),
    .automaticLookup = automatic_->isChecked(),
    .downloadArtwork = artwork_->isChecked(),
    .provider = static_cast<library::OnlineMetadataProvider>(
      provider_->currentData().toInt()),
    .endpoint = endpoint_->text().trimmed().toStdString(),
    .preferredLanguage = language_->text().trimmed().toLower().toStdString(),
    .preferredRegion = region_->text().trimmed().toLower().toStdString(),
    .cacheMegabytes = static_cast<std::uint32_t>(cacheSize_->value()),
  };
}

bool OnlineMetadataDialog::applySettings()
{
  const auto edited = editedSettings();
  const auto validation = library::validateOnlineMetadataSettings(edited);
  if (!validation) {
    errorLabel_->setText(QString::fromStdString(validation.message));
    errorLabel_->setVisible(true);
    return false;
  }
  if (settingsSink_) {
    const auto saved = settingsSink_(edited);
    if (!saved) {
      errorLabel_->setText(QString::fromStdString(saved.message));
      errorLabel_->setVisible(true);
      return false;
    }
  }
  settings_ = edited;
  errorLabel_->clear();
  errorLabel_->setVisible(false);
  return true;
}

} // namespace genplusgx::ui
