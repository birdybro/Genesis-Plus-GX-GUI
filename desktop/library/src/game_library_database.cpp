#include "genplusgx/library/game_library_database.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariant>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <ranges>
#include <system_error>
#include <thread>
#include <utility>

namespace genplusgx::library {
namespace {

std::atomic<std::uint64_t> connectionCounter{0U};

GameLibraryStatus failure(GameLibraryError error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

QString pathToQString(const std::filesystem::path& path)
{
#if defined(Q_OS_WIN)
  return QString::fromStdWString(path.wstring());
#else
  return QString::fromUtf8(path.string());
#endif
}

std::filesystem::path pathFromQString(const QString& path)
{
#if defined(Q_OS_WIN)
  return std::filesystem::path{path.toStdWString()};
#else
  return std::filesystem::path{path.toUtf8().constData()};
#endif
}

std::int64_t currentEpochMilliseconds()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
}

bool pathIsWithin(
  const std::filesystem::path& candidate,
  const std::filesystem::path& root)
{
  const auto relative = candidate.lexically_relative(root);
  return !relative.empty() && !relative.is_absolute() &&
         std::ranges::none_of(relative, [](const auto& component) {
           return component == "..";
         });
}

bool indicatesCorruption(const QSqlError& error)
{
  const auto native = error.nativeErrorCode();
  const auto text = error.text().toLower();
  return native == QStringLiteral("11") || native == QStringLiteral("26") ||
         text.contains(QStringLiteral("malformed")) ||
         text.contains(QStringLiteral("not a database"));
}

std::filesystem::path unusedRecoveryPath(const std::filesystem::path& databasePath)
{
  const auto base = databasePath.string() + ".corrupt-" +
                    std::to_string(currentEpochMilliseconds());
  std::filesystem::path candidate{base};
  for (std::uint32_t suffix = 1U; std::filesystem::exists(candidate); ++suffix) {
    candidate = std::filesystem::path{base + "-" + std::to_string(suffix)};
  }
  return candidate;
}

QVariant optionalInteger(const std::optional<std::uint16_t>& value)
{
  return value ? QVariant{static_cast<unsigned int>(*value)} : QVariant{};
}

QVariant optionalInteger(const std::optional<std::uint64_t>& value)
{
  return value ? QVariant{static_cast<qulonglong>(*value)} : QVariant{};
}

std::optional<std::uint16_t> optionalUnsigned16(const QVariant& value)
{
  if (value.isNull()) {
    return std::nullopt;
  }
  const auto converted = value.toUInt();
  if (converted > std::numeric_limits<std::uint16_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint16_t>(converted);
}

std::optional<std::uint64_t> optionalUnsigned64(const QVariant& value)
{
  return value.isNull()
    ? std::nullopt
    : std::optional<std::uint64_t>{value.toULongLong()};
}

} // namespace

class GameLibraryDatabase::Private final {
public:
  explicit Private(std::filesystem::path path)
    : path_(std::move(path)),
      connectionName_(QStringLiteral("genplusgx-library-%1").arg(
        ++connectionCounter))
  {
  }

  ~Private()
  {
    if (database_.isValid()) {
      if (scanActive_) {
        static_cast<void>(database_.rollback());
      }
      database_.close();
      database_ = QSqlDatabase{};
      QSqlDatabase::removeDatabase(connectionName_);
    }
  }

