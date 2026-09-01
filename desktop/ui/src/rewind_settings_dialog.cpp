#include "genplusgx/ui/rewind_settings_dialog.h"

#include "genplusgx/settings/rewind_settings.h"

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
namespace {

constexpr std::size_t bytesPerMebibyte = 1024U * 1024U;
constexpr double nominalFramesPerSecond = 60.0;

} // namespace

RewindSettingsDialog::RewindSettingsDialog(
  RewindConfiguration current,
  QWidget* parent)
  : QDialog(parent)
{
  setObjectName(QStringLiteral("rewindSettingsDialog"));
  setWindowTitle(tr("Rewind Settings"));
  setModal(true);
  setMinimumWidth(520);

  auto* root = new QVBoxLayout(this);
  auto* introduction = new QLabel(
    tr("Rewind records bounded in-memory core states. Holding the rewind hotkey "
       "moves backward at the normal display rate; audio is muted and discarded "
       "until forward play resumes."), this);
  introduction->setObjectName(QStringLiteral("rewindIntroductionLabel"));
  introduction->setWordWrap(true);
  root->addWidget(introduction);

  auto* group = new QGroupBox(tr("History"), this);
  group->setObjectName(QStringLiteral("rewindHistoryGroup"));
  auto* form = new QFormLayout(group);
  enabled_ = new QCheckBox(tr("Enable rewind history"), group);
  enabled_->setObjectName(QStringLiteral("rewindEnabledCheckBox"));
  form->addRow(QString{}, enabled_);
  interval_ = new QSpinBox(group);
  interval_->setObjectName(QStringLiteral("rewindCaptureIntervalSpinBox"));
  interval_->setRange(1, 60);
  interval_->setSuffix(tr(" frames"));
  interval_->setAccessibleName(tr("Rewind capture interval in frames"));
  form->addRow(tr("Capture every:"), interval_);
  memory_ = new QSpinBox(group);
  memory_->setObjectName(QStringLiteral("rewindMemoryLimitSpinBox"));
  memory_->setRange(16, 1024);
  memory_->setSingleStep(16);
  memory_->setSuffix(tr(" MiB"));
  memory_->setAccessibleName(tr("Maximum rewind memory in mebibytes"));
  form->addRow(tr("Memory limit:"), memory_);
  root->addWidget(group);

  summary_ = new QLabel(this);
  summary_->setObjectName(QStringLiteral("rewindCapacitySummaryLabel"));
  summary_->setWordWrap(true);
  root->addWidget(summary_);
  error_ = new QLabel(this);
  error_->setObjectName(QStringLiteral("rewindSettingsErrorLabel"));
  error_->setWordWrap(true);
  error_->setStyleSheet(QStringLiteral("color: palette(highlight);"));
  error_->hide();
  root->addWidget(error_);

  auto* buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
      QDialogButtonBox::Apply | QDialogButtonBox::RestoreDefaults,
    this);
  buttons->setObjectName(QStringLiteral("rewindSettingsButtonBox"));
  buttons->button(QDialogButtonBox::Ok)->setObjectName(
    QStringLiteral("okRewindSettingsButton"));
  buttons->button(QDialogButtonBox::Cancel)->setObjectName(
    QStringLiteral("cancelRewindSettingsButton"));
  buttons->button(QDialogButtonBox::Apply)->setObjectName(
    QStringLiteral("applyRewindSettingsButton"));
  buttons->button(QDialogButtonBox::RestoreDefaults)->setObjectName(
    QStringLiteral("restoreRewindDefaultsButton"));
  connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
    this, [this] { static_cast<void>(apply()); });
  connect(buttons->button(QDialogButtonBox::RestoreDefaults),
    &QPushButton::clicked, this, &RewindSettingsDialog::restoreDefaults);
  connect(buttons->button(QDialogButtonBox::Ok), &QPushButton::clicked,
    this, [this] {
      if (apply()) {
        accept();
      }
    });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);

  connect(enabled_, &QCheckBox::toggled,
    this, &RewindSettingsDialog::updateSummary);
  connect(interval_, &QSpinBox::valueChanged,
    this, &RewindSettingsDialog::updateSummary);
  connect(memory_, &QSpinBox::valueChanged,
    this, &RewindSettingsDialog::updateSummary);
  setSettings(current);
}

void RewindSettingsDialog::setSettingsSink(SettingsSink sink)
{
  settingsSink_ = std::move(sink);
}

RewindConfiguration RewindSettingsDialog::settings() const
{
  return {
    .enabled = enabled_->isChecked(),
    .captureIntervalFrames = static_cast<std::uint32_t>(interval_->value()),
    .memoryLimitBytes = static_cast<std::size_t>(memory_->value()) *
      bytesPerMebibyte,
  };
}

void RewindSettingsDialog::setSettings(const RewindConfiguration& settings)
{
  if (!validateRewindConfiguration(settings) ||
      settings.memoryLimitBytes % bytesPerMebibyte != 0U) {
    return;
  }
  enabled_->setChecked(settings.enabled);
  interval_->setValue(static_cast<int>(settings.captureIntervalFrames));
  memory_->setValue(static_cast<int>(settings.memoryLimitBytes / bytesPerMebibyte));
  error_->hide();
  updateSummary();
}

bool RewindSettingsDialog::apply()
{
  const auto value = settings();
  if (!validateRewindConfiguration(value)) {
    error_->setText(tr("The rewind settings are outside their safe limits."));
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

void RewindSettingsDialog::restoreDefaults()
{
  setSettings(settings::defaultRewindSettings());
}

void RewindSettingsDialog::updateSummary()
{
  interval_->setEnabled(enabled_->isChecked());
  memory_->setEnabled(enabled_->isChecked());
  if (!enabled_->isChecked()) {
    summary_->setText(tr("Rewind history is disabled and consumes no state memory."));
    return;
  }
  const auto seconds = static_cast<double>(memory_->value()) *
    static_cast<double>(interval_->value()) / nominalFramesPerSecond;
  summary_->setText(
    tr("The history is strictly capped at %1 MiB. With approximately 1 MiB "
       "Genesis Plus GX states, this stores about %2 seconds at 60 Hz; exact "
       "duration varies by system and state size.")
      .arg(memory_->value()).arg(seconds, 0, 'f', 1));
}

} // namespace genplusgx::ui
