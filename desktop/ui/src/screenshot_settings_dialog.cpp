#include "genplusgx/ui/screenshot_settings_dialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace genplusgx::ui {

ScreenshotSettingsDialog::ScreenshotSettingsDialog(settings::ScreenshotSettings current,
  std::filesystem::path defaultDirectory,
  std::shared_ptr<DialogService> dialogService,
  QWidget* parent)
    : QDialog(parent), defaultDirectory_(std::move(defaultDirectory)),
      dialogService_(std::move(dialogService))
{
  setObjectName(QStringLiteral("screenshotSettingsDialog"));
  setWindowTitle(tr("Screenshot Settings"));
  setModal(false);
  resize(640, 190);

  auto* root = new QVBoxLayout(this);
  auto* explanation = new QLabel(
    tr("Native emulator frames are saved as PNG files in this directory."), this);
  explanation->setObjectName(QStringLiteral("screenshotSettingsExplanation"));
  explanation->setWordWrap(true);
  root->addWidget(explanation);
  auto* form = new QFormLayout;
  auto* pathRow = new QHBoxLayout;
  directory_ = new QLineEdit(this);
  directory_->setObjectName(QStringLiteral("screenshotDirectoryEdit"));
  directory_->setReadOnly(true);
  directory_->setAccessibleName(tr("Screenshot directory"));
  auto* browse = new QPushButton(tr("Browse…"), this);
  browse->setObjectName(QStringLiteral("browseScreenshotDirectoryButton"));
  connect(
    browse, &QPushButton::clicked, this, &ScreenshotSettingsDialog::chooseDirectory);
  pathRow->addWidget(directory_, 1);
  pathRow->addWidget(browse);
  form->addRow(tr("Directory:"), pathRow);
  root->addLayout(form);

  auto* buttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                           QDialogButtonBox::Apply | QDialogButtonBox::RestoreDefaults,
      Qt::Horizontal,
      this);
  buttons->setObjectName(QStringLiteral("screenshotSettingsButtonBox"));
  buttons->button(QDialogButtonBox::Ok)
    ->setObjectName(QStringLiteral("okScreenshotSettingsButton"));
  buttons->button(QDialogButtonBox::Cancel)
    ->setObjectName(QStringLiteral("cancelScreenshotSettingsButton"));
  buttons->button(QDialogButtonBox::Apply)
    ->setObjectName(QStringLiteral("applyScreenshotSettingsButton"));
  buttons->button(QDialogButtonBox::RestoreDefaults)
    ->setObjectName(QStringLiteral("restoreScreenshotDefaultsButton"));
  connect(
    buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
      static_cast<void>(apply());
    });
  connect(buttons->button(QDialogButtonBox::RestoreDefaults),
    &QPushButton::clicked,
    this,
    &ScreenshotSettingsDialog::restoreDefaults);
  connect(buttons, &QDialogButtonBox::accepted, this, [this] {
    if (apply()) {
      accept();
    }
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);
  setSettings(current);
}

void ScreenshotSettingsDialog::setDialogService(std::shared_ptr<DialogService> service)
{
  if (service) {
    dialogService_ = std::move(service);
  }
}

void ScreenshotSettingsDialog::setSettingsSink(SettingsSink sink)
{
  settingsSink_ = std::move(sink);
}

void ScreenshotSettingsDialog::setSettings(const settings::ScreenshotSettings& value)
{
  if (settings::validateScreenshotSettings(value)) {
    directory_->setText(pathToQString(value.directory));
  }
}

settings::ScreenshotSettings ScreenshotSettingsDialog::settings() const
{
  return {.directory = pathFromQString(directory_->text())};
}

bool ScreenshotSettingsDialog::apply()
{
  const auto value = settings();
  return settings::validateScreenshotSettings(value) &&
         (!settingsSink_ || settingsSink_(value));
}

void ScreenshotSettingsDialog::chooseDirectory()
{
  const auto selected = dialogService_->chooseDirectory(this, settings().directory);
  if (selected) {
    setSettings({.directory = *selected});
  }
}

void ScreenshotSettingsDialog::restoreDefaults()
{
  setSettings({.directory = defaultDirectory_});
}

} // namespace genplusgx::ui
