#include "genplusgx/ui/system_settings_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace genplusgx::ui {
namespace {

QComboBox* combo(QWidget& parent, const char* objectName)
{
  auto* value = new QComboBox(&parent);
  value->setObjectName(QString::fromLatin1(objectName));
  return value;
}

template<typename Enum>
void addChoice(QComboBox& box, const QString& label, Enum value)
{
  box.addItem(label, static_cast<int>(value));
}

template<typename Enum>
Enum choice(const QComboBox& box)
{
  return static_cast<Enum>(box.currentData().toInt());
}

template<typename Enum>
void select(QComboBox& box, Enum value)
{
  const auto index = box.findData(static_cast<int>(value));
  if (index >= 0) {
    box.setCurrentIndex(index);
  }
}

} // namespace

SystemSettingsDialog::SystemSettingsDialog(
  CoreSystemSettings current,
  QWidget* parent)
  : QDialog(parent)
{
  setObjectName(QStringLiteral("systemSettingsDialog"));
  setWindowTitle(tr("System Settings"));
  setModal(false);
  resize(560, 480);

  auto* root = new QVBoxLayout(this);
  auto* note = new QLabel(
    tr("System, region, VDP, clock, and accuracy changes take effect the next "
       "time a game is loaded."), this);
  note->setObjectName(QStringLiteral("systemSettingsReloadNote"));
  note->setWordWrap(true);
  root->addWidget(note);

  auto* identityGroup = new QGroupBox(tr("Console identity and timing"), this);
  identityGroup->setObjectName(QStringLiteral("systemIdentityGroup"));
  auto* identityForm = new QFormLayout(identityGroup);
  hardware_ = combo(*identityGroup, "systemHardwareCombo");
  addChoice(*hardware_, tr("Automatic"), CoreSystemHardware::automatic);
  addChoice(*hardware_, tr("SG-1000"), CoreSystemHardware::sg1000);
  addChoice(*hardware_, tr("SG-1000 II"), CoreSystemHardware::sg1000II);
  addChoice(*hardware_, tr("SG-1000 II + RAM extension"),
    CoreSystemHardware::sg1000IIRamExtension);
  addChoice(*hardware_, tr("Mark III"), CoreSystemHardware::markIII);
  addChoice(*hardware_, tr("Master System"), CoreSystemHardware::masterSystem);
  addChoice(*hardware_, tr("Master System II"), CoreSystemHardware::masterSystemII);
  addChoice(*hardware_, tr("Game Gear"), CoreSystemHardware::gameGear);
  addChoice(*hardware_, tr("Mega Drive / Genesis"), CoreSystemHardware::genesis);
  identityForm->addRow(tr("Emulated hardware:"), hardware_);

  region_ = combo(*identityGroup, "systemRegionCombo");
  addChoice(*region_, tr("Automatic"), CoreSystemRegion::automatic);
  addChoice(*region_, tr("NTSC-U (Americas)"), CoreSystemRegion::ntscU);
  addChoice(*region_, tr("PAL (Europe)"), CoreSystemRegion::palEurope);
  addChoice(*region_, tr("NTSC-J (Japan)"), CoreSystemRegion::ntscJapan);
  addChoice(*region_, tr("PAL (Japan)"), CoreSystemRegion::palJapan);
  identityForm->addRow(tr("Console region:"), region_);

  videoStandard_ = combo(*identityGroup, "vdpModeCombo");
  addChoice(*videoStandard_, tr("Automatic"), CoreVideoStandard::automatic);
  addChoice(*videoStandard_, tr("NTSC (60 Hz)"), CoreVideoStandard::ntsc);
  addChoice(*videoStandard_, tr("PAL (50 Hz)"), CoreVideoStandard::pal);
  identityForm->addRow(tr("Force VDP mode:"), videoStandard_);

  masterClock_ = combo(*identityGroup, "masterClockCombo");
  addChoice(*masterClock_, tr("Automatic"), CoreMasterClock::automatic);
  addChoice(*masterClock_, tr("NTSC clock (53.693175 MHz)"),
    CoreMasterClock::ntsc);
  addChoice(*masterClock_, tr("PAL clock (53.203424 MHz)"),
    CoreMasterClock::pal);
  identityForm->addRow(tr("Master clock:"), masterClock_);
  root->addWidget(identityGroup);

  auto* accuracyGroup = new QGroupBox(tr("Accuracy"), this);
  accuracyGroup->setObjectName(QStringLiteral("systemAccuracyGroup"));
  auto* accuracyLayout = new QVBoxLayout(accuracyGroup);
  illegalAccessLockups_ = new QCheckBox(
    tr("Emulate lockups on illegal hardware access"), accuracyGroup);
  illegalAccessLockups_->setObjectName(
    QStringLiteral("illegalAccessLockupsCheckBox"));
  addressErrors_ = new QCheckBox(
    tr("Enable 68000 address-error exceptions"), accuracyGroup);
  addressErrors_->setObjectName(QStringLiteral("addressErrorsCheckBox"));
  accuracyLayout->addWidget(illegalAccessLockups_);
  accuracyLayout->addWidget(addressErrors_);
  auto* warning = new QLabel(
    tr("Disabling either accuracy option is intended only for incompatible demos "
       "or homebrew."), accuracyGroup);
  warning->setObjectName(QStringLiteral("systemAccuracyWarningLabel"));
  warning->setWordWrap(true);
  accuracyLayout->addWidget(warning);
  root->addWidget(accuracyGroup);
  root->addStretch();

  auto* buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
      QDialogButtonBox::Apply | QDialogButtonBox::RestoreDefaults,
    Qt::Horizontal, this);
  buttons->setObjectName(QStringLiteral("systemSettingsButtonBox"));
  buttons->button(QDialogButtonBox::Ok)->setObjectName(
    QStringLiteral("okSystemSettingsButton"));
  buttons->button(QDialogButtonBox::Cancel)->setObjectName(
    QStringLiteral("cancelSystemSettingsButton"));
  buttons->button(QDialogButtonBox::Apply)->setObjectName(
    QStringLiteral("applySystemSettingsButton"));
  buttons->button(QDialogButtonBox::RestoreDefaults)->setObjectName(
    QStringLiteral("restoreSystemDefaultsButton"));
  connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
    this, &SystemSettingsDialog::apply);
  connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
    this, &SystemSettingsDialog::restoreDefaults);
  connect(buttons, &QDialogButtonBox::accepted, this, [this] {
    apply();
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);
  setSettings(current);
}