  GameLibraryStatus initialize()
  {
    if (path_.empty()) {
      return failure(
        GameLibraryError::invalidPath,
        "The game-library database path is empty.");
    }
    if (initialized_) {
      return checkThread();
    }
    ownerThread_ = std::this_thread::get_id();

    std::error_code error;
    const auto parent = path_.parent_path();
    if (parent.empty()) {
      return failure(
        GameLibraryError::invalidPath,
        "The game-library database requires an explicit parent directory.");
    }
    std::filesystem::create_directories(parent, error);
    if (error) {
      return failure(
        GameLibraryError::databaseUnavailable,
        "The game-library directory could not be created: " + error.message());
    }

    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    if (!database_.isValid()) {
      return failure(
        GameLibraryError::databaseUnavailable,
        "The Qt SQLite driver is unavailable.");
    }
    database_.setDatabaseName(pathToQString(path_));
    database_.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (!database_.open()) {
      return failure(
        GameLibraryError::databaseUnavailable,
        "The game-library database could not be opened: " +
          database_.lastError().text().toStdString());
    }

    const auto integrity = checkIntegrity();
    if (!integrity) {
      if (integrity.error != GameLibraryError::databaseCorrupt) {
        return integrity;
      }
      const auto recovered = recoverCorruptDatabase();
      if (!recovered) {
        return recovered;
      }
    }

    const auto configured = configureConnection();
    if (!configured) {
      return configured;
    }
    const auto schema = initializeSchema();
    if (!schema) {
      if (schema.error != GameLibraryError::databaseCorrupt ||
          recoveredCorruption_) {
        return schema;
      }
      const auto recovered = recoverCorruptDatabase();
      if (!recovered) {
        return recovered;
      }
      const auto replacementConfigured = configureConnection();
      if (!replacementConfigured) {
        return replacementConfigured;
      }
      const auto replacementSchema = initializeSchema();
      if (!replacementSchema) {
        return replacementSchema;
      }
    }
    initialized_ = true;
    return recoveredCorruption_
      ? GameLibraryStatus{
          .error = GameLibraryError::none,
          .message = "A corrupt game-library database was preserved and rebuilt."}
      : GameLibraryStatus{};
  }

  bool isOpen() const noexcept
  {
    return initialized_ && database_.isOpen();
  }

  const std::filesystem::path& path() const noexcept { return path_; }
  bool recoveredCorruption() const noexcept { return recoveredCorruption_; }
  const std::filesystem::path& recoveryBackupPath() const noexcept
  {
    return recoveryBackupPath_;
  }

  LibraryDirectoryResult addDirectory(
    const std::filesystem::path& path,
    bool recursive)
  {
    if (const auto ready = checkReady(); !ready) {
      return {.status = ready, .directory = {}};
    }
    if (path.empty()) {
      return {
        .status = failure(
          GameLibraryError::invalidPath,
          "The game-library directory path is empty."),
        .directory = {},
      };
    }
    std::error_code error;
    if (!std::filesystem::is_directory(path, error) || error) {
      return {
        .status = failure(
          GameLibraryError::directoryNotFound,
          "The selected game-library directory does not exist."),
        .directory = {},
      };
    }
    const auto canonical = std::filesystem::canonical(path, error);
    if (error) {
      return {
        .status = failure(
          GameLibraryError::invalidPath,
          "The selected game-library directory could not be resolved: " +
            error.message()),
        .directory = {},
      };
    }

    const auto existing = directories();
    if (!existing.status) {
      return {.status = existing.status, .directory = {}};
    }
    if (existing.directories.size() >=
        GameLibraryDatabase::maximumConfiguredDirectories) {
      return {
        .status = failure(
          GameLibraryError::invalidRecord,
          "The game library has reached its 256-directory safety limit."),
        .directory = {},
      };
    }
    for (const auto& directory : existing.directories) {
      if (canonical == directory.path || pathIsWithin(canonical, directory.path) ||
          pathIsWithin(directory.path, canonical)) {
        return {
          .status = failure(
            GameLibraryError::directoryOverlap,
            "Game-library directories may not duplicate or overlap."),
          .directory = {},
        };
      }
    }

    QSqlQuery query{database_};
    query.prepare(QStringLiteral(
      "INSERT INTO library_directories(path, recursive, created_at) "
      "VALUES (?, ?, ?)"));
    query.addBindValue(pathToQString(canonical));
    query.addBindValue(recursive ? 1 : 0);
    query.addBindValue(static_cast<qlonglong>(currentEpochMilliseconds()));
    if (!query.exec()) {
      return {
        .status = queryFailure("The game-library directory could not be added", query),
        .directory = {},
      };
    }
    return {
      .status = {},
      .directory = {
        .id = query.lastInsertId().toLongLong(),
        .path = canonical,
        .recursive = recursive,
      },
    };
  }

  GameLibraryStatus updateDirectory(std::int64_t directoryId, bool recursive)
  {
    if (const auto ready = checkReady(); !ready) {
      return ready;
    }
    if (directoryId <= 0) {
      return failure(GameLibraryError::invalidRecord, "The library directory ID is invalid.");
    }
    QSqlQuery query{database_};
    query.prepare(QStringLiteral(
      "UPDATE library_directories SET recursive = ? WHERE id = ?"));
    query.addBindValue(recursive ? 1 : 0);
    query.addBindValue(static_cast<qlonglong>(directoryId));
    if (!query.exec()) {
      return queryFailure("The game-library directory could not be updated", query);
    }
    return query.numRowsAffected() == 1
      ? GameLibraryStatus{}
      : failure(GameLibraryError::directoryNotFound,
          "The game-library directory no longer exists.");
  }

