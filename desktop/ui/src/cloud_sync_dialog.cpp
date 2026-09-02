#include "genplusgx/ui/cloud_sync_dialog.h"

#include "genplusgx/ui/dialog_service.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <utility>

namespace genplusgx::ui {
namespace {

QString actionName(cloud::Action action)
{
  switch (action) {
    case cloud::Action::unchanged:
      return CloudSyncDialog::tr("Unchanged");
    case cloud::Action::upload:
      return CloudSyncDialog::tr("Uploaded");
    case cloud::Action::download:
      return CloudSyncDialog::tr("Downloaded");
    case cloud::Action::conflict:
      return CloudSyncDialog::tr("Conflict copy");
  }
  return CloudSyncDialog::tr("Unknown");
}

} // namespace

CloudSyncDialog::CloudSyncDialog(QWidget* parent) : QDialog(parent)
{
  setObjectName(QStringLiteral("cloudSyncDialog"));
  setWindowTitle(tr("Cloud Synchronization"));
  setModal(false);
  resize(760, 620);

  auto* root = new QVBoxLayout(this);
  auto* notice = new QLabel(tr(
    "Synchronizes only save RAM and wrapped save states with your HTTPS "
    "WebDAV account. ROMs, BIOS files, settings, logs, and credentials are "
    "never uploaded. Conflicting remote data is preserved as a local copy."), this);
  notice->setObjectName(QStringLiteral("cloudPrivacyLabel"));
  notice->setWordWrap(true);
  root->addWidget(notice);

  auto* account = new QGroupBox(tr("WebDAV account"), this);
  account->setObjectName(QStringLiteral("cloudAccountGroup"));
  auto* form = new QFormLayout(account);
  enabled_ = new QCheckBox(tr("Enable cloud synchronization"), account);
  enabled_->setObjectName(QStringLiteral("cloudEnabledCheck"));
  form->addRow(enabled_);
  endpoint_ = new QLineEdit(account);
  endpoint_->setObjectName(QStringLiteral("cloudEndpointEdit"));
  endpoint_->setMaxLength(2'048);
  endpoint_->setPlaceholderText(
    tr("https://cloud.example.com/remote.php/dav/files/account"));
  endpoint_->setAccessibleName(tr("HTTPS WebDAV collection URL"));
  form->addRow(tr("Server URL:"), endpoint_);
  username_ = new QLineEdit(account);
  username_->setObjectName(QStringLiteral("cloudUsernameEdit"));
  username_->setMaxLength(128);
  username_->setAccessibleName(tr("WebDAV username"));
  form->addRow(tr("Username:"), username_);
  password_ = new QLineEdit(account);
  password_->setObjectName(QStringLiteral("cloudPasswordEdit"));
  password_->setEchoMode(QLineEdit::Password);
  password_->setMaxLength(1'024);
  password_->setAccessibleName(tr("WebDAV password or application password"));
  form->addRow(tr("Password:"), password_);
  remoteDirectory_ = new QLineEdit(account);
  remoteDirectory_->setObjectName(QStringLiteral("cloudRemoteDirectoryEdit"));
  remoteDirectory_->setMaxLength(64);
  remoteDirectory_->setAccessibleName(tr("Remote synchronization directory"));
  form->addRow(tr("Remote folder:"), remoteDirectory_);
  root->addWidget(account);

  auto* content = new QGroupBox(tr("Content and automation"), this);
  content->setObjectName(QStringLiteral("cloudContentGroup"));
  auto* contentLayout = new QVBoxLayout(content);
  saves_ = new QCheckBox(tr("Save RAM (cartridge SRAM and Sega CD BRAM)"), content);
  saves_->setObjectName(QStringLiteral("cloudSavesCheck"));
  states_ = new QCheckBox(tr("Save states"), content);
  states_->setObjectName(QStringLiteral("cloudStatesCheck"));
  startup_ = new QCheckBox(tr("Synchronize after application startup"), content);
  startup_->setObjectName(QStringLiteral("cloudStartupCheck"));
  gameClose_ = new QCheckBox(tr("Synchronize after a game closes"), content);
  gameClose_->setObjectName(QStringLiteral("cloudGameCloseCheck"));
  contentLayout->addWidget(saves_);
  contentLayout->addWidget(states_);
  contentLayout->addWidget(startup_);
  contentLayout->addWidget(gameClose_);
  root->addWidget(content);

  status_ = new QLabel(tr("Cloud synchronization is disabled."), this);
  status_->setObjectName(QStringLiteral("cloudStatusLabel"));
  status_->setAccessibleName(tr("Cloud synchronization status"));
  status_->setWordWrap(true);
  root->addWidget(status_);

  results_ = new QTableWidget(this);
  results_->setObjectName(QStringLiteral("cloudResultsTable"));
  results_->setColumnCount(3);
  results_->setHorizontalHeaderLabels({tr("File"), tr("Action"), tr("Details")});
  results_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  results_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  results_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  results_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  results_->setSelectionBehavior(QAbstractItemView::SelectRows);
  results_->setAlternatingRowColors(true);
  root->addWidget(results_, 1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  buttons->setObjectName(QStringLiteral("cloudButtonBox"));
  apply_ = buttons->addButton(tr("Apply"), QDialogButtonBox::ApplyRole);
  apply_->setObjectName(QStringLiteral("cloudApplyButton"));
  remember_ = buttons->addButton(tr("Remember Password"),
    QDialogButtonBox::ActionRole);
  remember_->setObjectName(QStringLiteral("cloudRememberPasswordButton"));
  forget_ = buttons->addButton(tr("Forget Password"),
    QDialogButtonBox::ActionRole);
  forget_->setObjectName(QStringLiteral("cloudForgetPasswordButton"));
  sync_ = buttons->addButton(tr("Sync Now"), QDialogButtonBox::ActionRole);
  sync_->setObjectName(QStringLiteral("cloudSyncNowButton"));
  root->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
  connect(apply_, &QPushButton::clicked, this, &CloudSyncDialog::apply);
  connect(remember_, &QPushButton::clicked,
    this, &CloudSyncDialog::rememberPassword);
  connect(forget_, &QPushButton::clicked,
    this, &CloudSyncDialog::forgetPassword);
  connect(sync_, &QPushButton::clicked,
    this, &CloudSyncDialog::synchronizeNow);
  connect(enabled_, &QCheckBox::toggled,
    this, &CloudSyncDialog::refreshControls);
  setSettings(cloud::defaultSettings());
}

void CloudSyncDialog::setSettings(cloud::Settings settings)
{
  settings_ = std::move(settings);
  enabled_->setChecked(settings_.enabled);
  endpoint_->setText(QString::fromStdString(settings_.endpoint));
  username_->setText(QString::fromStdString(settings_.username));
  remoteDirectory_->setText(QString::fromStdString(settings_.remoteDirectory));
  saves_->setChecked(settings_.syncSaves);
  states_->setChecked(settings_.syncStates);
  startup_->setChecked(settings_.syncOnStartup);
  gameClose_->setChecked(settings_.syncOnGameClose);
  password_->clear();
  refreshControls();
}

void CloudSyncDialog::setSettingsSink(SettingsSink sink)
{
  settingsSink_ = std::move(sink);
  refreshControls();
}

void CloudSyncDialog::setPasswordSink(PasswordSink sink)
{
  passwordSink_ = std::move(sink);
  refreshControls();
}

void CloudSyncDialog::setForgetSink(AccountSink sink)
{
  forgetSink_ = std::move(sink);
  refreshControls();
}

void CloudSyncDialog::setSyncSink(SyncSink sink)
{
  syncSink_ = std::move(sink);
  refreshControls();
}

void CloudSyncDialog::setGameActive(bool active)
{
  gameActive_ = active;
  refreshControls();
}

void CloudSyncDialog::setBusy(bool busy)
{
  busy_ = busy;
  if (busy_) {
    status_->setText(tr("Synchronizing…"));
  }
  refreshControls();
}

cloud::Settings CloudSyncDialog::stagedSettings() const
{
  return {
    .enabled = enabled_->isChecked(),
    .syncSaves = saves_->isChecked(),
    .syncStates = states_->isChecked(),
    .syncOnStartup = startup_->isChecked(),
    .syncOnGameClose = gameClose_->isChecked(),
    .endpoint = endpoint_->text().trimmed().toStdString(),
    .username = username_->text().trimmed().toStdString(),
    .remoteDirectory = remoteDirectory_->text().trimmed().toStdString(),
  };
}

bool CloudSyncDialog::apply()
{
  const auto candidate = stagedSettings();
  const auto validation = cloud::validateSettings(candidate);
  if (!validation) {
    showError(validation.message);
    return false;
  }
  if (settingsSink_) {
    const auto saved = settingsSink_(candidate);
    if (!saved) {
      showError(saved.message);
      return false;
    }
  }
  settings_ = candidate;
  status_->setText(settings_.enabled
    ? tr("Settings saved. Synchronization has not started.")
    : tr("Cloud synchronization is disabled."));
  refreshControls();
  return true;
}

void CloudSyncDialog::rememberPassword()
{
  if (!apply() || !settings_.enabled || password_->text().isEmpty() ||
      !passwordSink_) {
    if (password_->text().isEmpty()) {
      showError("Enter a password or application password first.");
    }
    return;
  }
  auto secret = password_->text().toStdString();
  password_->clear();
  passwordSink_(settings_.endpoint, settings_.username, std::move(secret));
  status_->setText(tr("Saving the password in the operating-system credential store…"));
}

void CloudSyncDialog::forgetPassword()
{
  if (!apply() || !forgetSink_) {
    return;
  }
  password_->clear();
  forgetSink_(settings_.endpoint, settings_.username);
  status_->setText(tr("Removing the saved password…"));
}

void CloudSyncDialog::synchronizeNow()
{
  if (!apply() || !settings_.enabled || gameActive_ || busy_ || !syncSink_) {
    if (gameActive_) {
      showError("Close the active game before synchronizing saves and states.");
    }
    return;
  }
  auto secret = password_->text().toStdString();
  password_->clear();
  syncSink_(std::move(secret));
}

void CloudSyncDialog::showResult(const cloud::SyncResult& result)
{
  busy_ = false;
  if (!result.status) {
    showError(result.status.message);
    return;
  }
  status_->setText(tr(
    "Complete: %1 uploaded, %2 downloaded, %3 conflict copies, %4 unchanged.")
      .arg(result.summary.uploaded).arg(result.summary.downloaded)
      .arg(result.summary.conflicts).arg(result.summary.unchanged));
  results_->setRowCount(static_cast<int>(result.summary.items.size()));
  for (int row = 0; row < results_->rowCount(); ++row) {
    const auto& item = result.summary.items[static_cast<std::size_t>(row)];
    results_->setItem(row, 0,
      new QTableWidgetItem(QString::fromStdString(item.key)));
    results_->setItem(row, 1, new QTableWidgetItem(actionName(item.action)));
    results_->setItem(row, 2, new QTableWidgetItem(item.conflictPath.empty()
      ? QString{} : pathToQString(item.conflictPath)));
  }
  refreshControls();
}

void CloudSyncDialog::showError(const std::string& detail)
{
  busy_ = false;
  status_->setText(QString::fromStdString(detail));
  refreshControls();
}

void CloudSyncDialog::showStatus(const std::string& detail)
{
  status_->setText(QString::fromStdString(detail));
  refreshControls();
}

void CloudSyncDialog::refreshControls()
{
  const bool enabled = enabled_->isChecked();
  endpoint_->setEnabled(enabled && !busy_);
  username_->setEnabled(enabled && !busy_);
  password_->setEnabled(enabled && !busy_);
  remoteDirectory_->setEnabled(enabled && !busy_);
  saves_->setEnabled(enabled && !busy_);
  states_->setEnabled(enabled && !busy_);
  startup_->setEnabled(enabled && !busy_);
  gameClose_->setEnabled(enabled && !busy_);
  apply_->setEnabled(!busy_ && static_cast<bool>(settingsSink_));
  remember_->setEnabled(enabled && !busy_ && static_cast<bool>(passwordSink_));
  forget_->setEnabled(!busy_ && static_cast<bool>(forgetSink_) &&
    !endpoint_->text().trimmed().isEmpty() &&
    !username_->text().trimmed().isEmpty());
  sync_->setEnabled(enabled && !busy_ && !gameActive_ &&
    static_cast<bool>(syncSink_));
  sync_->setToolTip(gameActive_
    ? tr("Close the active game before synchronizing.") : QString{});
}

} // namespace genplusgx::ui
