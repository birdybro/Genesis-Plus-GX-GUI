#include "genplusgx/ui/audio_settings_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
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

QSpinBox* percent(
  QWidget& parent,
  const char* objectName,
  int maximum = 200)
{
  auto* value = new QSpinBox(&parent);
  value->setObjectName(QString::fromLatin1(objectName));
  value->setRange(0, maximum);
  value->setSuffix(QStringLiteral(" %"));
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

AudioSettingsDialog::AudioSettingsDialog(
  settings::AudioSettings current,
  std::vector<std::string> availableDevices,
  QWidget* parent)
  : QDialog(parent)
{
  setObjectName(QStringLiteral("audioSettingsDialog"));
  setWindowTitle(tr("Audio Settings"));
  setModal(false);
  resize(580, 680);

  auto* root = new QVBoxLayout(this);
  auto* scroll = new QScrollArea(this);
  scroll->setObjectName(QStringLiteral("audioSettingsScrollArea"));
  scroll->setWidgetResizable(true);
  auto* content = new QWidget(scroll);
  auto* contentLayout = new QVBoxLayout(content);

  auto* outputGroup = new QGroupBox(tr("Output"), content);
  outputGroup->setObjectName(QStringLiteral("hostAudioOutputGroup"));
  auto* outputForm = new QFormLayout(outputGroup);
  device_ = combo(*outputGroup, "audioOutputDeviceCombo");
  device_->addItem(tr("System default"), QString{});
  for (const auto& name : availableDevices) {
    const auto qName = QString::fromStdString(name);
    if (device_->findData(qName) < 0) {
      device_->addItem(qName, qName);
    }
  }
  if (!current.outputDeviceName.empty()) {
    const auto configured = QString::fromStdString(current.outputDeviceName);
    if (device_->findData(configured) < 0) {
      device_->addItem(tr("%1 (currently unavailable)").arg(configured), configured);
    }
  }
  outputForm->addRow(tr("Playback device:"), device_);
  latency_ = new QSpinBox(outputGroup);
  latency_->setObjectName(QStringLiteral("audioLatencySpinBox"));
  latency_->setRange(10, 500);
  latency_->setSuffix(tr(" ms"));
  outputForm->addRow(tr("Buffer latency:"), latency_);
  auto* restartNote = new QLabel(
    tr("Playback device and latency changes take effect after restarting the application."),
    outputGroup);
  restartNote->setObjectName(QStringLiteral("audioRestartRequiredLabel"));
  restartNote->setWordWrap(true);
  outputForm->addRow(QString{}, restartNote);
  volume_ = percent(*outputGroup, "masterVolumeSpinBox", 100);
  outputForm->addRow(tr("Master volume:"), volume_);
  muted_ = new QCheckBox(tr("Mute output"), outputGroup);
  muted_->setObjectName(QStringLiteral("muteAudioCheckBox"));
  outputForm->addRow(QString{}, muted_);
  contentLayout->addWidget(outputGroup);

  auto* mixerGroup = new QGroupBox(tr("Core mixer"), content);
  mixerGroup->setObjectName(QStringLiteral("coreAudioMixerGroup"));
  auto* mixerForm = new QFormLayout(mixerGroup);
  output_ = combo(*mixerGroup, "coreSoundOutputCombo");
  addChoice(*output_, tr("Stereo"), CoreSoundOutput::stereo);
  addChoice(*output_, tr("Mono"), CoreSoundOutput::mono);
  mixerForm->addRow(tr("Channels:"), output_);
  psg_ = percent(*mixerGroup, "psgLevelSpinBox");
  fm_ = percent(*mixerGroup, "fmLevelSpinBox");
  cdda_ = percent(*mixerGroup, "cddaLevelSpinBox", 100);
  pcm_ = percent(*mixerGroup, "pcmLevelSpinBox", 100);
  mixerForm->addRow(tr("PSG level:"), psg_);
  mixerForm->addRow(tr("FM level:"), fm_);
  mixerForm->addRow(tr("CDDA level:"), cdda_);
  mixerForm->addRow(tr("Sega CD PCM level:"), pcm_);
  contentLayout->addWidget(mixerGroup);

  auto* filterGroup = new QGroupBox(tr("Filtering"), content);
  filterGroup->setObjectName(QStringLiteral("coreAudioFilterGroup"));
  auto* filterForm = new QFormLayout(filterGroup);
  filter_ = combo(*filterGroup, "coreAudioFilterCombo");
  addChoice(*filter_, tr("Disabled"), CoreAudioFilter::disabled);
  addChoice(*filter_, tr("Low-pass"), CoreAudioFilter::lowPass);
  addChoice(*filter_, tr("Three-band equalizer"), CoreAudioFilter::equalizer);
  filterForm->addRow(tr("Mode:"), filter_);
  lowPass_ = new QSpinBox(filterGroup);
  lowPass_->setObjectName(QStringLiteral("lowPassRangeSpinBox"));
  lowPass_->setRange(5, 95);
  lowPass_->setSuffix(QStringLiteral(" %"));
  filterForm->addRow(tr("Low-pass strength:"), lowPass_);
  eqLow_ = percent(*filterGroup, "equalizerLowSpinBox");
  eqMid_ = percent(*filterGroup, "equalizerMidSpinBox");
  eqHigh_ = percent(*filterGroup, "equalizerHighSpinBox");
  filterForm->addRow(tr("Equalizer low:"), eqLow_);
  filterForm->addRow(tr("Equalizer mid:"), eqMid_);
  filterForm->addRow(tr("Equalizer high:"), eqHigh_);
  connect(filter_, &QComboBox::currentIndexChanged,
    this, &AudioSettingsDialog::updateFilterControls);
  contentLayout->addWidget(filterGroup);

  auto* chipsGroup = new QGroupBox(tr("Sound chips"), content);
  chipsGroup->setObjectName(QStringLiteral("coreSoundChipsGroup"));
  auto* chipsForm = new QFormLayout(chipsGroup);
  ym2612_ = combo(*chipsGroup, "ym2612CoreCombo");
  addChoice(*ym2612_, tr("MAME YM2612 (discrete)"),
    CoreYm2612Core::mameDiscrete);
  addChoice(*ym2612_, tr("MAME YM2612 (integrated)"),
    CoreYm2612Core::mameIntegrated);
  addChoice(*ym2612_, tr("MAME YM2612 (enhanced)"),
    CoreYm2612Core::mameEnhanced);
  addChoice(*ym2612_, tr("Nuked OPN2 (YM2612)"),
    CoreYm2612Core::nukedYm2612);
  addChoice(*ym2612_, tr("Nuked OPN2 (YM3438)"),
    CoreYm2612Core::nukedYm3438);
  chipsForm->addRow(tr("Genesis FM core:"), ym2612_);
  ym2413Mode_ = combo(*chipsGroup, "ym2413ModeCombo");
  addChoice(*ym2413Mode_, tr("Disabled"), CoreYm2413Mode::disabled);
  addChoice(*ym2413Mode_, tr("Enabled"), CoreYm2413Mode::enabled);
  addChoice(*ym2413Mode_, tr("Automatic"), CoreYm2413Mode::autoDetect);
  chipsForm->addRow(tr("Master System FM:"), ym2413Mode_);
  ym2413Core_ = combo(*chipsGroup, "ym2413CoreCombo");
  addChoice(*ym2413Core_, tr("MAME YM2413"), CoreYm2413Core::mame);
  addChoice(*ym2413Core_, tr("Nuked OPLL"), CoreYm2413Core::nuked);
  chipsForm->addRow(tr("YM2413 core:"), ym2413Core_);
  highQualityFm_ = new QCheckBox(tr("High-quality FM resampling"), chipsGroup);
  highQualityFm_->setObjectName(QStringLiteral("highQualityFmCheckBox"));
  highQualityPsg_ = new QCheckBox(tr("High-quality PSG resampling"), chipsGroup);
  highQualityPsg_->setObjectName(QStringLiteral("highQualityPsgCheckBox"));
  chipsForm->addRow(QString{}, highQualityFm_);
  chipsForm->addRow(QString{}, highQualityPsg_);
  contentLayout->addWidget(chipsGroup);
  contentLayout->addStretch();
  scroll->setWidget(content);
  root->addWidget(scroll);

  auto* buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
      QDialogButtonBox::Apply | QDialogButtonBox::RestoreDefaults,
    Qt::Horizontal, this);
  buttons->setObjectName(QStringLiteral("audioSettingsButtonBox"));
  buttons->button(QDialogButtonBox::Ok)->setObjectName(
    QStringLiteral("okAudioSettingsButton"));
  buttons->button(QDialogButtonBox::Cancel)->setObjectName(
    QStringLiteral("cancelAudioSettingsButton"));
  buttons->button(QDialogButtonBox::Apply)->setObjectName(
    QStringLiteral("applyAudioSettingsButton"));
  buttons->button(QDialogButtonBox::RestoreDefaults)->setObjectName(
    QStringLiteral("restoreAudioDefaultsButton"));
  connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
    this, &AudioSettingsDialog::apply);
  connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
    this, &AudioSettingsDialog::restoreDefaults);
  connect(buttons, &QDialogButtonBox::accepted, this, [this] {
    apply();
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);
  setSettings(current);
}

