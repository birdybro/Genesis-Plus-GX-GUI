#include "genplusgx/ui/achievements_dialog.h"

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

AchievementsDialog::AchievementsDialog(QWidget* parent) : QDialog(parent)
{
  setObjectName(QStringLiteral("achievementsDialog"));
  setWindowTitle(tr("RetroAchievements"));
  setModal(false);
  resize(760, 620);

  auto* root = new QVBoxLayout(this);
  auto* notice = new QLabel(tr(
    "Connect your own RetroAchievements account. Credentials are sent only "
    "to RetroAchievements over HTTPS; the returned token is kept in the "
    "operating-system credential store."), this);
  notice->setObjectName(QStringLiteral("achievementsPrivacyLabel"));
  notice->setWordWrap(true);
  root->addWidget(notice);

  auto* account = new QGroupBox(tr("Account and mode"), this);
  account->setObjectName(QStringLiteral("achievementsAccountGroup"));
  auto* form = new QFormLayout(account);
  enabled_ = new QCheckBox(tr("Enable RetroAchievements"), account);
  enabled_->setObjectName(QStringLiteral("achievementsEnabledCheck"));
  form->addRow(enabled_);
  username_ = new QLineEdit(account);
  username_->setObjectName(QStringLiteral("achievementsUsernameEdit"));
  username_->setMaxLength(static_cast<int>(achievements::maximumUsernameBytes));
  username_->setAccessibleName(tr("RetroAchievements username"));
  form->addRow(tr("Username:"), username_);
  password_ = new QLineEdit(account);
  password_->setObjectName(QStringLiteral("achievementsPasswordEdit"));
  password_->setEchoMode(QLineEdit::Password);
  password_->setMaxLength(256);
  password_->setAccessibleName(tr("RetroAchievements password"));
  form->addRow(tr("Password:"), password_);
  hardcore_ = new QCheckBox(tr("Hardcore Mode"), account);
  hardcore_->setObjectName(QStringLiteral("achievementsHardcoreCheck"));
  hardcore_->setToolTip(tr(
    "Disables save states, rewind, run-ahead, slow motion, cheats, frame "
    "advance, netplay, and debugger access while active."));
  form->addRow(hardcore_);
  unofficial_ = new QCheckBox(tr("Unofficial achievements"), account);
  unofficial_->setObjectName(QStringLiteral("achievementsUnofficialCheck"));
  form->addRow(unofficial_);
  encore_ = new QCheckBox(tr("Encore mode"), account);
  encore_->setObjectName(QStringLiteral("achievementsEncoreCheck"));
  form->addRow(encore_);
  notifications_ = new QCheckBox(tr("Show achievement notifications"), account);
  notifications_->setObjectName(QStringLiteral("achievementsNotificationsCheck"));
  form->addRow(notifications_);
  root->addWidget(account);

  connection_ = new QLabel(tr("Disabled"), this);
  connection_->setObjectName(QStringLiteral("achievementsConnectionLabel"));
  connection_->setAccessibleName(tr("RetroAchievements connection status"));
  score_ = new QLabel(tr("Score: —"), this);
  score_->setObjectName(QStringLiteral("achievementsScoreLabel"));
  game_ = new QLabel(tr("Game: —"), this);
  game_->setObjectName(QStringLiteral("achievementsGameLabel"));
  presence_ = new QLabel(this);
  presence_->setObjectName(QStringLiteral("achievementsPresenceLabel"));
  presence_->setWordWrap(true);
  validation_ = new QLabel(this);
  validation_->setObjectName(QStringLiteral("achievementsValidationLabel"));
  validation_->setWordWrap(true);
  root->addWidget(connection_);
  root->addWidget(score_);
  root->addWidget(game_);
  root->addWidget(presence_);
  root->addWidget(validation_);

  achievements_ = new QTableWidget(this);
  achievements_->setObjectName(QStringLiteral("achievementsTable"));
  achievements_->setColumnCount(4);
  achievements_->setHorizontalHeaderLabels({
    tr("Achievement"), tr("Description"), tr("Points"), tr("Progress")});
  achievements_->horizontalHeader()->setStretchLastSection(true);
  achievements_->horizontalHeader()->setSectionResizeMode(
    0, QHeaderView::ResizeToContents);
  achievements_->horizontalHeader()->setSectionResizeMode(
    1, QHeaderView::Stretch);
  achievements_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  achievements_->setSelectionBehavior(QAbstractItemView::SelectRows);
  achievements_->setAlternatingRowColors(true);
  root->addWidget(achievements_, 1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  buttons->setObjectName(QStringLiteral("achievementsButtonBox"));
  apply_ = buttons->addButton(tr("Apply"), QDialogButtonBox::ApplyRole);
  apply_->setObjectName(QStringLiteral("achievementsApplyButton"));
  signIn_ = buttons->addButton(tr("Sign In"), QDialogButtonBox::ActionRole);
  signIn_->setObjectName(QStringLiteral("achievementsSignInButton"));
  signOut_ = buttons->addButton(tr("Sign Out"), QDialogButtonBox::ActionRole);
  signOut_->setObjectName(QStringLiteral("achievementsSignOutButton"));
  root->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
  connect(apply_, &QPushButton::clicked, this, &AchievementsDialog::apply);
  connect(signIn_, &QPushButton::clicked, this, &AchievementsDialog::signIn);
  connect(signOut_, &QPushButton::clicked, this, [this] {
    password_->clear();
    if (logoutSink_) {
      logoutSink_();
    }
  });
  connect(enabled_, &QCheckBox::toggled,
    this, &AchievementsDialog::refreshControls);
  refreshControls();
}

void AchievementsDialog::setSettings(achievements::Settings settings)
{
  settings_ = std::move(settings);
  enabled_->setChecked(settings_.enabled);
  hardcore_->setChecked(settings_.hardcore);
  unofficial_->setChecked(settings_.unofficial);
  encore_->setChecked(settings_.encore);
  notifications_->setChecked(settings_.notifications);
  username_->setText(QString::fromStdString(settings_.username));
  refreshControls();
}

void AchievementsDialog::setSnapshot(
  const achievements::Snapshot& snapshot)
{
  snapshot_ = snapshot;
  connection_->setText(tr("Status: %1%2")
    .arg(QString::fromLatin1(achievements::connectionStateName(snapshot.state)))
    .arg(snapshot.displayName.empty()
      ? QString{} : tr(" — %1").arg(QString::fromStdString(snapshot.displayName))));
  score_->setText(tr("Score: %1 hardcore, %2 softcore")
    .arg(snapshot.userScore).arg(snapshot.userSoftcoreScore));
  game_->setText(snapshot.gameTitle.empty()
    ? tr("Game: no recognized game")
    : tr("Game: %1 — %2/%3 achievements, %4/%5 points")
        .arg(QString::fromStdString(snapshot.gameTitle))
        .arg(snapshot.unlockedCount).arg(snapshot.achievementCount)
        .arg(snapshot.unlockedPoints).arg(snapshot.totalPoints));
  presence_->setText(QString::fromStdString(snapshot.richPresence));
  validation_->setText(QString::fromStdString(snapshot.detail));
  achievements_->setRowCount(static_cast<int>(snapshot.achievements.size()));
  for (int row = 0; row < achievements_->rowCount(); ++row) {
    const auto& item = snapshot.achievements[static_cast<std::size_t>(row)];
    achievements_->setItem(row, 0,
      new QTableWidgetItem(QString::fromStdString(item.title)));
    achievements_->setItem(row, 1,
      new QTableWidgetItem(QString::fromStdString(item.description)));
    achievements_->setItem(row, 2,
      new QTableWidgetItem(QString::number(item.points)));
    auto progress = QString::fromStdString(item.measuredProgress);
    if (progress.isEmpty()) {
      progress = item.unlockedHardcore ? tr("Hardcore unlocked")
        : item.unlockedSoftcore ? tr("Unlocked") : tr("Locked");
    }
    achievements_->setItem(row, 3, new QTableWidgetItem(progress));
  }
  refreshControls();
}

void AchievementsDialog::setSettingsSink(SettingsSink sink)
{
  settingsSink_ = std::move(sink);
  refreshControls();
}

void AchievementsDialog::setLoginSink(LoginSink sink)
{
  loginSink_ = std::move(sink);
  refreshControls();
}

void AchievementsDialog::setLogoutSink(LogoutSink sink)
{
  logoutSink_ = std::move(sink);
  refreshControls();
}

void AchievementsDialog::showError(const std::string& detail)
{
  validation_->setText(QString::fromStdString(detail));
}

bool AchievementsDialog::apply()
{
  achievements::Settings candidate{
    .enabled = enabled_->isChecked(),
    .hardcore = hardcore_->isChecked(),
    .unofficial = unofficial_->isChecked(),
    .encore = encore_->isChecked(),
    .notifications = notifications_->isChecked(),
    .username = username_->text().trimmed().toStdString(),
  };
  if (!achievements::validateSettings(candidate)) {
    showError("Enter a valid RetroAchievements username (letters, digits, '.', '_' or '-').");
    return false;
  }
  if (settingsSink_) {
    const auto saved = settingsSink_(candidate);
    if (!saved) {
      showError(saved.message);
      return false;
    }
  }
  settings_ = std::move(candidate);
  validation_->clear();
  refreshControls();
  return true;
}

void AchievementsDialog::signIn()
{
  if (!apply()) {
    return;
  }
  if (!settings_.enabled || !loginSink_ || password_->text().isEmpty()) {
    showError("Enable RetroAchievements and enter a username and password.");
    return;
  }
  auto password = password_->text().toStdString();
  password_->clear();
  loginSink_(settings_.username, std::move(password));
  validation_->setText(tr("Signing in…"));
}

void AchievementsDialog::refreshControls()
{
  const bool enabled = enabled_->isChecked();
  username_->setEnabled(enabled && !snapshot_.authenticated);
  password_->setEnabled(enabled && !snapshot_.authenticated);
  hardcore_->setEnabled(enabled);
  unofficial_->setEnabled(enabled);
  encore_->setEnabled(enabled);
  notifications_->setEnabled(enabled);
  apply_->setEnabled(static_cast<bool>(settingsSink_));
  signIn_->setEnabled(enabled && !snapshot_.authenticated &&
    static_cast<bool>(loginSink_));
  signOut_->setEnabled(snapshot_.authenticated && static_cast<bool>(logoutSink_));
}

} // namespace genplusgx::ui
