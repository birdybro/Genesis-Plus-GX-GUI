#include "genplusgx/ui/netplay_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <utility>

namespace genplusgx::ui {

NetplayDialog::NetplayDialog(QWidget* parent) : QDialog(parent)
{
  setObjectName(QStringLiteral("netplayDialog"));
  setWindowTitle(tr("Netplay"));
  setModal(false);
  resize(500, 360);

  auto* root = new QVBoxLayout(this);
  auto* explanation = new QLabel(tr(
    "Play the currently loaded game with one authenticated peer. Both sides "
    "must use the same game, core build, deterministic settings, delay, and "
    "rollback window. Starting a session resets the game."), this);
  explanation->setObjectName(QStringLiteral("netplayExplanationLabel"));
  explanation->setWordWrap(true);
  root->addWidget(explanation);

  auto* form = new QFormLayout;
  mode_ = new QComboBox(this);
  mode_->setObjectName(QStringLiteral("netplayModeCombo"));
  mode_->addItem(tr("Host (Player 1)"));
  mode_->addItem(tr("Join (Player 2)"));
  form->addRow(tr("Mode:"), mode_);

  host_ = new QLineEdit(QStringLiteral("127.0.0.1"), this);
  host_->setObjectName(QStringLiteral("netplayHostEdit"));
  host_->setMaxLength(255);
  host_->setAccessibleName(tr("Netplay host name or address"));
  form->addRow(tr("Host:"), host_);

  port_ = new QSpinBox(this);
  port_->setObjectName(QStringLiteral("netplayPortSpin"));
  port_->setRange(1, 65'535);
  port_->setValue(netplay::defaultPort);
  form->addRow(tr("Port:"), port_);

  code_ = new QLineEdit(this);
  code_->setObjectName(QStringLiteral("netplaySessionCodeEdit"));
  code_->setEchoMode(QLineEdit::Password);
  code_->setMaxLength(128);
  code_->setPlaceholderText(tr("At least 6 characters; share out of band"));
  code_->setAccessibleName(tr("Private netplay session code"));
  form->addRow(tr("Session code:"), code_);

  delay_ = new QSpinBox(this);
  delay_->setObjectName(QStringLiteral("netplayInputDelaySpin"));
  delay_->setRange(0, static_cast<int>(netplay::maximumInputDelayFrames));
  delay_->setValue(2);
  delay_->setSuffix(tr(" frames"));
  form->addRow(tr("Input delay:"), delay_);

  rollback_ = new QSpinBox(this);
  rollback_->setObjectName(QStringLiteral("netplayRollbackSpin"));
  rollback_->setRange(1, static_cast<int>(netplay::maximumRollbackFrames));
  rollback_->setValue(8);
  rollback_->setSuffix(tr(" frames"));
  form->addRow(tr("Rollback window:"), rollback_);
  root->addLayout(form);

  auto* privacy = new QLabel(tr(
    "The session code authenticates and integrity-protects peer traffic, but "
    "does not encrypt gameplay. The code is never saved or written to logs."), this);
  privacy->setObjectName(QStringLiteral("netplayPrivacyLabel"));
  privacy->setWordWrap(true);
  root->addWidget(privacy);

  status_ = new QLabel(tr("Disconnected"), this);
  status_->setObjectName(QStringLiteral("netplayStatusLabel"));
  status_->setAccessibleName(tr("Netplay connection status"));
  root->addWidget(status_);
  validation_ = new QLabel(this);
  validation_->setObjectName(QStringLiteral("netplayValidationLabel"));
  validation_->setWordWrap(true);
  validation_->setStyleSheet(QStringLiteral("color: palette(highlight);"));
  root->addWidget(validation_);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  buttons->setObjectName(QStringLiteral("netplayButtonBox"));
  connect_ = buttons->addButton(tr("Host Session"), QDialogButtonBox::ActionRole);
  connect_->setObjectName(QStringLiteral("netplayConnectButton"));
  disconnect_ = buttons->addButton(tr("Disconnect"), QDialogButtonBox::ActionRole);
  disconnect_->setObjectName(QStringLiteral("netplayDisconnectButton"));
  root->addWidget(buttons);

  connect(mode_, &QComboBox::currentIndexChanged,
    this, &NetplayDialog::updateMode);
  connect(connect_, &QPushButton::clicked, this, &NetplayDialog::submit);
  connect(disconnect_, &QPushButton::clicked,
    this, &NetplayDialog::disconnectSession);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
  updateMode();
  refreshControls();
}

void NetplayDialog::setRequestSink(RequestSink sink)
{
  requestSink_ = std::move(sink);
  refreshControls();
}

void NetplayDialog::setGameReady(bool ready)
{
  gameReady_ = ready;
  refreshControls();
}

void NetplayDialog::setSessionState(
  netplay::NetplaySessionState state,
  const std::string& detail)
{
  state_ = state;
  const auto defaultText = [state] {
    switch (state) {
      case netplay::NetplaySessionState::disconnected: return tr("Disconnected");
      case netplay::NetplaySessionState::listening: return tr("Listening for one peer…");
      case netplay::NetplaySessionState::connecting: return tr("Connecting…");
      case netplay::NetplaySessionState::authenticating: return tr("Authenticating peer…");
      case netplay::NetplaySessionState::connected: return tr("Connected — netplay active");
    }
    return tr("Disconnected");
  }();
  status_->setText(detail.empty()
    ? defaultText : QString::fromStdString(detail));
  if (state == netplay::NetplaySessionState::connected) {
    validation_->clear();
  }
  refreshControls();
}

void NetplayDialog::showError(const std::string& detail)
{
  validation_->setText(QString::fromStdString(detail));
}

void NetplayDialog::updateMode()
{
  const bool joining = mode_->currentIndex() == 1;
  host_->setEnabled(joining && state_ == netplay::NetplaySessionState::disconnected);
  connect_->setText(joining ? tr("Join Session") : tr("Host Session"));
}

void NetplayDialog::submit()
{
  validation_->clear();
  if (!requestSink_) {
    showError("The netplay service is unavailable.");
    return;
  }
  const auto code = code_->text().toStdString();
  if (code.size() < 6U) {
    showError("Enter a private session code containing at least 6 characters.");
    code_->setFocus();
    return;
  }
  NetplayUiRequest request{
    .operation = mode_->currentIndex() == 0
      ? NetplayUiOperation::host : NetplayUiOperation::join,
    .host = host_->text().trimmed().toStdString(),
    .port = static_cast<std::uint16_t>(port_->value()),
    .sessionCode = code,
    .inputDelayFrames = static_cast<std::uint32_t>(delay_->value()),
    .rollbackFrames = static_cast<std::uint32_t>(rollback_->value()),
  };
  code_->clear();
  const auto status = requestSink_(std::move(request));
  if (!status) {
    showError(status.message);
  }
}

void NetplayDialog::disconnectSession()
{
  if (!requestSink_) {
    return;
  }
  NetplayUiRequest request;
  request.operation = NetplayUiOperation::disconnect;
  const auto status = requestSink_(std::move(request));
  if (!status) {
    showError(status.message);
  }
}

void NetplayDialog::refreshControls()
{
  const bool disconnected = state_ == netplay::NetplaySessionState::disconnected;
  mode_->setEnabled(disconnected);
  port_->setEnabled(disconnected);
  code_->setEnabled(disconnected);
  delay_->setEnabled(disconnected);
  rollback_->setEnabled(disconnected);
  connect_->setEnabled(disconnected && gameReady_ && static_cast<bool>(requestSink_));
  disconnect_->setEnabled(!disconnected && static_cast<bool>(requestSink_));
  updateMode();
}

} // namespace genplusgx::ui
