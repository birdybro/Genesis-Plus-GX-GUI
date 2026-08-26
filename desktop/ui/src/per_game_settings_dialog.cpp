#include "genplusgx/ui/per_game_settings_dialog.h"

#include "genplusgx/ui/audio_settings_dialog.h"
#include "genplusgx/ui/bios_settings_dialog.h"
#include "genplusgx/ui/system_settings_dialog.h"
#include "genplusgx/ui/video_settings_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace genplusgx::ui {
namespace {

QPushButton* editButton(
  QWidget& parent, const QString& accessibleName, const char* objectName)
{
  auto* result = new QPushButton(QObject::tr("Edit Override…"), &parent);
  result->setObjectName(QString::fromLatin1(objectName));
  result->setAccessibleName(accessibleName);
  return result;
}

platform::BiosSnapshot biosSnapshot(const platform::BiosConfiguration& configuration)
{
  platform::BiosSnapshot snapshot;
  snapshot.configuration = configuration;
  for (const auto& descriptor : platform::biosDescriptors()) {
    snapshot.validation[static_cast<std::size_t>(descriptor.slot)] =
      platform::validateBios(descriptor.slot, configuration.path(descriptor.slot));
  }
  return snapshot;
}

} // namespace

PerGameSettingsDialog::PerGameSettingsDialog(settings::PerGameSettings overrides,
  settings::GlobalGameSettings global,
  std::vector<std::string> inputProfiles,
  std::vector<std::string> audioDevices,
  QWidget* parent)
    : QDialog(parent), global_(std::move(global)), video_(global_.video),
      audio_(global_.audio), system_(global_.system), bios_(global_.bios),
      inputProfiles_(std::move(inputProfiles)), audioDevices_(std::move(audioDevices))
{
  setObjectName(QStringLiteral("perGameSettingsDialog"));
  setWindowTitle(tr("Per-Game Settings"));
  setModal(false);
  resize(620, 540);

  auto* root = new QVBoxLayout(this);
  auto* explanation = new QLabel(
    tr("Only checked categories are stored for this game. Unchecked categories "
       "continue to inherit global settings, including future global changes."),
    this);
  explanation->setObjectName(QStringLiteral("perGameSettingsExplanation"));
  explanation->setWordWrap(true);
  root->addWidget(explanation);

  auto* categories = new QGroupBox(tr("Overrides"), this);
  categories->setObjectName(QStringLiteral("perGameOverrideGroup"));
  auto* rows = new QVBoxLayout(categories);

  const auto addEditorRow = [rows, categories](QCheckBox*& check,
                              QPushButton*& button,
                              const QString& label,
                              const char* checkName,
                              const char* buttonName) {
    auto* row = new QHBoxLayout;
    check = new QCheckBox(label, categories);
    check->setObjectName(QString::fromLatin1(checkName));
    row->addWidget(check, 1);
    button = editButton(*categories, label, buttonName);
    row->addWidget(button);
    rows->addLayout(row);
  };
  addEditorRow(videoOverride_,
    editVideoButton_,
    tr("Override video settings"),
    "overrideVideoCheckBox",
    "editPerGameVideoButton");
  addEditorRow(audioOverride_,
    editAudioButton_,
    tr("Override audio settings"),
    "overrideAudioCheckBox",
    "editPerGameAudioButton");
  addEditorRow(systemOverride_,
    editSystemButton_,
    tr("Override system and region"),
    "overrideSystemCheckBox",
    "editPerGameSystemButton");

  auto* inputRow = new QHBoxLayout;
  inputOverride_ = new QCheckBox(tr("Override input profile"), categories);
  inputOverride_->setObjectName(QStringLiteral("overrideInputProfileCheckBox"));
  inputRow->addWidget(inputOverride_, 1);
  inputProfile_ = new QComboBox(categories);
  inputProfile_->setObjectName(QStringLiteral("perGameInputProfileCombo"));
  inputProfile_->setAccessibleName(tr("Per-game input profile"));
  for (const auto& profile : inputProfiles_) {
    inputProfile_->addItem(QString::fromStdString(profile));
  }
  inputRow->addWidget(inputProfile_);
  rows->addLayout(inputRow);

  addEditorRow(biosOverride_,
    editBiosButton_,
    tr("Override BIOS paths"),
    "overrideBiosCheckBox",
    "editPerGameBiosButton");
  root->addWidget(categories);

  auto* timingNote = new QLabel(
    tr("System, region, BIOS, and input-profile overrides are selected before "
       "the game loads. Changing them while a game is running requires reopening "
       "that game. Video and audio changes apply immediately."),
    this);
  timingNote->setObjectName(QStringLiteral("perGameReloadNote"));
  timingNote->setWordWrap(true);
  root->addWidget(timingNote);

  validation_ = new QLabel(this);
  validation_->setObjectName(QStringLiteral("perGameSettingsValidationLabel"));
  validation_->setWordWrap(true);
  validation_->setStyleSheet(QStringLiteral("color: palette(highlight);"));
  validation_->hide();
  root->addWidget(validation_);
  root->addStretch();

  auto* buttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                           QDialogButtonBox::Apply | QDialogButtonBox::RestoreDefaults,
      Qt::Horizontal,
      this);
  buttons->setObjectName(QStringLiteral("perGameSettingsButtonBox"));
  buttons->button(QDialogButtonBox::Ok)
    ->setObjectName(QStringLiteral("okPerGameSettingsButton"));
  buttons->button(QDialogButtonBox::Cancel)
    ->setObjectName(QStringLiteral("cancelPerGameSettingsButton"));
  buttons->button(QDialogButtonBox::Apply)
    ->setObjectName(QStringLiteral("applyPerGameSettingsButton"));
  buttons->button(QDialogButtonBox::RestoreDefaults)
    ->setObjectName(QStringLiteral("useGlobalSettingsButton"));
  buttons->button(QDialogButtonBox::RestoreDefaults)
    ->setText(tr("Use Global Settings"));
  connect(
    buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
      static_cast<void>(apply());
    });
  connect(buttons->button(QDialogButtonBox::RestoreDefaults),
    &QPushButton::clicked,
    this,
    &PerGameSettingsDialog::restoreGlobal);
  connect(buttons->button(QDialogButtonBox::Ok), &QPushButton::clicked, this, [this] {
    if (apply()) {
      accept();
    }
  });
  connect(buttons->button(QDialogButtonBox::Cancel),
    &QPushButton::clicked,
    this,
    &QDialog::reject);
  root->addWidget(buttons);

  for (auto* check :
    {videoOverride_, audioOverride_, systemOverride_, inputOverride_, biosOverride_}) {
    connect(check, &QCheckBox::toggled, this, &PerGameSettingsDialog::updateControls);
  }
  connect(
    editVideoButton_, &QPushButton::clicked, this, &PerGameSettingsDialog::editVideo);
  connect(
    editAudioButton_, &QPushButton::clicked, this, &PerGameSettingsDialog::editAudio);
  connect(
    editSystemButton_, &QPushButton::clicked, this, &PerGameSettingsDialog::editSystem);
  connect(
    editBiosButton_, &QPushButton::clicked, this, &PerGameSettingsDialog::editBios);
  setConfiguration(overrides);
}

