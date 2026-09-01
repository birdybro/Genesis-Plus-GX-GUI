#include "genplusgx/ui/speed_settings_dialog.h"

#include "genplusgx/settings/speed_settings.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <utility>

namespace genplusgx::ui {

SpeedSettingsDialog::SpeedSettingsDialog(
  EmulationSpeedConfiguration current,
  QWidget* parent)
  : QDialog(parent)
{
  setObjectName(QStringLiteral("speedSettingsDialog"));
  setWindowTitle(tr("Emulation Speed Settings"));
  setModal(true);
  setMinimumWidth(540);

  auto* root = new QVBoxLayout(this);
  auto* introduction = new QLabel(
    tr("Choose exact host pacing percentages for normal play, slow motion, and "
       "fast forward. Genesis Plus GX still executes complete frames; timing "
       "changes never alter the core's clocks or algorithms."), this);
  introduction->setObjectName(QStringLiteral("speedIntroductionLabel"));
  introduction->setWordWrap(true);
  root->addWidget(introduction);

  auto* group = new QGroupBox(tr("Pacing"), this);
  group->setObjectName(QStringLiteral("speedPacingGroup"));
  auto* form = new QFormLayout(group);

  normal_ = new QSpinBox(group);
  normal_->setObjectName(QStringLiteral("normalSpeedPercentSpinBox"));
  normal_->setRange(
    static_cast<int>(minimumNormalSpeedPercent),
    static_cast<int>(maximumNormalSpeedPercent));
  normal_->setSingleStep(25);
  normal_->setSuffix(tr("%"));
  normal_->setAccessibleName(tr("Normal emulation speed percentage"));
  form->addRow(tr("Normal speed:"), normal_);

  slow_ = new QSpinBox(group);
  slow_->setObjectName(QStringLiteral("slowMotionPercentSpinBox"));
  slow_->setRange(
    static_cast<int>(minimumSlowMotionSpeedPercent),
    static_cast<int>(maximumSlowMotionSpeedPercent));
  slow_->setSingleStep(25);
  slow_->setSuffix(tr("%"));
  slow_->setAccessibleName(tr("Slow motion speed percentage"));
  form->addRow(tr("Slow motion:"), slow_);

  fast_ = new QSpinBox(group);
  fast_->setObjectName(QStringLiteral("fastForwardPercentSpinBox"));
  fast_->setRange(
    static_cast<int>(minimumFastForwardSpeedPercent),
    static_cast<int>(maximumFastForwardSpeedPercent));
  fast_->setSingleStep(100);
  fast_->setSuffix(tr("%"));
  fast_->setAccessibleName(tr("Fast forward speed percentage"));
  form->addRow(tr("Fast forward:"), fast_);
  root->addWidget(group);

  summary_ = new QLabel(this);
  summary_->setObjectName(QStringLiteral("speedBehaviorSummaryLabel"));
  summary_->setWordWrap(true);
  root->addWidget(summary_);

  error_ = new QLabel(this);
  error_->setObjectName(QStringLiteral("speedSettingsErrorLabel"));
  error_->setWordWrap(true);
  error_->setStyleSheet(QStringLiteral("color: palette(highlight);"));
  error_->hide();
  root->addWidget(error_);

  auto* buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
      QDialogButtonBox::Apply | QDialogButtonBox::RestoreDefaults,
    this);
  buttons->setObjectName(QStringLiteral("speedSettingsButtonBox"));
  buttons->button(QDialogButtonBox::Ok)->setObjectName(
    QStringLiteral("okSpeedSettingsButton"));
  buttons->button(QDialogButtonBox::Cancel)->setObjectName(
    QStringLiteral("cancelSpeedSettingsButton"));
  buttons->button(QDialogButtonBox::Apply)->setObjectName(
    QStringLiteral("applySpeedSettingsButton"));
  buttons->button(QDialogButtonBox::RestoreDefaults)->setObjectName(
    QStringLiteral("restoreSpeedDefaultsButton"));
  connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
    this, [this] { static_cast<void>(apply()); });
  connect(buttons->button(QDialogButtonBox::RestoreDefaults),
    &QPushButton::clicked, this, &SpeedSettingsDialog::restoreDefaults);
  connect(buttons->button(QDialogButtonBox::Ok), &QPushButton::clicked,
    this, [this] {
      if (apply()) {
        accept();
      }
    });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);

  connect(normal_, &QSpinBox::valueChanged,
    this, &SpeedSettingsDialog::updateSummary);
  connect(slow_, &QSpinBox::valueChanged,
    this, &SpeedSettingsDialog::updateSummary);
  connect(fast_, &QSpinBox::valueChanged,
    this, &SpeedSettingsDialog::updateSummary);
  setSettings(current);
}

void SpeedSettingsDialog::setSettingsSink(SettingsSink sink)
{
  settingsSink_ = std::move(sink);
}

EmulationSpeedConfiguration SpeedSettingsDialog::settings() const
{
  return {
    .normalPercent = static_cast<std::uint32_t>(normal_->value()),
    .slowMotionPercent = static_cast<std::uint32_t>(slow_->value()),
    .fastForwardPercent = static_cast<std::uint32_t>(fast_->value()),
  };
}

void SpeedSettingsDialog::setSettings(
  const EmulationSpeedConfiguration& settings)
{
  if (!validateEmulationSpeedConfiguration(settings)) {
    return;
  }
  normal_->setValue(static_cast<int>(settings.normalPercent));
  slow_->setValue(static_cast<int>(settings.slowMotionPercent));
  fast_->setValue(static_cast<int>(settings.fastForwardPercent));
  error_->hide();
  updateSummary();
}

bool SpeedSettingsDialog::apply()
{
  const auto value = settings();
  if (!validateEmulationSpeedConfiguration(value)) {
    error_->setText(tr("The emulation speed settings are outside safe limits."));
    error_->show();
    return false;
  }
  const auto result = settingsSink_ ? settingsSink_(value) : PersistenceStatus{};
  if (!result) {
    error_->setText(QString::fromStdString(result.message));
    error_->show();
    return false;
  }
  error_->hide();
  return true;
}

void SpeedSettingsDialog::restoreDefaults()
{
  setSettings(settings::defaultSpeedSettings());
}

void SpeedSettingsDialog::updateSummary()
{
  summary_->setText(
    tr("Normal play targets %1%. Slow motion targets %2%; fast forward targets "
       "%3%. Audio playback is paused whenever the effective speed is not "
       "100%, preventing buffer drift and distorted output.")
      .arg(normal_->value()).arg(slow_->value()).arg(fast_->value()));
}

} // namespace genplusgx::ui