  GameLibraryStatus removeDirectory(std::int64_t directoryId)
  {
    if (const auto ready = checkReady(); !ready) {
      return ready;
    }
    if (scanActive_) {
      return failure(
        GameLibraryError::transactionFailed,
        "A directory cannot be removed during an active scan transaction.");
    }
    QSqlQuery query{database_};
    query.prepare(QStringLiteral("DELETE FROM library_directories WHERE id = ?"));
    query.addBindValue(static_cast<qlonglong>(directoryId));
    if (!query.exec()) {
      return queryFailure("The game-library directory could not be removed", query);
    }
    return query.numRowsAffected() == 1
      ? GameLibraryStatus{}
      : failure(GameLibraryError::directoryNotFound,
          "The game-library directory no longer exists.");
  }

  LibraryDirectoriesResult directories() const
  {
    if (const auto ready = checkReady(); !ready) {
      return {.status = ready, .directories = {}};
    }
    QSqlQuery query{database_};
    if (!query.exec(QStringLiteral(
          "SELECT id, path, recursive FROM library_directories ORDER BY path"))) {
      return {
        .status = queryFailure("Game-library directories could not be read", query),
        .directories = {},
      };
    }
    LibraryDirectoriesResult result;
    while (query.next()) {
      result.directories.push_back({
        .id = query.value(0).toLongLong(),
        .path = pathFromQString(query.value(1).toString()),
        .recursive = query.value(2).toBool(),
      });
    }
    return result;
  }

  LibraryGamesResult games() const
  {
    if (const auto ready = checkReady(); !ready) {
      return {.status = ready, .games = {}};
    }
    QSqlQuery query{database_};
    if (!query.exec(QStringLiteral(
      "SELECT id, directory_id, path, related_data_path, file_size, system, "
      "format, domestic_title, international_title, copyright, product_code, "
      "region, rom_type, peripheral_support, mapper, sha256, notes, "
      "header_checksum, computed_checksum, declared_rom_size, track_count, "
      "header_recognized, last_modified, favorite, last_played, play_count, "
      "artwork_path FROM library_games ORDER BY display_title, path"))) {
      return {
        .status = queryFailure("Game-library entries could not be read", query),
        .games = {},
      };
    }

    LibraryGamesResult result;
    while (query.next()) {
      LibraryGame game;
      game.id = query.value(0).toLongLong();
      game.directoryId = query.value(1).toLongLong();
      game.metadata.path = pathFromQString(query.value(2).toString());
      game.metadata.relatedDataPath = pathFromQString(query.value(3).toString());
      game.metadata.fileSize = query.value(4).toULongLong();
      const auto system = query.value(5).toUInt();
      game.metadata.system = system <= static_cast<unsigned int>(GameSystem::segaCd)
        ? static_cast<GameSystem>(system) : GameSystem::unknown;
      game.metadata.format = query.value(6).toString().toStdString();
      game.metadata.domesticTitle = query.value(7).toString().toStdString();
      game.metadata.internationalTitle = query.value(8).toString().toStdString();
      game.metadata.copyright = query.value(9).toString().toStdString();
      game.metadata.productCode = query.value(10).toString().toStdString();
      game.metadata.region = query.value(11).toString().toStdString();
      game.metadata.romType = query.value(12).toString().toStdString();
      game.metadata.peripheralSupport = query.value(13).toString().toStdString();
      game.metadata.mapper = query.value(14).toString().toStdString();
      game.metadata.sha256 = query.value(15).toString().toStdString();
      game.metadata.notes = query.value(16).toString().toStdString();
      game.metadata.headerChecksum = optionalUnsigned16(query.value(17));
      game.metadata.computedChecksum = optionalUnsigned16(query.value(18));
      game.metadata.declaredRomSize = optionalUnsigned64(query.value(19));
      game.metadata.trackCount = query.value(20).toUInt();
      game.metadata.headerRecognized = query.value(21).toBool();
      game.lastModifiedEpochMilliseconds = query.value(22).toLongLong();
      game.favorite = query.value(23).toBool();
      if (!query.value(24).isNull()) {
        game.lastPlayedEpochMilliseconds = query.value(24).toLongLong();
      }
      game.playCount = query.value(25).toULongLong();
      game.artworkPath = pathFromQString(query.value(26).toString());
      result.games.push_back(std::move(game));
    }
    return result;
  }

