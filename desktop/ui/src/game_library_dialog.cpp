#include "genplusgx/ui/game_library_dialog.h"

#include "genplusgx/ui/game_information_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImageReader>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QPixmap>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <ranges>
#include <set>

namespace genplusgx::ui {
namespace {

constexpr int gameIdRole = Qt::UserRole + 1;
constexpr int favoriteRole = Qt::UserRole + 2;
constexpr int systemRole = Qt::UserRole + 3;
constexpr int regionRole = Qt::UserRole + 4;
constexpr int searchRole = Qt::UserRole + 5;
constexpr int sortRole = Qt::UserRole + 6;

enum GameColumn : int {
  favoriteColumn,
  titleColumn,
  systemColumn,
  regionColumn,
  lastPlayedColumn,
  playCountColumn,
  pathColumn,
  gameColumnCount,
};

class LibraryFilterModel final : public QSortFilterProxyModel {
public:
  using QSortFilterProxyModel::QSortFilterProxyModel;

  void setSearch(QString search)
  {
    search_ = std::move(search).trimmed();
    refilter();
  }

  void setSystem(int system)
  {
    system_ = system;
    refilter();
  }

  void setRegion(QString region)
  {
    region_ = std::move(region);
    refilter();
  }

  void setFavoritesOnly(bool enabled)
  {
    favoritesOnly_ = enabled;
    refilter();
  }

protected:
  bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override
  {
    const auto index = sourceModel()->index(
      sourceRow, favoriteColumn, sourceParent);
    if (favoritesOnly_ && !index.data(favoriteRole).toBool()) {
      return false;
    }
    if (system_ >= 0 && index.data(systemRole).toInt() != system_) {
      return false;
    }
    if (!region_.isEmpty() &&
        QString::compare(index.data(regionRole).toString(), region_,
          Qt::CaseInsensitive) != 0) {
      return false;
    }
    return search_.isEmpty() || index.data(searchRole).toString().contains(
      search_, Qt::CaseInsensitive);
  }

private:
  void refilter()
  {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    endFilterChange(Direction::Rows);
#else
    invalidateFilter();
#endif
  }

  QString search_;
  QString region_;
  int system_{-1};
  bool favoritesOnly_{false};
};

QStandardItem* item(const QString& display, const QVariant& sortValue)
{
  auto* value = new QStandardItem(display);
  value->setEditable(false);
  value->setData(sortValue, sortRole);
  return value;
}

QString playedText(const std::optional<std::int64_t>& timestamp)
{
  return timestamp
    ? QDateTime::fromMSecsSinceEpoch(*timestamp).toString(
        QStringLiteral("yyyy-MM-dd HH:mm"))
    : QObject::tr("Never");
}

QString systemText(library::GameSystem system)
{
  const auto name = library::gameSystemName(system);
  return QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()));
}

} // namespace

