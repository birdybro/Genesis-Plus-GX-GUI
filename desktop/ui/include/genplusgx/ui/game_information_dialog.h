#pragma once

#include "genplusgx/library/game_metadata.h"

#include <QDialog>

namespace genplusgx::ui {

class GameInformationDialog final : public QDialog {
  Q_OBJECT

public:
  explicit GameInformationDialog(
    const library::GameMetadata& metadata,
    QWidget* parent = nullptr);

  void setMetadata(const library::GameMetadata& metadata);
};

} // namespace genplusgx::ui