  GameLibraryStatus setFavorite(std::int64_t gameId, bool favorite)
  {
    return updateGameField(
      gameId,
      QStringLiteral("UPDATE library_games SET favorite = ? WHERE id = ?"),
      favorite ? QVariant{1} : QVariant{0},
      "The game favorite could not be updated");
  }

  GameLibraryStatus setArtworkPath(
    std::int64_t gameId,
    const std::filesystem::path& artworkPath)
  {
    if (artworkPath.empty()) {
      return updateGameField(
        gameId,
        QStringLiteral("UPDATE library_games SET artwork_path = ? WHERE id = ?"),
        QStringLiteral(""),
        "The local artwork path could not be cleared");
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(artworkPath, error) || error) {
      return failure(
        GameLibraryError::invalidPath,
        "The selected local artwork file does not exist.");
    }
    const auto canonical = std::filesystem::canonical(artworkPath, error);
    if (error || canonical.native().size() > 4'096U) {
      return failure(
        GameLibraryError::invalidPath,
        "The selected local artwork path could not be resolved safely.");
    }
    return updateGameField(
      gameId,
      QStringLiteral("UPDATE library_games SET artwork_path = ? WHERE id = ?"),
      pathToQString(canonical),
      "The local artwork path could not be updated");
  }

  GameLibraryStatus recordLaunch(
    std::int64_t gameId,
    std::int64_t epochMilliseconds)
  {
    if (const auto ready = checkReady(); !ready) {
      return ready;
    }
    if (gameId <= 0 || epochMilliseconds < 0) {
      return failure(GameLibraryError::invalidRecord, "The game launch record is invalid.");
    }
    QSqlQuery query{database_};
    query.prepare(QStringLiteral(
      "UPDATE library_games SET last_played = ?, play_count = play_count + 1 "
      "WHERE id = ?"));
    query.addBindValue(static_cast<qlonglong>(epochMilliseconds));
    query.addBindValue(static_cast<qlonglong>(gameId));
    if (!query.exec()) {
      return queryFailure("The game launch could not be recorded", query);
    }
    return query.numRowsAffected() == 1
      ? GameLibraryStatus{}
      : failure(GameLibraryError::invalidRecord, "The game no longer exists.");
  }

  LibraryScanStartResult beginDirectoryScan(std::int64_t directoryId)
  {
    if (const auto ready = checkReady(); !ready) {
      return {.status = ready, .generation = 0};
    }
    if (scanActive_) {
      return {
        .status = failure(
          GameLibraryError::transactionFailed,
          "Another game-library scan transaction is already active."),
        .generation = 0,
      };
    }
    if (!directoryExists(directoryId)) {
      return {
        .status = failure(
          GameLibraryError::directoryNotFound,
          "The game-library directory no longer exists."),
        .generation = 0,
      };
    }
    if (!database_.transaction()) {
      return {
        .status = failure(
          GameLibraryError::transactionFailed,
          "The game-library scan transaction could not start: " +
            database_.lastError().text().toStdString()),
        .generation = 0,
      };
    }
    const auto generation = ++nextScanGeneration_;
    scanActive_ = true;
    scanDirectoryId_ = directoryId;
    scanGeneration_ = generation;
    return {.status = {}, .generation = generation};
  }