void AudioSettingsDialog::setSettingsSink(SettingsSink sink)
{
  settingsSink_ = std::move(sink);
}

settings::AudioSettings AudioSettingsDialog::settings() const
{
  return {
    .masterVolumePercent = volume_->value(),
    .muted = muted_->isChecked(),
    .latencyMilliseconds = latency_->value(),
    .outputDeviceName = device_->currentData().toString().toStdString(),
    .core = {
      .output = choice<CoreSoundOutput>(*output_),
      .filter = choice<CoreAudioFilter>(*filter_),
      .ym2612Core = choice<CoreYm2612Core>(*ym2612_),
      .ym2413Mode = choice<CoreYm2413Mode>(*ym2413Mode_),
      .ym2413Core = choice<CoreYm2413Core>(*ym2413Core_),
      .psgLevelPercent = psg_->value(),
      .fmLevelPercent = fm_->value(),
      .cddaLevelPercent = cdda_->value(),
      .pcmLevelPercent = pcm_->value(),
      .lowPassPercent = lowPass_->value(),
      .equalizerLowPercent = eqLow_->value(),
      .equalizerMidPercent = eqMid_->value(),
      .equalizerHighPercent = eqHigh_->value(),
      .highQualityFm = highQualityFm_->isChecked(),
      .highQualityPsg = highQualityPsg_->isChecked(),
    },
  };
}

