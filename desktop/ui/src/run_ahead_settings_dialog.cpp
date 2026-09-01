#include "genplusgx/ui/run_ahead_settings_dialog.h"

#include "genplusgx/settings/run_ahead_settings.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <utility>

namespace genplusgx::ui {

RunAheadSettingsDialog::RunAheadSettingsDialog(
  RunAheadConfiguration current,
  QWidget* parent)
  : QDialog(parent)
{
  setObjectName(QStringLiteral("runAheadSettingsDialog"));
  setWindowTitle(tr("Run-Ahead Settings"));
  setModal(true);
  setMinimumWidth(540);

  auto* root = new QVBoxLayout(this);
  auto* introduction = new QLabel(
    tr("Run-ahead reduces perceived controller latency by rendering future "
       "frames speculatively, restoring the exact prior core state, then "
       "executing one authoritative frame with normal audio."), this);
  introduction->setObjectName(QStringLiteral("runAheadIntroductionLabel"));
  introduction->setWordWrap(true);
  root->addWidget(introduction);

  auto* group = new QGroupBox(tr("Speculation"), this);
  group->setObjectName(QStringLiteral("runAheadSpeculationGroup"));
  auto* form = new QFormLayout(group);
  enabled_ = new QCheckBox(tr("Enable run-ahead for cartridge games"), group);
  enabled_->setObjectName(QStringLiteral("runAheadEnabledCheckBox"));
  form->addRow(QString{}, enabled_);
  frames_ = new QSpinBox(group);
  frames_->setObjectName(QStringLiteral("runAheadFramesSpinBox"));
  frames_->setRange(
    static_cast<int>(minimumRunAheadFrames),
    static_cast<int>(maximumRunAheadFrames));
  frames_->setSuffix(tr(" frames"));
  frames_->setAccessibleName(tr("Number of speculative run-ahead frames"));
  form->addRow(tr("Frames ahead:"), frames_);
  root->addWidget(group);

  summary_ = new QLabel(this);
  summary_->setObjectName(QStringLiteral("runAheadBehaviorSummaryLabel"));
  summary_->setWordWrap(true);
  root->addWidget(summary_);
  error_ = new QLabel(this);
  error_->setObjectName(QStringLiteral("runAheadSettingsErrorLabel"));
  error_->setWordWrap(true);
  error_->setStyleSheet(QStringLiteral("color: palette(highlight);"));
  error_->hide();
  root->addWidget(error_);

  auto* buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
      QDialogButtonBox::Apply | QDialogButtonBox::RestoreDefaults,
    this);
  buttons->setObjectName(QStringLiteral("runAheadSettingsButtonBox"));
  buttons->button(QDialogButtonBox::Ok)->setObjectName(
    QStringLiteral("okRunAheadSettingsButton"));
  buttons->button(QDialogButtonBox::Cancel)->setObjectName(
    QStringLiteral("cancelRunAheadSettingsButton"));
  buttons->button(QDialogButtonBox::Apply)->setObjectName(
    QStringLiteral("applyRunAheadSettingsButton"));
  buttons->button(QDialogButtonBox::RestoreDefaults)->setObjectName(
    QStringLiteral("restoreRunAheadDefaultsButton"));
  connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
    this, [this] { static_cast<void>(apply()); });
  connect(buttons->button(QDialogButtonBox::RestoreDefaults),
    &QPushButton::clicked, this, &RunAheadSettingsDialog::restoreDefaults);
  connect(buttons->button(QDialogButtonBox::Ok), &QPushButton::clicked,
    this, [this] {
      if (apply()) {
        accept();
      }
    });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);

  connect(enabled_, &QCheckBox::toggled,
    this, &RunAheadSettingsDialog::updateSummary);
  connect(frames_, &QSpinBox::valueChanged,
    this, &RunAheadSettingsDialog::updateSummary);
  setSettings(current);
}

void RunAheadSettingsDialog::setSettingsSink(SettingsSink sink)
{
  settingsSink_ = std::move(sink);
}

RunAheadConfiguration RunAheadSettingsDialog::settings() const
{
  return {
    .enabled = enabled_->isChecked(),
    .frames = static_cast<std::uint32_t>(frames_->value()),
  };
}

void RunAheadSettingsDialog::setSettings(
  const RunAheadConfiguration& settings)
{
  if (!validateRunAheadConfiguration(settings)) {
    return;
  }
  enabled_->setChecked(settings.enabled);
  frames_->setValue(static_cast<int>(settings.frames));
  error_->hide();
  updateSummary();
}

bool RunAheadSettingsDialog::apply()
{
  const auto value = settings();
  if (!validateRunAheadConfiguration(value)) {
    error_->setText(tr("The run-ahead settings are outside safe limits."));
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

void RunAheadSettingsDialog::restoreDefaults()
{
  setSettings(settings::defaultRunAheadSettings());
}

void RunAheadSettingsDialog::updateSummary()
{
  frames_->setEnabled(enabled_->isChecked());
  if (!enabled_->isChecked()) {
    summary_->setText(tr("Run-ahead is disabled; Genesis Plus GX executes one authoritative frame per host frame."));
    return;
  }
  summary_->setText(
    tr("Each host frame executes %1 speculative frame(s), rolls back, and then "
       "executes one authoritative frame. Speculative audio is discarded. "
       "Run-ahead automatically suspends during rewind, slow motion, fast "
       "forward, Sega CD, and specialized-controller sessions.")
      .arg(frames_->value()));
}

} // namespace genplusgx::ui