  GameLibraryStatus applyScanBatch(
    std::int64_t directoryId,
    std::int64_t generation,
    std::span<const LibraryScanRecord> records)
  {
    if (const auto active = checkScan(directoryId, generation); !active) {
      return active;
    }
    if (records.size() > GameLibraryDatabase::maximumScanBatchSize) {
      return failure(
        GameLibraryError::invalidRecord,
        "A game-library scan batch exceeds its fixed capacity.");
    }
    if (records.empty()) {
      return {};
    }

    QSqlQuery query{database_};
    if (!query.prepare(QStringLiteral(
      "INSERT INTO library_games("
      "directory_id, path, related_data_path, file_name, display_title, "
      "file_size, system, format, domestic_title, international_title, "
      "copyright, product_code, region, rom_type, peripheral_support, mapper, "
      "sha256, notes, header_checksum, computed_checksum, declared_rom_size, "
      "track_count, header_recognized, last_modified, scan_generation) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
      "?, ?, ?, ?, ?) "
      "ON CONFLICT(directory_id, path) DO UPDATE SET "
      "related_data_path=excluded.related_data_path, "
      "file_name=excluded.file_name, display_title=excluded.display_title, "
      "file_size=excluded.file_size, system=excluded.system, format=excluded.format, "
      "domestic_title=excluded.domestic_title, "
      "international_title=excluded.international_title, copyright=excluded.copyright, "
      "product_code=excluded.product_code, region=excluded.region, "
      "rom_type=excluded.rom_type, peripheral_support=excluded.peripheral_support, "
      "mapper=excluded.mapper, sha256=excluded.sha256, notes=excluded.notes, "
      "header_checksum=excluded.header_checksum, "
      "computed_checksum=excluded.computed_checksum, "
      "declared_rom_size=excluded.declared_rom_size, track_count=excluded.track_count, "
      "header_recognized=excluded.header_recognized, "
      "last_modified=excluded.last_modified, scan_generation=excluded.scan_generation"))) {
      return queryFailure("The game-library scan statement could not be prepared", query);
    }

    for (const auto& record : records) {
      const auto& metadata = record.metadata;
      if (metadata.path.empty() || metadata.sha256.size() != 64U ||
          metadata.fileSize > static_cast<std::uintmax_t>(
            std::numeric_limits<qlonglong>::max())) {
        return failure(
          GameLibraryError::invalidRecord,
          "A scanned game contains an invalid path, hash, or file size.");
      }
      query.addBindValue(static_cast<qlonglong>(directoryId));
      query.addBindValue(pathToQString(metadata.path));
      query.addBindValue(pathToQString(metadata.relatedDataPath));
      query.addBindValue(pathToQString(metadata.path.filename()));
      query.addBindValue(QString::fromStdString(metadata.displayTitle()));
      query.addBindValue(static_cast<qlonglong>(metadata.fileSize));
      query.addBindValue(static_cast<unsigned int>(metadata.system));
      query.addBindValue(QString::fromStdString(metadata.format));
      query.addBindValue(QString::fromStdString(metadata.domesticTitle));
      query.addBindValue(QString::fromStdString(metadata.internationalTitle));
      query.addBindValue(QString::fromStdString(metadata.copyright));
      query.addBindValue(QString::fromStdString(metadata.productCode));
      query.addBindValue(QString::fromStdString(metadata.region));
      query.addBindValue(QString::fromStdString(metadata.romType));
      query.addBindValue(QString::fromStdString(metadata.peripheralSupport));
      query.addBindValue(QString::fromStdString(metadata.mapper));
      query.addBindValue(QString::fromStdString(metadata.sha256));
      query.addBindValue(QString::fromStdString(metadata.notes));
      query.addBindValue(optionalInteger(metadata.headerChecksum));
      query.addBindValue(optionalInteger(metadata.computedChecksum));
      query.addBindValue(optionalInteger(metadata.declaredRomSize));
      query.addBindValue(static_cast<unsigned int>(metadata.trackCount));
      query.addBindValue(metadata.headerRecognized ? 1 : 0);
      query.addBindValue(static_cast<qlonglong>(record.lastModifiedEpochMilliseconds));
      query.addBindValue(static_cast<qlonglong>(generation));
      if (!query.exec()) {
        return queryFailure("A scanned game could not be indexed", query);
      }
      query.finish();
    }
    return {};
  }

  LibraryScanFinishResult finishDirectoryScan(
    std::int64_t directoryId,
    std::int64_t generation)
  {
    if (const auto active = checkScan(directoryId, generation); !active) {
      return {.status = active, .removedGames = 0U};
    }
    QSqlQuery query{database_};
    query.prepare(QStringLiteral(
      "DELETE FROM library_games WHERE directory_id = ? AND scan_generation <> ?"));
    query.addBindValue(static_cast<qlonglong>(directoryId));
    query.addBindValue(static_cast<qlonglong>(generation));
    if (!query.exec()) {
      static_cast<void>(database_.rollback());
      resetScan();
      return {
        .status = queryFailure("Stale game-library entries could not be removed", query),
        .removedGames = 0U,
      };
    }
    const auto removed = static_cast<std::size_t>(
      std::max<qint64>(query.numRowsAffected(), 0));
    if (!database_.commit()) {
      const auto message = database_.lastError().text().toStdString();
      static_cast<void>(database_.rollback());
      resetScan();
      return {
        .status = failure(
          GameLibraryError::transactionFailed,
          "The game-library scan could not be committed: " + message),
        .removedGames = 0U,
      };
    }
    resetScan();
    return {.status = {}, .removedGames = removed};
  }