void PerGameSettingsDialog::setConfigurationSink(ConfigurationSink sink)
{
  configurationSink_ = std::move(sink);
}

void PerGameSettingsDialog::setSession(
  const settings::PerGameSettings& overrides, settings::GlobalGameSettings global)
{
  global_ = std::move(global);
  setConfiguration(overrides);
}

void PerGameSettingsDialog::setConfiguration(const settings::PerGameSettings& overrides)
{
  if (!settings::validatePerGameSettings(overrides)) {
    return;
  }
  video_ = overrides.video.value_or(global_.video);
  audio_ = overrides.audio.value_or(global_.audio);
  system_ = overrides.system.value_or(global_.system);
  bios_ = overrides.bios.value_or(global_.bios);
  videoOverride_->setChecked(overrides.video.has_value());
  audioOverride_->setChecked(overrides.audio.has_value());
  systemOverride_->setChecked(overrides.system.has_value());
  biosOverride_->setChecked(overrides.bios.has_value());
  inputOverride_->setChecked(overrides.inputProfile.has_value());
  if (overrides.inputProfile) {
    auto index =
      inputProfile_->findText(QString::fromStdString(*overrides.inputProfile));
    if (index < 0) {
      inputProfile_->addItem(QString::fromStdString(*overrides.inputProfile));
      index = inputProfile_->count() - 1;
    }
    inputProfile_->setCurrentIndex(index);
  } else {
    const auto index =
      inputProfile_->findText(QString::fromStdString(global_.inputProfile));
    if (index >= 0) {
      inputProfile_->setCurrentIndex(index);
    }
  }
  validation_->clear();
  validation_->hide();
  updateControls();
}

