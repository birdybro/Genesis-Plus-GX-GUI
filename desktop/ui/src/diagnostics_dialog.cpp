#include "genplusgx/ui/diagnostics_dialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QVBoxLayout>

#include <utility>

namespace genplusgx::ui {

DiagnosticsDialog::DiagnosticsDialog(
  diagnostics::DiagnosticsSnapshot snapshot, QWidget* parent)
    : QDialog(parent), snapshot_(std::move(snapshot))
{
  setObjectName(QStringLiteral("diagnosticsDialog"));
  setWindowTitle(tr("Log and Diagnostics"));
  setModal(false);
  resize(720, 620);

  auto* root = new QVBoxLayout(this);
  auto* introduction =
    new QLabel(tr("This report contains build and runtime status. Filesystem paths and "
                  "credential-like values are omitted or redacted."),
      this);
  introduction->setObjectName(QStringLiteral("diagnosticsPrivacyLabel"));
  introduction->setAccessibleName(tr("Diagnostics privacy notice"));
  introduction->setWordWrap(true);
  root->addWidget(introduction);

  report_ = new QPlainTextEdit(this);
  report_->setObjectName(QStringLiteral("diagnosticsReportEdit"));
  report_->setAccessibleName(tr("Application diagnostics report"));
  report_->setAccessibleDescription(
    tr("Read-only build, renderer, audio, game, BIOS, and logging status."));
  report_->setReadOnly(true);
  report_->setLineWrapMode(QPlainTextEdit::NoWrap);
  root->addWidget(report_, 1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  buttons->setObjectName(QStringLiteral("diagnosticsButtonBox"));
  auto* copy =
    buttons->addButton(tr("&Copy Diagnostics"), QDialogButtonBox::ActionRole);
  copy->setObjectName(QStringLiteral("copyDiagnosticsButton"));
  copy->setAccessibleDescription(
    tr("Copy the privacy-filtered diagnostics report to the clipboard."));
  buttons->button(QDialogButtonBox::Close)
    ->setObjectName(QStringLiteral("closeDiagnosticsButton"));
  connect(copy, &QPushButton::clicked, this, &DiagnosticsDialog::copyToClipboard);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);

  QWidget::setTabOrder(report_, copy);
  QWidget::setTabOrder(copy, buttons->button(QDialogButtonBox::Close));
  refreshReport();
}

void DiagnosticsDialog::setSnapshot(diagnostics::DiagnosticsSnapshot snapshot)
{
  snapshot_ = std::move(snapshot);
  refreshReport();
}

const diagnostics::DiagnosticsSnapshot& DiagnosticsDialog::snapshot() const noexcept
{
  return snapshot_;
}

QString DiagnosticsDialog::report() const { return report_->toPlainText(); }

void DiagnosticsDialog::refreshReport()
{
  const auto text = diagnostics::formatDiagnostics(snapshot_);
  report_->setPlainText(
    QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())));
  report_->moveCursor(QTextCursor::Start);
}

void DiagnosticsDialog::copyToClipboard()
{
  QApplication::clipboard()->setText(report());
}

} // namespace genplusgx::ui
