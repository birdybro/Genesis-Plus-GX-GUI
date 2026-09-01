#pragma once

#include "genplusgx/game_archive.h"

#include <QDialog>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class QListWidget;

namespace genplusgx::ui {

class ArchiveEntryDialog final : public QDialog {
public:
  ArchiveEntryDialog(
    const std::filesystem::path& archivePath,
    const std::vector<ArchivedGameEntry>& entries,
    QWidget* parent = nullptr);

  [[nodiscard]] std::optional<std::string> selectedEntry() const;

private:
  QListWidget* entries_{nullptr};
};

} // namespace genplusgx::ui
