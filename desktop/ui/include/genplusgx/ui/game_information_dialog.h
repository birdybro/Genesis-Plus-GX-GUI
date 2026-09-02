#pragma once

#include "genplusgx/library/game_metadata.h"
#include "genplusgx/library/online_metadata.h"

#include <QDialog>

#include <optional>

namespace genplusgx::ui {

class GameInformationDialog final : public QDialog {
  Q_OBJECT

public:
  explicit GameInformationDialog(
    const library::GameMetadata& metadata,
    QWidget* parent = nullptr);

  void setMetadata(const library::GameMetadata& metadata);
  void setOnlineMetadata(
    const std::optional<library::OnlineMetadataRecord>& metadata);
};

} // namespace genplusgx::ui
