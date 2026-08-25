#pragma once

#include <QDialog>

namespace genplusgx::ui {

class AboutDialog final : public QDialog {
  Q_OBJECT

public:
  explicit AboutDialog(QWidget* parent = nullptr);
};

} // namespace genplusgx::ui