void SystemSettingsDialog::setSettingsSink(SettingsSink sink)
{
  settingsSink_ = std::move(sink);
}

CoreSystemSettings SystemSettingsDialog::settings() const
{
  return {
    .hardware = choice<CoreSystemHardware>(*hardware_),
    .region = choice<CoreSystemRegion>(*region_),
    .videoStandard = choice<CoreVideoStandard>(*videoStandard_),
    .masterClock = choice<CoreMasterClock>(*masterClock_),
    .emulateIllegalAccessLockups = illegalAccessLockups_->isChecked(),
    .enableAddressErrors = addressErrors_->isChecked(),
  };
}

void SystemSettingsDialog::setSettings(const CoreSystemSettings& value)
{
  if (!validateCoreSystemSettings(value)) {
    return;
  }
  select(*hardware_, value.hardware);
  select(*region_, value.region);
  select(*videoStandard_, value.videoStandard);
  select(*masterClock_, value.masterClock);
  illegalAccessLockups_->setChecked(value.emulateIllegalAccessLockups);
  addressErrors_->setChecked(value.enableAddressErrors);
}

void SystemSettingsDialog::apply()
{
  const auto value = settings();
  if (validateCoreSystemSettings(value) && settingsSink_) {
    settingsSink_(value);
  }
}

void SystemSettingsDialog::restoreDefaults()
{
  setSettings(CoreSystemSettings{});
}

} // namespace genplusgx::ui