  GameLibraryStatus cancelDirectoryScan()
  {
    if (const auto ready = checkReady(); !ready) {
      return ready;
    }
    if (!scanActive_) {
      return {};
    }
    const bool rolledBack = database_.rollback();
    const auto error = database_.lastError().text().toStdString();
    resetScan();
    return rolledBack
      ? GameLibraryStatus{}
      : failure(
          GameLibraryError::transactionFailed,
          "The game-library scan rollback failed: " + error);
  }

private:
  GameLibraryStatus checkThread() const
  {
    return ownerThread_ == std::this_thread::get_id()
      ? GameLibraryStatus{}
      : failure(
          GameLibraryError::wrongThread,
          "The game-library database was accessed from a different thread.");
  }

  GameLibraryStatus checkReady() const
  {
    if (const auto thread = checkThread(); !thread) {
      return thread;
    }
    return isOpen()
      ? GameLibraryStatus{}
      : failure(
          GameLibraryError::databaseUnavailable,
          "The game-library database is not open.");
  }

  GameLibraryStatus checkIntegrity()
  {
    QSqlQuery query{database_};
    if (!query.exec(QStringLiteral("PRAGMA quick_check(1)"))) {
      return indicatesCorruption(query.lastError())
        ? failure(GameLibraryError::databaseCorrupt,
            "The game-library database is corrupt.")
        : queryFailure("The game-library integrity check failed", query);
    }
    if (!query.next() || query.value(0).toString() != QStringLiteral("ok")) {
      return failure(
        GameLibraryError::databaseCorrupt,
        "The game-library database failed its integrity check.");
    }
    return {};
  }

  GameLibraryStatus recoverCorruptDatabase()
  {
    database_.close();
    recoveryBackupPath_ = unusedRecoveryPath(path_);
    std::error_code error;
    std::filesystem::rename(path_, recoveryBackupPath_, error);
    if (error) {
      return failure(
        GameLibraryError::recoveryFailed,
        "The corrupt game-library database could not be preserved: " +
          error.message());
    }
    for (const auto& suffix : {std::string{"-wal"}, std::string{"-shm"}}) {
      const std::filesystem::path sidecar{path_.string() + suffix};
      if (!std::filesystem::exists(sidecar, error) || error) {
        error.clear();
        continue;
      }
      std::filesystem::rename(
        sidecar,
        std::filesystem::path{recoveryBackupPath_.string() + suffix},
        error);
      if (error) {
        return failure(
          GameLibraryError::recoveryFailed,
          "A corrupt game-library sidecar could not be preserved: " +
            error.message());
      }
    }
    if (!database_.open()) {
      return failure(
        GameLibraryError::recoveryFailed,
        "A replacement game-library database could not be opened: " +
          database_.lastError().text().toStdString());
    }
    recoveredCorruption_ = true;
    return {};
  }

  GameLibraryStatus configureConnection()
  {
    QSqlQuery query{database_};
    for (const auto* statement : {
           "PRAGMA foreign_keys = ON",
           "PRAGMA journal_mode = WAL",
           "PRAGMA synchronous = NORMAL"}) {
      if (!query.exec(QString::fromLatin1(statement))) {
        return queryFailure("The game-library database could not be configured", query);
      }
    }
    return {};
  }

