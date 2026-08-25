#pragma once

#include "genplusgx/library/game_library_database.h"
#include "genplusgx/library/game_library_scanner.h"
#include "genplusgx/ui/dialog_service.h"

#include <QDialog>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSortFilterProxyModel;
class QStandardItemModel;
class QTableView;

namespace genplusgx::ui {

struct GameLibraryActions final {
  std::function<void(const std::filesystem::path&, bool)> addDirectory;
  std::function<void(std::int64_t)> removeDirectory;
  std::function<void(std::int64_t, bool)> updateDirectory;
  std::function<void(std::int64_t)> scanDirectory;
  std::function<void(std::int64_t, bool)> setFavorite;
  std::function<void(std::int64_t, const std::filesystem::path&)> setArtwork;
  std::function<void(std::int64_t, const std::filesystem::path&)> launchGame;
};

class GameLibraryDialog final : public QDialog {
  Q_OBJECT

public:
  explicit GameLibraryDialog(
    std::shared_ptr<DialogService> dialogService,
    QWidget* parent = nullptr);

  void setDialogService(std::shared_ptr<DialogService> service);
  void setActions(GameLibraryActions actions);
  void setSnapshot(
    std::vector<library::LibraryDirectory> directories,
    std::vector<library::LibraryGame> games);
  void setServiceAvailable(bool available, const std::string& detail = {});
  void showScanStarted(
    std::int64_t directoryId,
    const std::filesystem::path& path);
  void showScanProgress(
    std::int64_t directoryId,
    const library::GameLibraryScanSummary& summary);
  void showScanCompleted(
    std::int64_t directoryId,
    const library::GameLibraryScanSummary& summary);
  void showScanFailed(std::int64_t directoryId, const std::string& detail);
  void showOperationError(const std::string& detail);

private:
  void buildUi();
  void rebuildDirectoryList();
  void rebuildGameRows();
  void rebuildRegionFilter();
  void updateDirectorySelection();
  void updateGameSelection();
  void updateFilter();
  void addDirectory();
  void removeDirectory();
  void scanDirectory();
  void setRecursive(bool recursive);
  void toggleFavorite();
  void chooseArtwork();
  void clearArtwork();
  void launchSelected();
  void showSelectedInformation();
  [[nodiscard]] std::int64_t selectedDirectoryId() const;
  [[nodiscard]] const library::LibraryGame* selectedGame() const;
  void updateBusyPresentation();

  std::shared_ptr<DialogService> dialogService_;
  GameLibraryActions actions_;
  std::vector<library::LibraryDirectory> directories_;
  std::vector<library::LibraryGame> games_;
  std::vector<std::int64_t> scanningDirectoryIds_;
  QLineEdit* searchEdit_{nullptr};
  QComboBox* systemFilter_{nullptr};
  QComboBox* regionFilter_{nullptr};
  QCheckBox* favoritesOnly_{nullptr};
  QTableView* gameTable_{nullptr};
  QStandardItemModel* gameModel_{nullptr};
  QSortFilterProxyModel* proxyModel_{nullptr};
  QListWidget* directoryList_{nullptr};
  QCheckBox* recursiveCheck_{nullptr};
  QPushButton* addDirectoryButton_{nullptr};
  QPushButton* removeDirectoryButton_{nullptr};
  QPushButton* scanDirectoryButton_{nullptr};
  QPushButton* favoriteButton_{nullptr};
  QPushButton* launchButton_{nullptr};
  QPushButton* informationButton_{nullptr};
  QPushButton* chooseArtworkButton_{nullptr};
  QPushButton* clearArtworkButton_{nullptr};
  QLabel* artworkLabel_{nullptr};
  QLabel* statusLabel_{nullptr};
  QProgressBar* progressBar_{nullptr};
  bool serviceAvailable_{true};
};

} // namespace genplusgx::ui