GameLibraryDialog::GameLibraryDialog(
  std::shared_ptr<DialogService> dialogService,
  QWidget* parent)
  : QDialog(parent), dialogService_(std::move(dialogService))
{
  setObjectName(QStringLiteral("gameLibraryDialog"));
  setWindowTitle(tr("Game Library"));
  setModal(false);
  resize(1'050, 680);
  setMinimumSize(760, 520);
  buildUi();
}

void GameLibraryDialog::buildUi()
{
  auto* root = new QVBoxLayout(this);
  auto* filters = new QHBoxLayout;
  searchEdit_ = new QLineEdit(this);
  searchEdit_->setObjectName(QStringLiteral("librarySearchEdit"));
  searchEdit_->setPlaceholderText(tr("Search titles and paths…"));
  searchEdit_->setClearButtonEnabled(true);
  searchEdit_->setAccessibleName(tr("Search game library"));
  filters->addWidget(searchEdit_, 1);
  systemFilter_ = new QComboBox(this);
  systemFilter_->setObjectName(QStringLiteral("librarySystemFilterCombo"));
  systemFilter_->setAccessibleName(tr("System filter"));
  systemFilter_->addItem(tr("All systems"), -1);
  for (const auto system : {
         library::GameSystem::sg1000,
         library::GameSystem::masterSystem,
         library::GameSystem::gameGear,
         library::GameSystem::genesis,
         library::GameSystem::segaCd,
         library::GameSystem::unknown}) {
    systemFilter_->addItem(
      systemText(system),
      static_cast<int>(system));
  }
  filters->addWidget(systemFilter_);
  regionFilter_ = new QComboBox(this);
  regionFilter_->setObjectName(QStringLiteral("libraryRegionFilterCombo"));
  regionFilter_->setAccessibleName(tr("Region filter"));
  filters->addWidget(regionFilter_);
  favoritesOnly_ = new QCheckBox(tr("Favorites only"), this);
  favoritesOnly_->setObjectName(QStringLiteral("libraryFavoritesOnlyCheck"));
  filters->addWidget(favoritesOnly_);
  root->addLayout(filters);

  auto* splitter = new QSplitter(Qt::Horizontal, this);
  splitter->setObjectName(QStringLiteral("librarySplitter"));
  auto* directoryPanel = new QWidget(splitter);
  auto* directoryLayout = new QVBoxLayout(directoryPanel);
  auto* directoryLabel = new QLabel(tr("Game directories"), directoryPanel);
  directoryLabel->setObjectName(QStringLiteral("libraryDirectoriesLabel"));
  directoryLayout->addWidget(directoryLabel);
  directoryList_ = new QListWidget(directoryPanel);
  directoryList_->setObjectName(QStringLiteral("libraryDirectoryList"));
  directoryList_->setAccessibleName(tr("Configured game directories"));
  directoryLayout->addWidget(directoryList_, 1);
  recursiveCheck_ = new QCheckBox(tr("Scan subdirectories"), directoryPanel);
  recursiveCheck_->setObjectName(QStringLiteral("libraryRecursiveCheck"));
  directoryLayout->addWidget(recursiveCheck_);
  addDirectoryButton_ = new QPushButton(tr("Add…"), directoryPanel);
  addDirectoryButton_->setObjectName(QStringLiteral("libraryAddDirectoryButton"));
  removeDirectoryButton_ = new QPushButton(tr("Remove"), directoryPanel);
  removeDirectoryButton_->setObjectName(
    QStringLiteral("libraryRemoveDirectoryButton"));
  scanDirectoryButton_ = new QPushButton(tr("Scan Now"), directoryPanel);
  scanDirectoryButton_->setObjectName(QStringLiteral("libraryScanDirectoryButton"));
  auto* directoryButtons = new QHBoxLayout;
  directoryButtons->addWidget(addDirectoryButton_);
  directoryButtons->addWidget(removeDirectoryButton_);
  directoryButtons->addWidget(scanDirectoryButton_);
  directoryLayout->addLayout(directoryButtons);

  auto* gamesPanel = new QWidget(splitter);
  auto* gamesLayout = new QVBoxLayout(gamesPanel);
  gameModel_ = new QStandardItemModel(0, gameColumnCount, this);
  gameModel_->setHorizontalHeaderLabels({
    tr("Favorite"), tr("Title"), tr("System"), tr("Region"),
    tr("Last Played"), tr("Plays"), tr("Path")});
  proxyModel_ = new LibraryFilterModel(this);
  proxyModel_->setSourceModel(gameModel_);
  proxyModel_->setSortRole(sortRole);
  proxyModel_->setDynamicSortFilter(true);
  gameTable_ = new QTableView(gamesPanel);
  gameTable_->setObjectName(QStringLiteral("libraryGameTable"));
  gameTable_->setAccessibleName(tr("Indexed games"));
  gameTable_->setModel(proxyModel_);
  gameTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  gameTable_->setSelectionMode(QAbstractItemView::SingleSelection);
  gameTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  gameTable_->setSortingEnabled(true);
  gameTable_->sortByColumn(titleColumn, Qt::AscendingOrder);
  gameTable_->verticalHeader()->setVisible(false);
  gameTable_->horizontalHeader()->setStretchLastSection(true);
  gameTable_->horizontalHeader()->setSectionResizeMode(
    titleColumn, QHeaderView::Stretch);
  gamesLayout->addWidget(gameTable_, 1);

  auto* selectionPanel = new QHBoxLayout;
  artworkLabel_ = new QLabel(tr("No artwork"), gamesPanel);
  artworkLabel_->setObjectName(QStringLiteral("libraryArtworkLabel"));
  artworkLabel_->setAccessibleName(tr("Selected game artwork"));
  artworkLabel_->setAlignment(Qt::AlignCenter);
  artworkLabel_->setFrameShape(QFrame::StyledPanel);
  artworkLabel_->setMinimumSize(120, 150);
  artworkLabel_->setMaximumSize(180, 240);
  selectionPanel->addWidget(artworkLabel_);
  auto* selectionButtons = new QVBoxLayout;
  favoriteButton_ = new QPushButton(tr("Add to Favorites"), gamesPanel);
  favoriteButton_->setObjectName(QStringLiteral("libraryFavoriteButton"));
  launchButton_ = new QPushButton(tr("Launch"), gamesPanel);
  launchButton_->setObjectName(QStringLiteral("libraryLaunchButton"));
  launchButton_->setDefault(true);
  informationButton_ = new QPushButton(tr("Game Information…"), gamesPanel);
  informationButton_->setObjectName(QStringLiteral("libraryInformationButton"));
  chooseArtworkButton_ = new QPushButton(tr("Choose Artwork…"), gamesPanel);
  chooseArtworkButton_->setObjectName(QStringLiteral("libraryChooseArtworkButton"));
  clearArtworkButton_ = new QPushButton(tr("Clear Artwork"), gamesPanel);
  clearArtworkButton_->setObjectName(QStringLiteral("libraryClearArtworkButton"));
  lookupMetadataButton_ = new QPushButton(tr("Fetch Online Metadata…"), gamesPanel);
  lookupMetadataButton_->setObjectName(
    QStringLiteral("libraryLookupMetadataButton"));
  clearMetadataButton_ = new QPushButton(tr("Clear Online Metadata"), gamesPanel);
  clearMetadataButton_->setObjectName(
    QStringLiteral("libraryClearMetadataButton"));
  for (auto* button : {favoriteButton_, launchButton_, informationButton_,
                       chooseArtworkButton_, clearArtworkButton_,
                       lookupMetadataButton_, clearMetadataButton_}) {
    selectionButtons->addWidget(button);
  }
  metadataDetailsLabel_ = new QLabel(tr("No online metadata"), gamesPanel);
  metadataDetailsLabel_->setObjectName(
    QStringLiteral("libraryOnlineMetadataDetailsLabel"));
  metadataDetailsLabel_->setAccessibleName(
    tr("Selected game online metadata attribution"));
  metadataDetailsLabel_->setWordWrap(true);
  metadataDetailsLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  selectionButtons->addWidget(metadataDetailsLabel_);
  selectionButtons->addStretch();
  selectionPanel->addLayout(selectionButtons);
  gamesLayout->addLayout(selectionPanel);
  splitter->addWidget(directoryPanel);
  splitter->addWidget(gamesPanel);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({250, 800});
  root->addWidget(splitter, 1);

  progressBar_ = new QProgressBar(this);
  progressBar_->setObjectName(QStringLiteral("libraryProgressBar"));
  progressBar_->setRange(0, 0);
  progressBar_->setVisible(false);
  root->addWidget(progressBar_);
  statusLabel_ = new QLabel(tr("No indexed games"), this);
  statusLabel_->setObjectName(QStringLiteral("libraryStatusLabel"));
  statusLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  root->addWidget(statusLabel_);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  buttons->setObjectName(QStringLiteral("libraryButtonBox"));
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
  root->addWidget(buttons);

  connect(searchEdit_, &QLineEdit::textChanged,
    this, [this] { updateFilter(); });
  connect(systemFilter_, &QComboBox::currentIndexChanged,
    this, [this] { updateFilter(); });
  connect(regionFilter_, &QComboBox::currentIndexChanged,
    this, [this] { updateFilter(); });
  connect(favoritesOnly_, &QCheckBox::toggled,
    this, [this] { updateFilter(); });
  connect(addDirectoryButton_, &QPushButton::clicked,
    this, &GameLibraryDialog::addDirectory);
  connect(removeDirectoryButton_, &QPushButton::clicked,
    this, &GameLibraryDialog::removeDirectory);
  connect(scanDirectoryButton_, &QPushButton::clicked,
    this, &GameLibraryDialog::scanDirectory);
  connect(recursiveCheck_, &QCheckBox::clicked,
    this, &GameLibraryDialog::setRecursive);
  connect(directoryList_, &QListWidget::currentRowChanged,
    this, [this] { updateDirectorySelection(); });
  connect(gameTable_->selectionModel(), &QItemSelectionModel::selectionChanged,
    this, [this] { updateGameSelection(); });
  connect(gameTable_, &QTableView::doubleClicked,
    this, [this] { launchSelected(); });
  connect(favoriteButton_, &QPushButton::clicked,
    this, &GameLibraryDialog::toggleFavorite);
  connect(launchButton_, &QPushButton::clicked,
    this, &GameLibraryDialog::launchSelected);
  connect(informationButton_, &QPushButton::clicked,
    this, &GameLibraryDialog::showSelectedInformation);
  connect(chooseArtworkButton_, &QPushButton::clicked,
    this, &GameLibraryDialog::chooseArtwork);
  connect(clearArtworkButton_, &QPushButton::clicked,
    this, &GameLibraryDialog::clearArtwork);
  connect(lookupMetadataButton_, &QPushButton::clicked,
    this, &GameLibraryDialog::lookupOnlineMetadata);
  connect(clearMetadataButton_, &QPushButton::clicked,
    this, &GameLibraryDialog::clearOnlineMetadata);
  rebuildRegionFilter();
  updateDirectorySelection();
  updateGameSelection();
}

void GameLibraryDialog::setDialogService(std::shared_ptr<DialogService> service)
{
  if (service) {
    dialogService_ = std::move(service);
  }
}

void GameLibraryDialog::setActions(GameLibraryActions actions)
{
  actions_ = std::move(actions);
  updateDirectorySelection();
  updateGameSelection();
}

void GameLibraryDialog::setSnapshot(
  std::vector<library::LibraryDirectory> directories,
  std::vector<library::LibraryGame> games)
{
  const auto selectedDirectory = selectedDirectoryId();
  const auto* currentGame = selectedGame();
  const auto selectedGameId = currentGame == nullptr ? 0 : currentGame->id;
  directories_ = std::move(directories);
  games_ = std::move(games);
  rebuildDirectoryList();
  rebuildRegionFilter();
  rebuildGameRows();

  for (int row = 0; row < directoryList_->count(); ++row) {
    if (directoryList_->item(row)->data(Qt::UserRole).toLongLong() ==
        selectedDirectory) {
      directoryList_->setCurrentRow(row);
      break;
    }
  }
  for (int row = 0; row < proxyModel_->rowCount(); ++row) {
    if (proxyModel_->index(row, favoriteColumn).data(gameIdRole).toLongLong() ==
        selectedGameId) {
      gameTable_->selectRow(row);
      break;
    }
  }
  statusLabel_->setText(tr("%1 indexed game(s) from %2 directorie(s)")
    .arg(games_.size()).arg(directories_.size()));
  updateDirectorySelection();
  updateGameSelection();
}

void GameLibraryDialog::rebuildDirectoryList()
{
  directoryList_->clear();
  for (const auto& directory : directories_) {
    auto* item = new QListWidgetItem(pathToQString(directory.path), directoryList_);
    item->setData(Qt::UserRole, static_cast<qlonglong>(directory.id));
    item->setToolTip(pathToQString(directory.path));
  }
}

void GameLibraryDialog::rebuildRegionFilter()
{
  const auto previous = regionFilter_->currentData().toString();
  std::set<QString> regions;
  for (const auto& game : games_) {
    if (!game.metadata.region.empty()) {
      regions.insert(QString::fromStdString(game.metadata.region));
    }
  }
  regionFilter_->blockSignals(true);
  regionFilter_->clear();
  regionFilter_->addItem(tr("All regions"), QString{});
  for (const auto& region : regions) {
    regionFilter_->addItem(region, region);
  }
  const auto previousIndex = regionFilter_->findData(previous);
  regionFilter_->setCurrentIndex(std::max(previousIndex, 0));
  regionFilter_->blockSignals(false);
  updateFilter();
}

void GameLibraryDialog::rebuildGameRows()
{
  gameModel_->removeRows(0, gameModel_->rowCount());
  for (const auto& game : games_) {
    const auto favorite = game.favorite ? QStringLiteral("★") : QString{};
    const auto title = QString::fromStdString(game.displayTitle());
    const auto system = systemText(game.metadata.system);
    const auto region = QString::fromStdString(game.metadata.region);
    const auto path = pathToQString(game.metadata.path);
    QList<QStandardItem*> row{
      item(favorite, game.favorite),
      item(title, title.toCaseFolded()),
      item(system, system.toCaseFolded()),
      item(region, region.toCaseFolded()),
      item(playedText(game.lastPlayedEpochMilliseconds),
        static_cast<qlonglong>(game.lastPlayedEpochMilliseconds.value_or(-1))),
      item(QString::number(game.playCount),
        static_cast<qulonglong>(game.playCount)),
      item(path, path.toCaseFolded()),
    };
    const auto search = QStringLiteral("%1 %2 %3 %4 %5")
      .arg(title, path, region,
        QString::fromStdString(game.metadata.domesticTitle),
        QString::fromStdString(game.metadata.internationalTitle));
    for (auto* value : row) {
      value->setData(static_cast<qlonglong>(game.id), gameIdRole);
      value->setData(game.favorite, favoriteRole);
      value->setData(static_cast<int>(game.metadata.system), systemRole);
      value->setData(region, regionRole);
      value->setData(search, searchRole);
    }
    gameModel_->appendRow(row);
  }
  updateFilter();
}

void GameLibraryDialog::setServiceAvailable(
  bool available,
  const std::string& detail)
{
  serviceAvailable_ = available;
  if (!available) {
    scanningDirectoryIds_.clear();
    statusLabel_->setText(detail.empty()
      ? tr("The game-library service is unavailable.")
      : QString::fromStdString(detail));
  }
  updateBusyPresentation();
  updateDirectorySelection();
}

void GameLibraryDialog::setOnlineMetadataEnabled(bool enabled)
{
  onlineMetadataEnabled_ = enabled;
  updateGameSelection();
}

void GameLibraryDialog::showOnlineMetadataStarted(std::int64_t gameId)
{
  onlineMetadataGameId_ = gameId;
  statusLabel_->setText(tr("Looking up licensed online metadata…"));
  updateBusyPresentation();
}

void GameLibraryDialog::showOnlineMetadataCompleted(
  std::int64_t gameId,
  bool fromCache,
  bool staleCache)
{
  if (onlineMetadataGameId_ == gameId) {
    onlineMetadataGameId_ = 0;
  }
  statusLabel_->setText(staleCache
    ? tr("Online provider unavailable; restored validated cached metadata.")
    : (fromCache ? tr("Validated metadata loaded from the local cache.")
                 : tr("Licensed online metadata updated.")));
  updateBusyPresentation();
}

void GameLibraryDialog::showOnlineMetadataFailed(
  std::int64_t gameId,
  const std::string& detail)
{
  if (onlineMetadataGameId_ == gameId) {
    onlineMetadataGameId_ = 0;
  }
  updateBusyPresentation();
  showOperationError(detail);
}

void GameLibraryDialog::showScanStarted(
  std::int64_t directoryId,
  const std::filesystem::path& path)
{
  if (std::ranges::find(scanningDirectoryIds_, directoryId) ==
      scanningDirectoryIds_.end()) {
    scanningDirectoryIds_.push_back(directoryId);
  }
  statusLabel_->setText(tr("Scanning %1…").arg(pathToQString(path)));
  updateBusyPresentation();
}

void GameLibraryDialog::showScanProgress(
  std::int64_t,
  const library::GameLibraryScanSummary& summary)
{
  statusLabel_->setText(
    tr("Scanning: %1 files visited, %2 supported game(s) found")
      .arg(summary.visitedFiles).arg(summary.supportedFiles));
}

void GameLibraryDialog::showScanCompleted(
  std::int64_t directoryId,
  const library::GameLibraryScanSummary& summary)
{
  std::erase(scanningDirectoryIds_, directoryId);
  statusLabel_->setText(
    tr("Scan complete: %1 game(s) indexed, %2 stale entrie(s) removed")
      .arg(summary.indexedGames).arg(summary.removedGames));
  updateBusyPresentation();
}

void GameLibraryDialog::showScanFailed(
  std::int64_t directoryId,
  const std::string& detail)
{
  std::erase(scanningDirectoryIds_, directoryId);
  updateBusyPresentation();
  showOperationError(detail);
}

void GameLibraryDialog::showOperationError(const std::string& detail)
{
  statusLabel_->setText(tr("Game-library operation failed"));
  dialogService_->showError(
    this, tr("Game Library Error"), QString::fromStdString(detail));
}

void GameLibraryDialog::updateFilter()
{
  auto* filter = static_cast<LibraryFilterModel*>(proxyModel_);
  filter->setSearch(searchEdit_->text());
  filter->setSystem(systemFilter_->currentData().toInt());
  filter->setRegion(regionFilter_->currentData().toString());
  filter->setFavoritesOnly(favoritesOnly_->isChecked());
  updateGameSelection();
}

std::int64_t GameLibraryDialog::selectedDirectoryId() const
{
  const auto* item = directoryList_ == nullptr ? nullptr : directoryList_->currentItem();
  return item == nullptr ? 0 : item->data(Qt::UserRole).toLongLong();
}

const library::LibraryGame* GameLibraryDialog::selectedGame() const
{
  if (gameTable_ == nullptr || gameTable_->selectionModel() == nullptr) {
    return nullptr;
  }
  const auto rows = gameTable_->selectionModel()->selectedRows();
  if (rows.empty()) {
    return nullptr;
  }
  const auto id = rows.front().data(gameIdRole).toLongLong();
  const auto found = std::ranges::find_if(
    games_, [id](const auto& game) { return game.id == id; });
  return found == games_.end() ? nullptr : &*found;
}

void GameLibraryDialog::updateDirectorySelection()
{
  const bool writable = serviceAvailable_ && scanningDirectoryIds_.empty();
  const auto id = selectedDirectoryId();
  const auto found = std::ranges::find_if(
    directories_, [id](const auto& directory) { return directory.id == id; });
  const bool selected = found != directories_.end();
  addDirectoryButton_->setEnabled(writable && static_cast<bool>(actions_.addDirectory));
  recursiveCheck_->setEnabled(selected && writable);
  removeDirectoryButton_->setEnabled(
    selected && writable && static_cast<bool>(actions_.removeDirectory));
  scanDirectoryButton_->setEnabled(
    selected && serviceAvailable_ && static_cast<bool>(actions_.scanDirectory) &&
    std::ranges::find(scanningDirectoryIds_, id) ==
      scanningDirectoryIds_.end());
  if (selected) {
    recursiveCheck_->setChecked(found->recursive);
  } else {
    recursiveCheck_->setChecked(false);
  }
}

void GameLibraryDialog::updateGameSelection()
{
  const auto* game = selectedGame();
  const bool selected = game != nullptr;
  const bool writable = serviceAvailable_ && scanningDirectoryIds_.empty();
  favoriteButton_->setEnabled(
    selected && writable && static_cast<bool>(actions_.setFavorite));
  launchButton_->setEnabled(selected && static_cast<bool>(actions_.launchGame));
  informationButton_->setEnabled(selected);
  chooseArtworkButton_->setEnabled(
    selected && writable && static_cast<bool>(actions_.setArtwork));
  clearArtworkButton_->setEnabled(selected && writable && !game->artworkPath.empty() &&
    static_cast<bool>(actions_.setArtwork));
  lookupMetadataButton_->setEnabled(selected && writable && onlineMetadataEnabled_ &&
    onlineMetadataGameId_ == 0 && static_cast<bool>(actions_.lookupOnlineMetadata));
  lookupMetadataButton_->setToolTip(onlineMetadataEnabled_ ? QString{} :
    tr("Enable online metadata in Tools → Online Metadata and Artwork."));
  clearMetadataButton_->setEnabled(selected && writable && game->onlineMetadata &&
    onlineMetadataGameId_ == 0 && static_cast<bool>(actions_.clearOnlineMetadata));
  if (!selected) {
    favoriteButton_->setText(tr("Add to Favorites"));
    artworkLabel_->setPixmap({});
    artworkLabel_->setText(tr("No artwork"));
    metadataDetailsLabel_->setText(tr("No online metadata"));
    return;
  }
  favoriteButton_->setText(
    game->favorite ? tr("Remove from Favorites") : tr("Add to Favorites"));
  if (game->onlineMetadata) {
    const auto& online = *game->onlineMetadata;
    metadataDetailsLabel_->setText(
      tr("%1\n%2 · %3\nAttribution: %4")
        .arg(QString::fromStdString(online.preferredTitle),
          QString::fromStdString(online.providerName),
          QString::fromStdString(online.attribution.licenseSpdx),
          QString::fromStdString(online.attribution.creator)));
  } else {
    metadataDetailsLabel_->setText(tr("No online metadata"));
  }
  if (game->artworkPath.empty()) {
    artworkLabel_->setPixmap({});
    artworkLabel_->setText(tr("No artwork"));
    return;
  }
  QImageReader reader{pathToQString(game->artworkPath)};
  reader.setAutoTransform(true);
  const auto size = reader.size();
  if (size.isValid()) {
    reader.setScaledSize(size.scaled(QSize{180, 240}, Qt::KeepAspectRatio));
  }
  const auto image = reader.read();
  if (image.isNull()) {
    artworkLabel_->setPixmap({});
    artworkLabel_->setText(tr("Artwork unavailable"));
    return;
  }
  artworkLabel_->setText({});
  artworkLabel_->setPixmap(QPixmap::fromImage(image));
}

void GameLibraryDialog::addDirectory()
{
  if (!serviceAvailable_ || !actions_.addDirectory) {
    return;
  }
  const auto initial = directories_.empty()
    ? std::filesystem::path{} : directories_.back().path;
  const auto selected = dialogService_->chooseDirectory(this, initial);
  if (selected) {
    actions_.addDirectory(*selected, true);
  }
}

void GameLibraryDialog::removeDirectory()
{
  const auto id = selectedDirectoryId();
  if (id > 0 && actions_.removeDirectory) {
    actions_.removeDirectory(id);
  }
}

void GameLibraryDialog::scanDirectory()
{
  const auto id = selectedDirectoryId();
  if (id > 0 && actions_.scanDirectory) {
    actions_.scanDirectory(id);
  }
}

void GameLibraryDialog::setRecursive(bool recursive)
{
  const auto id = selectedDirectoryId();
  if (id > 0 && actions_.updateDirectory) {
    actions_.updateDirectory(id, recursive);
  }
}

void GameLibraryDialog::toggleFavorite()
{
  const auto* game = selectedGame();
  if (game != nullptr && actions_.setFavorite) {
    actions_.setFavorite(game->id, !game->favorite);
  }
}

void GameLibraryDialog::chooseArtwork()
{
  const auto* game = selectedGame();
  if (game == nullptr || !actions_.setArtwork) {
    return;
  }
  const auto initial = game->artworkPath.empty()
    ? game->metadata.path.parent_path() : game->artworkPath.parent_path();
  const auto selected = dialogService_->chooseArtwork(this, initial);
  if (selected) {
    actions_.setArtwork(game->id, *selected);
  }
}

void GameLibraryDialog::clearArtwork()
{
  const auto* game = selectedGame();
  if (game != nullptr && actions_.setArtwork) {
    actions_.setArtwork(game->id, {});
  }
}

void GameLibraryDialog::lookupOnlineMetadata()
{
  const auto* game = selectedGame();
  if (game != nullptr && onlineMetadataEnabled_ &&
      actions_.lookupOnlineMetadata) {
    actions_.lookupOnlineMetadata(game->id);
  }
}

void GameLibraryDialog::clearOnlineMetadata()
{
  const auto* game = selectedGame();
  if (game != nullptr && actions_.clearOnlineMetadata) {
    actions_.clearOnlineMetadata(game->id);
  }
}

void GameLibraryDialog::launchSelected()
{
  const auto* game = selectedGame();
  if (game != nullptr && actions_.launchGame) {
    actions_.launchGame(game->id, game->metadata.path);
  }
}

void GameLibraryDialog::showSelectedInformation()
{
  const auto* game = selectedGame();
  if (game == nullptr) {
    return;
  }
  if (auto* existing = findChild<GameInformationDialog*>(
        QStringLiteral("gameInformationDialog"))) {
    existing->setMetadata(game->metadata);
    existing->setOnlineMetadata(game->onlineMetadata);
    existing->raise();
    existing->activateWindow();
    return;
  }
  auto* dialog = new GameInformationDialog(game->metadata, this);
  dialog->setOnlineMetadata(game->onlineMetadata);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->open();
}

void GameLibraryDialog::updateBusyPresentation()
{
  progressBar_->setVisible(
    !scanningDirectoryIds_.empty() || onlineMetadataGameId_ != 0);
  updateDirectorySelection();
  updateGameSelection();
}

} // namespace genplusgx::ui
