#include "genplusgx/recent_games.h"

#include "genplusgx/game_file.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <algorithm>
#include <cwctype>
#include <optional>
#include <system_error>
#include <utility>

namespace genplusgx {
namespace {

PersistenceStatus failure(std::string message)
{
  return {
    .error = PersistenceError::invalidData,
    .message = std::move(message),
  };
}

QString pathString(const std::filesystem::path& path)
{
#if defined(_WIN32)
  return QString::fromStdWString(path.native());
#else
  return QString::fromUtf8(path.native());
#endif
}

std::filesystem::path filesystemPath(const QString& path)
{
#if defined(_WIN32)
  return std::filesystem::path{path.toStdWString()};
#else
  return std::filesystem::path{path.toUtf8().constData()};
#endif
}

std::optional<std::filesystem::path> normalizedPath(
  const std::filesystem::path& path)
{
  if (path.empty()) {
    return std::nullopt;
  }
  std::error_code error;
  auto absolute = path.is_absolute() ? path : std::filesystem::absolute(path, error);
  if (error || absolute.empty()) {
    return std::nullopt;
  }
  absolute = absolute.lexically_normal();
  if (absolute.string().size() > maximumCorePathBytes) {
    return std::nullopt;
  }
  return absolute;
}

bool pathsEqual(
  const std::filesystem::path& left,
  const std::filesystem::path& right)
{
#if defined(_WIN32)
  auto leftText = left.native();
  auto rightText = right.native();
  std::ranges::transform(leftText, leftText.begin(), [](wchar_t character) {
    return static_cast<wchar_t>(std::towlower(character));
  });
  std::ranges::transform(rightText, rightText.begin(), [](wchar_t character) {
    return static_cast<wchar_t>(std::towlower(character));
  });
  return leftText == rightText;
#else
  return left == right;
#endif
}

RecentGamesLoadResult invalidResult(std::string message)
{
  return {
    .status = failure(std::move(message)),
    .model = {},
    .migrated = false,
  };
}

bool addCurrentEntries(const QJsonArray& entries, RecentGamesModel& model)
{
  if (entries.size() > static_cast<qsizetype>(RecentGamesModel::maximumEntries)) {
    return false;
  }
  for (qsizetype index = entries.size(); index > 0; --index) {
    const auto value = entries.at(index - 1);
    if (!value.isObject()) {
      return false;
    }
    const auto object = value.toObject();
    const auto pathValue = object.value(QStringLiteral("path"));
    const auto timeValue = object.value(QStringLiteral("lastOpenedMs"));
    if (!pathValue.isString() || !timeValue.isDouble()) {
      return false;
    }
    const auto time = timeValue.toInteger(-1);
    const auto path = filesystemPath(pathValue.toString());
    if (time < 0 || !path.is_absolute() || !model.add(path, time)) {
      return false;
    }
  }
  return model.size() == static_cast<std::size_t>(entries.size());
}

} // namespace

bool RecentGamesModel::add(
  const std::filesystem::path& path,
  std::int64_t lastOpenedMilliseconds)
{
  const auto normalized = normalizedPath(path);
  if (!normalized || lastOpenedMilliseconds < 0) {
    return false;
  }
  const auto existing = std::ranges::find_if(entries_, [&normalized](const auto& entry) {
    return pathsEqual(entry.path, *normalized);
  });
  if (existing != entries_.end()) {
    entries_.erase(existing);
  }
  entries_.insert(entries_.begin(), RecentGame{
    .path = *normalized,
    .lastOpenedMilliseconds = lastOpenedMilliseconds,
  });
  if (entries_.size() > maximumEntries) {
    entries_.resize(maximumEntries);
  }
  return true;
}

bool RecentGamesModel::remove(const std::filesystem::path& path)
{
  const auto normalized = normalizedPath(path);
  if (!normalized) {
    return false;
  }
  const auto existing = std::ranges::find_if(entries_, [&normalized](const auto& entry) {
    return pathsEqual(entry.path, *normalized);
  });
  if (existing == entries_.end()) {
    return false;
  }
  entries_.erase(existing);
  return true;
}

void RecentGamesModel::clear() noexcept
{
  entries_.clear();
}

bool RecentGamesModel::empty() const noexcept
{
  return entries_.empty();
}

std::size_t RecentGamesModel::size() const noexcept
{
  return entries_.size();
}

const std::vector<RecentGame>& RecentGamesModel::entries() const noexcept
{
  return entries_;
}

RecentGamesStore::RecentGamesStore(std::filesystem::path path)
  : path_(std::move(path))
{
}

const std::filesystem::path& RecentGamesStore::path() const noexcept
{
  return path_;
}

RecentGamesLoadResult RecentGamesStore::load() const
{
  const auto loaded = readFileBounded(path_, maximumFileBytes);
  if (!loaded.status) {
    return {
      .status = loaded.status,
      .model = {},
      .migrated = false,
    };
  }
  if (!loaded.exists) {
    return {};
  }

  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(
    QByteArray{reinterpret_cast<const char*>(loaded.data.data()),
      static_cast<qsizetype>(loaded.data.size())},
    &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return invalidResult("The recent-games file is not valid JSON.");
  }
  const auto root = document.object();
  const auto schemaValue = root.value(QStringLiteral("schemaVersion"));
  if (!schemaValue.isDouble()) {
    return invalidResult("The recent-games schema version is missing.");
  }
  const auto schema = schemaValue.toInt(-1);
  RecentGamesModel model;
  if (schema == 0) {
    const auto paths = root.value(QStringLiteral("paths"));
    if (!paths.isArray() ||
        paths.toArray().size() > static_cast<qsizetype>(RecentGamesModel::maximumEntries)) {
      return invalidResult("The legacy recent-games list is invalid.");
    }
    const auto array = paths.toArray();
    for (qsizetype index = array.size(); index > 0; --index) {
      const auto value = array.at(index - 1);
      if (!value.isString()) {
        return invalidResult("The legacy recent-games list contains an invalid path.");
      }
      const auto candidate = filesystemPath(value.toString());
      if (!candidate.is_absolute() || !model.add(candidate, 0)) {
        return invalidResult("The legacy recent-games list contains an unsafe path.");
      }
    }
    if (model.size() != static_cast<std::size_t>(array.size())) {
      return invalidResult("The legacy recent-games list contains duplicates.");
    }
    return {.status = {}, .model = std::move(model), .migrated = true};
  }
  if (schema != static_cast<int>(schemaVersion)) {
    return invalidResult("The recent-games schema version is not supported.");
  }
  const auto entries = root.value(QStringLiteral("entries"));
  if (!entries.isArray() || !addCurrentEntries(entries.toArray(), model)) {
    return invalidResult("The recent-games entries are invalid.");
  }
  return {.status = {}, .model = std::move(model), .migrated = false};
}

PersistenceStatus RecentGamesStore::save(const RecentGamesModel& model) const
{
  QJsonArray entries;
  for (const auto& entry : model.entries()) {
    entries.push_back(QJsonObject{
      {QStringLiteral("path"), pathString(entry.path)},
      {QStringLiteral("lastOpenedMs"),
        QJsonValue{static_cast<qint64>(entry.lastOpenedMilliseconds)}},
    });
  }
  const QJsonDocument document{QJsonObject{
    {QStringLiteral("schemaVersion"), static_cast<int>(schemaVersion)},
    {QStringLiteral("entries"), entries},
  }};
  const auto json = document.toJson(QJsonDocument::Indented);
  return writeFileAtomically(
    path_,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(json.constData()),
      static_cast<std::size_t>(json.size())},
    maximumFileBytes);
}

} // namespace genplusgx