void AudioSettingsDialog::setSettings(const settings::AudioSettings& value)
{
  if (!settings::validateAudioSettings(value)) {
    return;
  }
  volume_->setValue(value.masterVolumePercent);
  muted_->setChecked(value.muted);
  latency_->setValue(value.latencyMilliseconds);
  const auto deviceName = QString::fromStdString(value.outputDeviceName);
  const auto deviceIndex = device_->findData(deviceName);
  device_->setCurrentIndex(deviceIndex < 0 ? 0 : deviceIndex);
  select(*output_, value.core.output);
  select(*filter_, value.core.filter);
  select(*ym2612_, value.core.ym2612Core);
  select(*ym2413Mode_, value.core.ym2413Mode);
  select(*ym2413Core_, value.core.ym2413Core);
  psg_->setValue(value.core.psgLevelPercent);
  fm_->setValue(value.core.fmLevelPercent);
  cdda_->setValue(value.core.cddaLevelPercent);
  pcm_->setValue(value.core.pcmLevelPercent);
  lowPass_->setValue(value.core.lowPassPercent);
  eqLow_->setValue(value.core.equalizerLowPercent);
  eqMid_->setValue(value.core.equalizerMidPercent);
  eqHigh_->setValue(value.core.equalizerHighPercent);
  highQualityFm_->setChecked(value.core.highQualityFm);
  highQualityPsg_->setChecked(value.core.highQualityPsg);
  updateFilterControls();
}

void AudioSettingsDialog::apply()
{
  const auto value = settings();
  if (settings::validateAudioSettings(value) && settingsSink_) {
    settingsSink_(value);
  }
}

void AudioSettingsDialog::restoreDefaults()
{
  setSettings(settings::defaultAudioSettings());
}

void AudioSettingsDialog::updateFilterControls()
{
  const auto mode = choice<CoreAudioFilter>(*filter_);
  lowPass_->setEnabled(mode == CoreAudioFilter::lowPass);
  for (auto* control : {eqLow_, eqMid_, eqHigh_}) {
    control->setEnabled(mode == CoreAudioFilter::equalizer);
  }
}

} // namespace genplusgx::ui
