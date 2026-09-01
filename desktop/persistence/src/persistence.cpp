#include "genplusgx/persistence.h"

#include "genplusgx/game_file.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <system_error>
#include <utility>

namespace genplusgx {
namespace {

constexpr std::size_t hashByteCount = 32U;
constexpr std::string_view cueHashDomain{"GENPLUSGX-CUE-CONTENT-V1"};

PersistenceStatus success()
{
  return {};
}

PersistenceStatus failure(PersistenceError error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

QString pathString(const std::filesystem::path& path)
{
#if defined(_WIN32)
  return QString::fromStdWString(path.native());
#else
  return QString::fromUtf8(path.native());
#endif
}

bool createDirectory(const std::filesystem::path& path, std::error_code& error)
{
  const auto status = std::filesystem::status(path, error);
  if (!error) {
    return std::filesystem::is_directory(status) ||
           (!std::filesystem::exists(status) &&
            std::filesystem::create_directories(path, error) && !error);
  }
  if (error != std::errc::no_such_file_or_directory) {
    return false;
  }
  error.clear();
  return std::filesystem::create_directories(path, error) && !error;
}

bool isReservedWindowsName(std::string_view name)
{
  const auto dot = name.find('.');
  std::string base{name.substr(0U, dot)};
  std::ranges::transform(base, base.begin(), [](unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL") {
    return true;
  }
  if (base.size() == 4U && (base.starts_with("COM") || base.starts_with("LPT")) &&
      base[3] >= '1' && base[3] <= '9') {
    return true;
  }
  return false;
}

const char* saveFilename(SaveRamKind kind)
{
  switch (kind) {
    case SaveRamKind::cartridge:
      return "cartridge.srm";
    case SaveRamKind::scdInternal:
      return "scd-internal.brm";
    case SaveRamKind::scdRamCartridge:
      return "scd-cartridge.brm";
  }
  return "unknown.ram";
}

void addUnsigned64(QCryptographicHash& hash, std::uint64_t value)
{
  std::array<char, 8U> encoded{};
  for (std::size_t index = 0U; index < encoded.size(); ++index) {
    encoded[encoded.size() - index - 1U] = static_cast<char>(value & 0xffU);
    value >>= 8U;
  }
  hash.addData(QByteArrayView{encoded.data(), static_cast<qsizetype>(encoded.size())});
}

} // namespace

ApplicationPaths::ApplicationPaths(std::filesystem::path root)
  : root_(std::move(root))
{
}

ApplicationPaths ApplicationPaths::fromPlatform()
{
  const auto location = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  return ApplicationPaths{std::filesystem::path{location.toStdString()}};
}

PersistenceStatus ApplicationPaths::initialize() const
{
  if (root_.empty() || !root_.is_absolute()) {
    return failure(PersistenceError::invalidRoot, "The application-data root must be an absolute path.");
  }

  const std::array directories{
    root_, configDirectory(), savesDirectory(), statesDirectory(), screenshotsDirectory(),
    libraryDirectory(), logsDirectory(), cacheDirectory()};
  for (const auto& directory : directories) {
    std::error_code error;
    if (!createDirectory(directory, error)) {
      return failure(
        PersistenceError::directoryCreationFailed,
        "Unable to create application-data directory: " + directory.string());
    }
  }
  return success();
}

const std::filesystem::path& ApplicationPaths::root() const noexcept
{
  return root_;
}

std::filesystem::path ApplicationPaths::configDirectory() const
{
  return root_ / "config";
}

std::filesystem::path ApplicationPaths::savesDirectory() const
{
  return root_ / "saves";
}

std::filesystem::path ApplicationPaths::statesDirectory() const
{
  return root_ / "states";
}

std::filesystem::path ApplicationPaths::screenshotsDirectory() const
{
  return root_ / "screenshots";
}

std::filesystem::path ApplicationPaths::libraryDirectory() const
{
  return root_ / "library";
}

std::filesystem::path ApplicationPaths::logsDirectory() const
{
  return root_ / "logs";
}

std::filesystem::path ApplicationPaths::cacheDirectory() const
{
  return root_ / "cache";
}

bool GameIdentity::valid() const noexcept
{
  return sha256.size() == (hashByteCount * 2U) &&
         std::ranges::all_of(sha256, [](unsigned char character) {
           return std::isdigit(character) != 0 ||
                  (character >= static_cast<unsigned char>('a') &&
                   character <= static_cast<unsigned char>('f'));
         }) &&
         !titleSlug.empty() && titleSlug.size() <= 64U &&
         sanitizeFilename(titleSlug) == titleSlug;
}

std::string GameIdentity::directoryName() const
{
  return titleSlug + '-' + sha256;
}

std::string sanitizeFilename(std::string_view input, std::size_t maximumLength)
{
  if (maximumLength == 0U) {
    return {};
  }

  std::string result;
  result.reserve(std::min(input.size(), maximumLength));
  bool previousWasReplacement = false;
  for (const auto rawCharacter : input) {
    const auto character = static_cast<unsigned char>(rawCharacter);
    const bool isSafe = std::isalnum(character) != 0 || character == '-' ||
                        character == '_' || character == '.';
    const char output = isSafe ? static_cast<char>(character) : '_';
    if (output == '_' && previousWasReplacement) {
      continue;
    }
    result.push_back(output);
    previousWasReplacement = output == '_';
    if (result.size() == maximumLength) {
      break;
    }
  }

  const auto isEdgeCharacter = [](char character) {
    return character == '.' || character == '_' || character == ' ';
  };
  while (!result.empty() && isEdgeCharacter(result.front())) {
    result.erase(result.begin());
  }
  while (!result.empty() && isEdgeCharacter(result.back())) {
    result.pop_back();
  }
  if (result.empty() || result == "." || result == "..") {
    result = "game";
  }
  if (isReservedWindowsName(result)) {
    result.insert(result.begin(), '_');
    if (result.size() > maximumLength) {
      result.resize(maximumLength);
    }
  }
  return result;
}

GameContentHashResult hashGameContent(
  const std::filesystem::path& path,
  const GameContentObserver& primaryFileObserver,
  const GameContentCancellation& cancellationRequested)
{
  auto content = gameContentFiles(path);
  if (!content.status) {
    return {
      .status = failure(PersistenceError::invalidData, content.status.message),
      .sha256 = {},
      .primaryFileSize = 0U,
    };
  }

  QCryptographicHash hash{QCryptographicHash::Sha256};
  const bool composite = content.files.size() > 1U;
  if (composite) {
    hash.addData(QByteArrayView{
      cueHashDomain.data(), static_cast<qsizetype>(cueHashDomain.size())});
    addUnsigned64(hash, static_cast<std::uint64_t>(content.files.size()));
  }

  std::array<char, 64U * 1024U> buffer{};
  std::uintmax_t primaryFileSize = 0U;
  for (std::size_t fileIndex = 0U; fileIndex < content.files.size(); ++fileIndex) {
    if (cancellationRequested && cancellationRequested()) {
      return {
        .status = failure(PersistenceError::cancelled,
          "Game-content hashing was cancelled."),
        .sha256 = {},
        .primaryFileSize = 0U,
      };
    }
    const QFileInfo before{pathString(content.files[fileIndex])};
    if (before.size() < 0) {
      return {
        .status = failure(PersistenceError::fileReadFailed,
          "Unable to read a game-content file size while hashing."),
        .sha256 = {},
        .primaryFileSize = 0U,
      };
    }
    const auto expectedSize = static_cast<std::uintmax_t>(before.size());
    if (fileIndex == 0U) {
      primaryFileSize = expectedSize;
    }
    if (composite) {
      addUnsigned64(hash, static_cast<std::uint64_t>(expectedSize));
    }

    QFile input{pathString(content.files[fileIndex])};
    if (!input.open(QIODevice::ReadOnly)) {
      return {
        .status = failure(PersistenceError::fileOpenFailed,
          "Unable to open game content for hashing."),
        .sha256 = {},
        .primaryFileSize = 0U,
      };
    }

    std::uintmax_t offset = 0U;
    while (true) {
      if (cancellationRequested && cancellationRequested()) {
        return {
          .status = failure(PersistenceError::cancelled,
            "Game-content hashing was cancelled."),
          .sha256 = {},
          .primaryFileSize = 0U,
        };
      }
      const auto bytesRead = input.read(
        buffer.data(), static_cast<qint64>(buffer.size()));
      if (bytesRead < 0) {
        return {
          .status = failure(PersistenceError::hashFailed,
            "Unable to read game content while hashing."),
          .sha256 = {},
          .primaryFileSize = 0U,
        };
      }
      if (bytesRead == 0) {
        break;
      }
      hash.addData(QByteArrayView{buffer.data(), bytesRead});
      if (fileIndex == 0U && primaryFileObserver) {
        primaryFileObserver({
          reinterpret_cast<const std::uint8_t*>(buffer.data()),
          static_cast<std::size_t>(bytesRead)}, offset);
      }
      offset += static_cast<std::uintmax_t>(bytesRead);
    }

    const QFileInfo after{pathString(content.files[fileIndex])};
    if (offset != expectedSize || after.size() != before.size() ||
        after.lastModified() != before.lastModified()) {
      return {
        .status = failure(PersistenceError::hashFailed,
          "Game content changed while it was being hashed."),
        .sha256 = {},
        .primaryFileSize = 0U,
      };
    }
  }

  if (composite) {
    const auto verifiedContent = gameContentFiles(path);
    if (!verifiedContent.status || verifiedContent.files != content.files) {
      return {
        .status = failure(PersistenceError::hashFailed,
          "The CUE sheet or its referenced content changed while hashing."),
        .sha256 = {},
        .primaryFileSize = 0U,
      };
    }
  }

  return {
    .status = success(),
    .sha256 = hash.result().toHex().toStdString(),
    .primaryFileSize = primaryFileSize,
  };
}

GameIdentityResult identifyGame(
  const std::filesystem::path& path,
  std::string_view preferredTitle,
  const GameContentCancellation& cancellationRequested)
{
  const auto contentHash = hashGameContent(path, {}, cancellationRequested);
  if (!contentHash.status) {
    return {.status = contentHash.status, .identity = {}};
  }

  const std::string fallbackTitle = path.stem().string();
  const auto title = preferredTitle.empty() ? std::string_view{fallbackTitle} : preferredTitle;
  return {
    .status = success(),
    .identity = {
      .sha256 = contentHash.sha256,
      .titleSlug = sanitizeFilename(title),
    },
  };
}

PersistenceStore::PersistenceStore(ApplicationPaths paths)
  : paths_(std::move(paths))
{
}

PersistenceStatus writeFileAtomically(
  const std::filesystem::path& destination,
  std::span<const std::uint8_t> data,
  std::size_t maximumBytes)
{
  if (data.size() > maximumBytes ||
      data.size() > static_cast<std::size_t>(std::numeric_limits<qint64>::max())) {
    return failure(PersistenceError::dataTooLarge, "The payload exceeds the bounded maximum.");
  }

  std::error_code directoryError;
  if (!createDirectory(destination.parent_path(), directoryError)) {
    return failure(PersistenceError::directoryCreationFailed, "Unable to create the destination directory.");
  }

  QSaveFile output{pathString(destination)};
  output.setDirectWriteFallback(false);
  if (!output.open(QIODevice::WriteOnly)) {
    return failure(PersistenceError::fileOpenFailed, "Unable to create the atomic file transaction.");
  }
  const auto requested = static_cast<qint64>(data.size());
  const auto written = output.write(
    reinterpret_cast<const char*>(data.data()), requested);
  if (written != requested) {
    output.cancelWriting();
    return failure(PersistenceError::fileWriteFailed, "Unable to write the complete payload.");
  }
  if (!output.commit()) {
    return failure(PersistenceError::fileCommitFailed, "Unable to atomically replace the destination file.");
  }
  return success();
}

PersistenceLoadResult readFileBounded(
  const std::filesystem::path& source,
  std::size_t maximumBytes)
{
  const QFileInfo information{pathString(source)};
  if (!information.exists()) {
    return {.status = success(), .exists = false, .data = {}};
  }
  if (!information.isFile()) {
    return {
      .status = failure(PersistenceError::fileReadFailed, "The source path is not a regular file."),
      .exists = true,
      .data = {},
    };
  }
  if (information.size() < 0 ||
      static_cast<quint64>(information.size()) > static_cast<quint64>(maximumBytes)) {
    return {
      .status = failure(PersistenceError::dataTooLarge, "The file exceeds its bounded maximum."),
      .exists = true,
      .data = {},
    };
  }

  QFile input{pathString(source)};
  if (!input.open(QIODevice::ReadOnly)) {
    return {
      .status = failure(PersistenceError::fileOpenFailed, "Unable to open the file."),
      .exists = true,
      .data = {},
    };
  }
  std::vector<std::uint8_t> data(static_cast<std::size_t>(information.size()));
  const auto read = input.read(
    reinterpret_cast<char*>(data.data()), static_cast<qint64>(data.size()));
  if (read != static_cast<qint64>(data.size())) {
    return {
      .status = failure(PersistenceError::fileReadFailed, "Unable to read the complete file."),
      .exists = true,
      .data = {},
    };
  }
  return {.status = success(), .exists = true, .data = std::move(data)};
}

PersistenceStatus PersistenceStore::initialize() const
{
  return paths_.initialize();
}

const ApplicationPaths& PersistenceStore::paths() const noexcept
{
  return paths_;
}

std::filesystem::path PersistenceStore::gameSaveDirectory(
  const GameIdentity& identity) const
{
  return paths_.savesDirectory() / identity.directoryName();
}

std::filesystem::path PersistenceStore::ramPath(
  const GameIdentity& identity,
  SaveRamKind kind) const
{
  return gameSaveDirectory(identity) / saveFilename(kind);
}

PersistenceStatus PersistenceStore::saveRam(
  const GameIdentity& identity,
  SaveRamKind kind,
  std::span<const std::uint8_t> data) const
{
  if (!identity.valid()) {
    return failure(PersistenceError::invalidGameIdentity, "The game identity is invalid.");
  }
  return writeFileAtomically(ramPath(identity, kind), data, maximumRamBytes);
}

PersistenceLoadResult PersistenceStore::loadRam(
  const GameIdentity& identity,
  SaveRamKind kind,
  std::size_t maximumBytes) const
{
  if (!identity.valid()) {
    return {
      .status = failure(PersistenceError::invalidGameIdentity, "The game identity is invalid."),
      .exists = false,
      .data = {},
    };
  }

  return readFileBounded(ramPath(identity, kind), maximumBytes);
}

} // namespace genplusgx