settings::PerGameSettings PerGameSettingsDialog::configuration() const
{
  settings::PerGameSettings result;
  if (videoOverride_->isChecked()) {
    result.video = video_;
  }
  if (audioOverride_->isChecked()) {
    result.audio = audio_;
  }
  if (systemOverride_->isChecked()) {
    result.system = system_;
  }
  if (inputOverride_->isChecked() && inputProfile_->currentIndex() >= 0) {
    result.inputProfile = inputProfile_->currentText().toStdString();
  }
  if (biosOverride_->isChecked()) {
    result.bios = bios_;
  }
  return result;
}

void PerGameSettingsDialog::updateControls()
{
  editVideoButton_->setEnabled(videoOverride_->isChecked());
  editAudioButton_->setEnabled(audioOverride_->isChecked());
  editSystemButton_->setEnabled(systemOverride_->isChecked());
  inputProfile_->setEnabled(inputOverride_->isChecked());
  editBiosButton_->setEnabled(biosOverride_->isChecked());
}

void PerGameSettingsDialog::editVideo()
{
  if (auto* existing =
        findChild<VideoSettingsDialog*>(QStringLiteral("videoSettingsDialog"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new VideoSettingsDialog(video_, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setSettingsSink(
    [this](const settings::VideoSettings& value) {
      video_ = value;
      return true;
    });
  dialog->open();
}

void PerGameSettingsDialog::editAudio()
{
  if (auto* existing =
        findChild<AudioSettingsDialog*>(QStringLiteral("audioSettingsDialog"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new AudioSettingsDialog(audio_, audioDevices_, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  if (auto* device =
        dialog->findChild<QComboBox*>(QStringLiteral("audioOutputDeviceCombo"))) {
    device->setEnabled(false);
    device->setToolTip(tr("Audio output device is a global setting."));
  }
  if (auto* latency =
        dialog->findChild<QSpinBox*>(QStringLiteral("audioLatencySpinBox"))) {
    latency->setEnabled(false);
    latency->setToolTip(tr("Audio buffer latency is a global setting."));
  }
  dialog->setSettingsSink([this](const settings::AudioSettings& value) {
    audio_ = value;
    audio_.latencyMilliseconds = global_.audio.latencyMilliseconds;
    audio_.outputDeviceName = global_.audio.outputDeviceName;
  });
  dialog->open();
}

void PerGameSettingsDialog::editSystem()
{
  if (auto* existing =
        findChild<SystemSettingsDialog*>(QStringLiteral("systemSettingsDialog"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new SystemSettingsDialog(system_, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setSettingsSink([this](const CoreSystemSettings& value) {
    system_ = value;
    return true;
  });
  dialog->open();
}

void PerGameSettingsDialog::editBios()
{
  if (auto* existing =
        findChild<BiosSettingsDialog*>(QStringLiteral("biosSettingsDialog"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new BiosSettingsDialog(biosSnapshot(bios_), this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setConfigurationSink(
    [this](const platform::BiosConfiguration& value) { bios_ = value; });
  dialog->open();
}

bool PerGameSettingsDialog::apply()
{
  const auto value = configuration();
  if (!settings::validatePerGameSettings(value)) {
    validation_->setText(tr("The selected overrides contain an invalid value."));
    validation_->show();
    return false;
  }
  if (inputOverride_->isChecked() && inputProfile_->currentIndex() < 0) {
    validation_->setText(tr("Choose an input profile or use the global profile."));
    validation_->show();
    return false;
  }
  if (configurationSink_) {
    const auto saved = configurationSink_(value);
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

void PerGameSettingsDialog::restoreGlobal() { setConfiguration({}); }

} // namespace genplusgx::ui