  GameLibraryStatus initializeSchema()
  {
    QSqlQuery versionQuery{database_};
    if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) ||
        !versionQuery.next()) {
      return queryFailure("The game-library schema version could not be read", versionQuery);
    }
    const auto version = versionQuery.value(0).toUInt();
    if (version > GameLibraryDatabase::currentSchemaVersion) {
      return failure(
        GameLibraryError::unsupportedSchema,
        "The game-library database was created by a newer application version.");
    }
    if (version == GameLibraryDatabase::currentSchemaVersion) {
      return validateSchema();
    }
    if (!database_.transaction()) {
      return failure(
        GameLibraryError::transactionFailed,
        "The game-library schema transaction could not start.");
    }
    QSqlQuery query{database_};
    const std::array statements{
      QStringLiteral(
        "CREATE TABLE IF NOT EXISTS library_directories("
        "id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, "
        "recursive INTEGER NOT NULL CHECK(recursive IN (0,1)), "
        "created_at INTEGER NOT NULL)"),
      QStringLiteral(
        "CREATE TABLE IF NOT EXISTS library_games("
        "id INTEGER PRIMARY KEY, directory_id INTEGER NOT NULL "
        "REFERENCES library_directories(id) ON DELETE CASCADE, "
        "path TEXT NOT NULL, related_data_path TEXT NOT NULL DEFAULT '', "
        "file_name TEXT NOT NULL, display_title TEXT NOT NULL, "
        "file_size INTEGER NOT NULL, system INTEGER NOT NULL, format TEXT NOT NULL, "
        "domestic_title TEXT NOT NULL DEFAULT '', "
        "international_title TEXT NOT NULL DEFAULT '', copyright TEXT NOT NULL DEFAULT '', "
        "product_code TEXT NOT NULL DEFAULT '', region TEXT NOT NULL DEFAULT '', "
        "rom_type TEXT NOT NULL DEFAULT '', peripheral_support TEXT NOT NULL DEFAULT '', "
        "mapper TEXT NOT NULL DEFAULT '', sha256 TEXT NOT NULL, notes TEXT NOT NULL DEFAULT '', "
        "header_checksum INTEGER, computed_checksum INTEGER, declared_rom_size INTEGER, "
        "track_count INTEGER NOT NULL DEFAULT 0, header_recognized INTEGER NOT NULL, "
        "last_modified INTEGER NOT NULL, favorite INTEGER NOT NULL DEFAULT 0, "
        "last_played INTEGER, play_count INTEGER NOT NULL DEFAULT 0, "
        "artwork_path TEXT NOT NULL DEFAULT '', scan_generation INTEGER NOT NULL, "
        "UNIQUE(directory_id, path))"),
      QStringLiteral(
        "CREATE INDEX IF NOT EXISTS library_games_title_idx "
        "ON library_games(display_title)"),
      QStringLiteral(
        "CREATE INDEX IF NOT EXISTS library_games_system_idx "
        "ON library_games(system)"),
      QStringLiteral(
        "CREATE INDEX IF NOT EXISTS library_games_favorite_idx "
        "ON library_games(favorite)"),
      QStringLiteral(
        "CREATE INDEX IF NOT EXISTS library_games_recent_idx "
        "ON library_games(last_played)"),
      QStringLiteral("PRAGMA user_version = 1"),
    };
    for (const auto& statement : statements) {
      if (!query.exec(statement)) {
        const auto status = queryFailure("The game-library schema could not be created", query);
        static_cast<void>(database_.rollback());
        return status;
      }
    }
    if (!database_.commit()) {
      const auto message = database_.lastError().text().toStdString();
      static_cast<void>(database_.rollback());
      return failure(
        GameLibraryError::transactionFailed,
        "The game-library schema could not be committed: " + message);
    }
    return validateSchema();
  }

  GameLibraryStatus validateSchema() const
  {
    QSqlQuery query{database_};
    for (const auto* statement : {
           "SELECT id, path, recursive, created_at FROM library_directories LIMIT 0",
           "SELECT id, directory_id, path, display_title, file_size, system, "
           "sha256, favorite, play_count, scan_generation "
           "FROM library_games LIMIT 0"}) {
      if (!query.exec(QString::fromLatin1(statement))) {
        return failure(
          GameLibraryError::databaseCorrupt,
          "The game-library schema is incomplete or invalid: " +
            query.lastError().text().toStdString());
      }
    }
    return {};
  }

  GameLibraryStatus queryFailure(
    std::string_view context,
    const QSqlQuery& query) const
  {
    return failure(
      GameLibraryError::queryFailed,
      std::string{context} + ": " + query.lastError().text().toStdString());
  }

  bool directoryExists(std::int64_t directoryId) const
  {
    QSqlQuery query{database_};
    query.prepare(QStringLiteral("SELECT 1 FROM library_directories WHERE id = ?"));
    query.addBindValue(static_cast<qlonglong>(directoryId));
    return query.exec() && query.next();
  }

