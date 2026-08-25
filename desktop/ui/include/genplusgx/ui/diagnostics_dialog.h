#pragma once

#include "genplusgx/diagnostics/diagnostics.h"

#include <QDialog>

class QPlainTextEdit;

namespace genplusgx::ui {

class DiagnosticsDialog final : public QDialog {
  Q_OBJECT

public:
  explicit DiagnosticsDialog(
    diagnostics::DiagnosticsSnapshot snapshot, QWidget* parent = nullptr);

  void setSnapshot(diagnostics::DiagnosticsSnapshot snapshot);
  [[nodiscard]] const diagnostics::DiagnosticsSnapshot& snapshot() const noexcept;
  [[nodiscard]] QString report() const;

private:
  void refreshReport();
  void copyToClipboard();

  diagnostics::DiagnosticsSnapshot snapshot_;
  QPlainTextEdit* report_{nullptr};
};

} // namespace genplusgx::ui
