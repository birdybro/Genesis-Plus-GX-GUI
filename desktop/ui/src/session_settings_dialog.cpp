#include "genplusgx/ui/session_settings_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace genplusgx::ui {

SessionSettingsDialog::SessionSettingsDialog(
  bool resumeOnLaunch,
  QWidget* parent)
  : QDialog(parent)
{
  setObjectName(QStringLiteral("sessionSettingsDialog"));
  setWindowTitle(tr("Session Settings"));
  setModal(true);
  setMinimumWidth(520);

  auto* root = new QVBoxLayout(this);
  auto* introduction = new QLabel(
    tr("Session resume creates a dedicated, game-identified checkpoint during "
       "clean shutdown and restores it the next time the application starts."),
    this);
  introduction->setObjectName(QStringLiteral("sessionSettingsIntroductionLabel"));
  introduction->setWordWrap(true);
  root->addWidget(introduction);

  resumeOnLaunch_ = new QCheckBox(
    tr("Automatically resume the last running game on launch"), this);
  resumeOnLaunch_->setObjectName(QStringLiteral("resumeOnLaunchCheckBox"));
  resumeOnLaunch_->setAccessibleDescription(
    tr("Save a bounded checkpoint on clean exit and restore it on the next launch."));
  root->addWidget(resumeOnLaunch_);

  detail_ = new QLabel(this);
  detail_->setObjectName(QStringLiteral("sessionResumeDetailLabel"));
  detail_->setWordWrap(true);
  root->addWidget(detail_);
  error_ = new QLabel(this);
  error_->setObjectName(QStringLiteral("sessionSettingsErrorLabel"));
  error_->setWordWrap(true);
  error_->setStyleSheet(QStringLiteral("color: palette(highlight);"));
  error_->hide();
  root->addWidget(error_);

  auto* buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
      QDialogButtonBox::Apply | QDialogButtonBox::RestoreDefaults,
    this);
  buttons->setObjectName(QStringLiteral("sessionSettingsButtonBox"));
  buttons->button(QDialogButtonBox::Ok)->setObjectName(
    QStringLiteral("okSessionSettingsButton"));
  buttons->button(QDialogButtonBox::Cancel)->setObjectName(
    QStringLiteral("cancelSessionSettingsButton"));
  buttons->button(QDialogButtonBox::Apply)->setObjectName(
    QStringLiteral("applySessionSettingsButton"));
  buttons->button(QDialogButtonBox::RestoreDefaults)->setObjectName(
    QStringLiteral("restoreSessionDefaultsButton"));
  connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
    this, [this] { static_cast<void>(apply()); });
  connect(buttons->button(QDialogButtonBox::RestoreDefaults),
    &QPushButton::clicked, this, [this] { setResumeOnLaunch(false); });
  connect(buttons->button(QDialogButtonBox::Ok), &QPushButton::clicked,
    this, [this] {
      if (apply()) {
        accept();
      }
    });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);

  connect(resumeOnLaunch_, &QCheckBox::toggled, this, [this](bool enabled) {
    detail_->setText(enabled
      ? tr("A checkpoint will be saved only after a clean shutdown. Explicit "
           "command-line games always take precedence.")
      : tr("The application will open with no game unless one is supplied."));
  });
  setResumeOnLaunch(resumeOnLaunch);
}

void SessionSettingsDialog::setSettingsSink(SettingsSink sink)
{
  settingsSink_ = std::move(sink);
}

bool SessionSettingsDialog::resumeOnLaunch() const noexcept
{
  return resumeOnLaunch_->isChecked();
}

void SessionSettingsDialog::setResumeOnLaunch(bool enabled)
{
  resumeOnLaunch_->setChecked(enabled);
  error_->hide();
}

bool SessionSettingsDialog::apply()
{
  const auto status = settingsSink_
    ? settingsSink_(resumeOnLaunch()) : PersistenceStatus{};
  if (!status) {
    error_->setText(QString::fromStdString(status.message));
    error_->show();
    return false;
  }
  error_->hide();
  return true;
}

} // namespace genplusgx::ui
