#include "genplusgx/ui/appearance_settings_dialog.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace genplusgx::ui {

AppearanceSettingsDialog::AppearanceSettingsDialog(
  settings::AppearanceSettings current, QWidget* parent)
    : QDialog(parent)
{
  setObjectName(QStringLiteral("appearanceSettingsDialog"));
  setWindowTitle(tr("Appearance Settings"));
  setModal(false);
  setMinimumSize(480, 300);

  auto* root = new QVBoxLayout(this);
  auto* introduction =
    new QLabel(tr("Choose how Genesis Plus GX GUI follows your desktop appearance. "
                  "Changes apply to every open window."),
      this);
  introduction->setObjectName(QStringLiteral("appearanceIntroductionLabel"));
  introduction->setAccessibleName(tr("Appearance settings description"));
  introduction->setWordWrap(true);
  root->addWidget(introduction);

  auto* group = new QGroupBox(tr("Theme"), this);
  group->setObjectName(QStringLiteral("themeGroup"));
  auto* form = new QFormLayout(group);
  theme_ = new QComboBox(group);
  theme_->setObjectName(QStringLiteral("themeModeCombo"));
  theme_->setAccessibleName(tr("Application theme"));
  theme_->setAccessibleDescription(
    tr("Use the operating-system colors, or force a light or dark palette."));
  theme_->addItem(tr("System default"), static_cast<int>(settings::ThemeMode::system));
  theme_->addItem(tr("Light"), static_cast<int>(settings::ThemeMode::light));
  theme_->addItem(tr("Dark"), static_cast<int>(settings::ThemeMode::dark));
  auto* themeLabel = new QLabel(tr("&Application theme:"), group);
  themeLabel->setObjectName(QStringLiteral("themeModeLabel"));
  themeLabel->setBuddy(theme_);
  form->addRow(themeLabel, theme_);
  root->addWidget(group);

  auto* developerGroup = new QGroupBox(tr("Developer features"), this);
  developerGroup->setObjectName(QStringLiteral("developerFeaturesGroup"));
  auto* developerLayout = new QVBoxLayout(developerGroup);
  developerTools_ = new QCheckBox(
    tr("Enable hidden emulator &debug tools"), developerGroup);
  developerTools_->setObjectName(QStringLiteral("developerToolsEnabledCheck"));
  developerTools_->setAccessibleDescription(tr(
    "Shows advanced CPU, memory, video, sound, input, and analysis tools. "
    "These controls can alter the running machine and are intended for developers."));
  developerLayout->addWidget(developerTools_);
  auto* warning = new QLabel(tr(
    "Debug tools are hidden by default. Memory and register edits are available "
    "only while emulation is paused."), developerGroup);
  warning->setObjectName(QStringLiteral("developerToolsWarningLabel"));
  warning->setWordWrap(true);
  developerLayout->addWidget(warning);
  root->addWidget(developerGroup);

  auto* scaling = new QLabel(
    tr("Interface size follows the operating system's display scaling, including "
       "fractional scaling and Retina displays. Emulator pixels remain controlled "
       "by Video settings."),
    this);
  scaling->setObjectName(QStringLiteral("highDpiInformationLabel"));
  scaling->setAccessibleName(tr("High DPI scaling information"));
  scaling->setWordWrap(true);
  root->addWidget(scaling);

  validation_ = new QLabel(this);
  validation_->setObjectName(QStringLiteral("appearanceValidationLabel"));
  validation_->setAccessibleName(tr("Appearance settings validation message"));
  validation_->setWordWrap(true);
  validation_->hide();
  root->addWidget(validation_);
  root->addStretch();

  auto* buttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                           QDialogButtonBox::Apply | QDialogButtonBox::RestoreDefaults,
      Qt::Horizontal,
      this);
  buttons->setObjectName(QStringLiteral("appearanceSettingsButtonBox"));
  auto* ok = buttons->button(QDialogButtonBox::Ok);
  auto* cancel = buttons->button(QDialogButtonBox::Cancel);
  auto* applyButton = buttons->button(QDialogButtonBox::Apply);
  auto* restore = buttons->button(QDialogButtonBox::RestoreDefaults);
  ok->setObjectName(QStringLiteral("okAppearanceSettingsButton"));
  cancel->setObjectName(QStringLiteral("cancelAppearanceSettingsButton"));
  applyButton->setObjectName(QStringLiteral("applyAppearanceSettingsButton"));
  restore->setObjectName(QStringLiteral("restoreAppearanceDefaultsButton"));
  connect(
    applyButton, &QPushButton::clicked, this, [this] { static_cast<void>(apply()); });
  connect(
    restore, &QPushButton::clicked, this, &AppearanceSettingsDialog::restoreDefaults);
  connect(ok, &QPushButton::clicked, this, [this] {
    if (apply()) {
      accept();
    }
  });
  connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
  root->addWidget(buttons);

  QWidget::setTabOrder(theme_, developerTools_);
  QWidget::setTabOrder(developerTools_, restore);
  QWidget::setTabOrder(restore, applyButton);
  QWidget::setTabOrder(applyButton, ok);
  QWidget::setTabOrder(ok, cancel);
  setSettings(current);
}

void AppearanceSettingsDialog::setSettingsSink(SettingsSink sink)
{
  settingsSink_ = std::move(sink);
}

settings::AppearanceSettings AppearanceSettingsDialog::settings() const
{
  return {
    .theme = static_cast<settings::ThemeMode>(theme_->currentData().toInt()),
    .developerToolsEnabled = developerTools_->isChecked(),
  };
}

void AppearanceSettingsDialog::setSettings(const settings::AppearanceSettings& value)
{
  if (!settings::validateAppearanceSettings(value)) {
    return;
  }
  const auto index = theme_->findData(static_cast<int>(value.theme));
  if (index >= 0) {
    theme_->setCurrentIndex(index);
  }
  developerTools_->setChecked(value.developerToolsEnabled);
  validation_->clear();
  validation_->hide();
}

bool AppearanceSettingsDialog::apply()
{
  const auto value = settings();
  if (!settings::validateAppearanceSettings(value)) {
    validation_->setText(tr("Select a valid application theme."));
    validation_->show();
    return false;
  }
  if (settingsSink_) {
    const auto saved = settingsSink_(value);
    if (!saved) {
      validation_->setText(QString::fromStdString(saved.message));
      validation_->show();
      return false;
    }
  }
  validation_->clear();
  validation_->hide();
  return true;
}

void AppearanceSettingsDialog::restoreDefaults()
{
  setSettings(settings::defaultAppearanceSettings());
}

} // namespace genplusgx::ui
