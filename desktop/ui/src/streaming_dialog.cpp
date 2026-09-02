#include "genplusgx/ui/streaming_dialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <utility>

namespace genplusgx::ui {

StreamingDialog::StreamingDialog(QWidget* parent)
  : QDialog(parent)
{
  setObjectName(QStringLiteral("streamingDialog"));
  setWindowTitle(tr("Local A/V Streaming Output"));
  setModal(false);
  auto* root = new QVBoxLayout(this);
  auto* explanation = new QLabel(tr(
    "Broadcast native RGB565 video and stereo S16 audio through the documented "
    "GPGX-AV/1 protocol. The server listens on 127.0.0.1 only for a compatible "
    "local capture or streaming client."), this);
  explanation->setObjectName(QStringLiteral("streamingExplanation"));
  explanation->setWordWrap(true);
  root->addWidget(explanation);
  auto* form = new QFormLayout;
  port_ = new QSpinBox(this);
  port_->setObjectName(QStringLiteral("streamingPort"));
  port_->setAccessibleName(tr("Loopback streaming port"));
  port_->setRange(1'024, 65'535);
  port_->setValue(capture::defaultStreamingPort);
  clients_ = new QSpinBox(this);
  clients_->setObjectName(QStringLiteral("streamingMaximumClients"));
  clients_->setAccessibleName(tr("Maximum streaming clients"));
  clients_->setRange(1, 4);
  clients_->setValue(2);
  form->addRow(tr("Loopback port:"), port_);
  form->addRow(tr("Maximum clients:"), clients_);
  root->addLayout(form);
  state_ = new QLabel(this);
  state_->setObjectName(QStringLiteral("streamingState"));
  state_->setAccessibleName(tr("Streaming state"));
  metricsLabel_ = new QLabel(this);
  metricsLabel_->setObjectName(QStringLiteral("streamingMetrics"));
  metricsLabel_->setAccessibleName(tr("Streaming metrics"));
  metricsLabel_->setTextInteractionFlags(Qt::TextSelectableByKeyboard |
    Qt::TextSelectableByMouse);
  root->addWidget(state_);
  root->addWidget(metricsLabel_);
  toggle_ = new QPushButton(this);
  toggle_->setObjectName(QStringLiteral("streamingToggle"));
  toggle_->setAccessibleDescription(
    tr("Start or stop the loopback-only native audio and video stream."));
  root->addWidget(toggle_);
  auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
  close->setObjectName(QStringLiteral("streamingClose"));
  root->addWidget(close);
  connect(toggle_, &QPushButton::clicked, this, &StreamingDialog::requestToggle);
  connect(close, &QDialogButtonBox::rejected, this, &QDialog::close);
  refresh();
}

void StreamingDialog::setRequestSink(RequestSink sink)
{
  requestSink_ = std::move(sink);
  refresh();
}

void StreamingDialog::setMetrics(capture::StreamingMetrics metrics)
{
  if (metrics.active != metrics_.active) {
    requestPending_ = false;
  }
  metrics_ = metrics;
  if (metrics_.port != 0U) {
    port_->setValue(metrics_.port);
  }
  refresh();
}

void StreamingDialog::showFailure(const std::string& detail)
{
  requestPending_ = false;
  QMessageBox::warning(this, tr("Local Streaming Error"),
    QString::fromStdString(detail));
  refresh();
}

void StreamingDialog::requestToggle()
{
  if (!requestSink_) {
    return;
  }
  const capture::StreamingConfiguration configuration{
    .port = static_cast<std::uint16_t>(port_->value()),
    .maximumClients = static_cast<std::size_t>(clients_->value()),
  };
  const auto result = requestSink_(!metrics_.active, configuration);
  if (!result) {
    showFailure(result.message);
    return;
  }
  requestPending_ = true;
  refresh();
}

void StreamingDialog::refresh()
{
  state_->setText(metrics_.active
    ? tr("Streaming on 127.0.0.1:%1").arg(metrics_.port)
    : tr("Streaming is stopped"));
  metricsLabel_->setText(tr(
    "Clients: %1  •  Queue: %2/%3  •  Sent: %4 frames / %5 bytes  •  "
    "Dropped: %6  •  Slow clients removed: %7")
      .arg(metrics_.connectedClients).arg(metrics_.queueDepth)
      .arg(metrics_.queueCapacity).arg(metrics_.broadcastFrames)
      .arg(metrics_.bytesSent).arg(metrics_.droppedFrames)
      .arg(metrics_.disconnectedSlowClients));
  port_->setEnabled(!metrics_.active);
  clients_->setEnabled(!metrics_.active);
  toggle_->setText(requestPending_
    ? (metrics_.active ? tr("Stopping…") : tr("Starting…"))
    : (metrics_.active ? tr("Stop streaming") : tr("Start streaming")));
  toggle_->setEnabled(static_cast<bool>(requestSink_) && !requestPending_);
}

} // namespace genplusgx::ui