  GameLibraryStatus updateGameField(
    std::int64_t gameId,
    const QString& statement,
    const QVariant& value,
    std::string_view context)
  {
    if (const auto ready = checkReady(); !ready) {
      return ready;
    }
    if (gameId <= 0) {
      return failure(GameLibraryError::invalidRecord, "The game ID is invalid.");
    }
    QSqlQuery query{database_};
    query.prepare(statement);
    query.addBindValue(value);
    query.addBindValue(static_cast<qlonglong>(gameId));
    if (!query.exec()) {
      return queryFailure(context, query);
    }
    return query.numRowsAffected() == 1
      ? GameLibraryStatus{}
      : failure(GameLibraryError::invalidRecord, "The game no longer exists.");
  }

  GameLibraryStatus checkScan(
    std::int64_t directoryId,
    std::int64_t generation) const
  {
    if (const auto ready = checkReady(); !ready) {
      return ready;
    }
    return scanActive_ && directoryId == scanDirectoryId_ &&
        generation == scanGeneration_
      ? GameLibraryStatus{}
      : failure(
          GameLibraryError::scanNotActive,
          "The game-library scan transaction is not active.");
  }

  void resetScan() noexcept
  {
    scanActive_ = false;
    scanDirectoryId_ = 0;
    scanGeneration_ = 0;
  }

  std::filesystem::path path_;
  std::filesystem::path recoveryBackupPath_;
  QString connectionName_;
  QSqlDatabase database_;
  std::thread::id ownerThread_;
  std::int64_t nextScanGeneration_{currentEpochMilliseconds()};
  std::int64_t scanDirectoryId_{0};
  std::int64_t scanGeneration_{0};
  bool initialized_{false};
  bool recoveredCorruption_{false};
  bool scanActive_{false};
};

GameLibraryDatabase::GameLibraryDatabase(std::filesystem::path databasePath)
  : private_(std::make_unique<Private>(std::move(databasePath)))
{
}

GameLibraryDatabase::~GameLibraryDatabase() = default;

GameLibraryStatus GameLibraryDatabase::initialize()
{
  return private_->initialize();
}

bool GameLibraryDatabase::isOpen() const noexcept { return private_->isOpen(); }

const std::filesystem::path& GameLibraryDatabase::path() const noexcept
{
  return private_->path();
}

bool GameLibraryDatabase::recoveredCorruption() const noexcept
{
  return private_->recoveredCorruption();
}

const std::filesystem::path& GameLibraryDatabase::recoveryBackupPath() const noexcept
{
  return private_->recoveryBackupPath();
}

LibraryDirectoryResult GameLibraryDatabase::addDirectory(
  const std::filesystem::path& path,
  bool recursive)
{
  return private_->addDirectory(path, recursive);
}

GameLibraryStatus GameLibraryDatabase::updateDirectory(
  std::int64_t directoryId,
  bool recursive)
{
  return private_->updateDirectory(directoryId, recursive);
}

GameLibraryStatus GameLibraryDatabase::removeDirectory(std::int64_t directoryId)
{
  return private_->removeDirectory(directoryId);
}

LibraryDirectoriesResult GameLibraryDatabase::directories() const
{
  return private_->directories();
}

LibraryGamesResult GameLibraryDatabase::games() const
{
  return private_->games();
}

GameLibraryStatus GameLibraryDatabase::setFavorite(
  std::int64_t gameId,
  bool favorite)
{
  return private_->setFavorite(gameId, favorite);
}

GameLibraryStatus GameLibraryDatabase::setArtworkPath(
  std::int64_t gameId,
  const std::filesystem::path& artworkPath)
{
  return private_->setArtworkPath(gameId, artworkPath);
}

GameLibraryStatus GameLibraryDatabase::recordLaunch(
  std::int64_t gameId,
  std::int64_t epochMilliseconds)
{
  return private_->recordLaunch(gameId, epochMilliseconds);
}

LibraryScanStartResult GameLibraryDatabase::beginDirectoryScan(
  std::int64_t directoryId)
{
  return private_->beginDirectoryScan(directoryId);
}

GameLibraryStatus GameLibraryDatabase::applyScanBatch(
  std::int64_t directoryId,
  std::int64_t generation,
  std::span<const LibraryScanRecord> records)
{
  return private_->applyScanBatch(directoryId, generation, records);
}

LibraryScanFinishResult GameLibraryDatabase::finishDirectoryScan(
  std::int64_t directoryId,
  std::int64_t generation)
{
  return private_->finishDirectoryScan(directoryId, generation);
}

GameLibraryStatus GameLibraryDatabase::cancelDirectoryScan()
{
  return private_->cancelDirectoryScan();
}

} // namespace genplusgx::library
