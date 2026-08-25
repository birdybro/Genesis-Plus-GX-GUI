#include "genplusgx/recent_games.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

bool check(bool condition, std::string_view message)
{
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

bool writeJson(const std::filesystem::path& path, const QJsonObject& object)
{
  QFile file{QString::fromStdString(path.string())};
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  const auto bytes = QJsonDocument{object}.toJson(QJsonDocument::Compact);
  return file.write(bytes) == bytes.size();
}

} // namespace

int main()
{
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "temporary directory creation failed")) {
    return 1;
  }
  const auto root = std::filesystem::path{temporary.path().toStdString()};
  genplusgx::RecentGamesModel model;
  for (std::size_t index = 0; index < 15U; ++index) {
    const auto added = model.add(
      root / ("game-" + std::to_string(index) + ".md"),
      static_cast<std::int64_t>(index));
    if (!check(added, "valid recent path was rejected")) {
      return 2;
    }
  }
  if (!check(model.size() == genplusgx::RecentGamesModel::maximumEntries,
        "recent history was not bounded") ||
      !check(model.entries().front().path.filename() == "game-14.md" &&
          model.entries().back().path.filename() == "game-3.md",
        "recent insertion order or truncation was wrong") ||
      !check(model.add(root / "game-9.md", 99),
        "duplicate path update was rejected") ||
      !check(model.size() == genplusgx::RecentGamesModel::maximumEntries &&
          model.entries().front().path.filename() == "game-9.md" &&
          model.entries().front().lastOpenedMilliseconds == 99,
        "duplicate path did not move to the front") ||
      !check(model.remove(root / "game-8.md") &&
          !model.remove(root / "game-does-not-exist.md"),
        "recent path removal semantics were wrong") ||
      !check(!model.add({}, 100) && !model.add(root / "negative.md", -1) &&
          !model.add(root / (std::string(300U, 'a') + ".md"), 100),
        "invalid recent entry was accepted")) {
    return 3;
  }

  const auto storePath = root / "config" / "recent-games.json";
  const genplusgx::RecentGamesStore store{storePath};
  if (!check(store.save(model), "recent list atomic save failed")) {
    return 4;
  }
  const auto loaded = store.load();
  if (!check(loaded.status && !loaded.migrated &&
          loaded.model.entries() == model.entries(),
        "recent list exact round trip failed")) {
    return 5;
  }

  const genplusgx::RecentGamesStore missingStore{root / "missing.json"};
  const auto missing = missingStore.load();
  if (!check(missing.status && missing.model.empty(),
        "missing recent list did not produce an empty success")) {
    return 6;
  }

  const auto legacyFirst = root / "legacy-first.md";
  const auto legacySecond = root / "legacy-second.gg";
  if (!check(writeJson(storePath, QJsonObject{
        {QStringLiteral("schemaVersion"), 0},
        {QStringLiteral("paths"), QJsonArray{
          QString::fromStdString(legacyFirst.string()),
          QString::fromStdString(legacySecond.string())}},
      }), "legacy fixture write failed")) {
    return 7;
  }
  const auto migrated = store.load();
  if (!check(migrated.status && migrated.migrated && migrated.model.size() == 2U &&
          migrated.model.entries().front().path == legacyFirst &&
          migrated.model.entries().back().path == legacySecond,
        "schema 0 recent list did not migrate in order")) {
    return 8;
  }

  if (!check(writeJson(storePath, QJsonObject{
        {QStringLiteral("schemaVersion"), 999},
        {QStringLiteral("entries"), QJsonArray{}}}),
        "future-schema fixture write failed")) {
    return 9;
  }
  const auto future = store.load();
  if (!check(future.status.error == genplusgx::PersistenceError::invalidData &&
          future.model.empty(),
        "future schema was not rejected without data exposure")) {
    return 10;
  }

  QFile corrupt{QString::fromStdString(storePath.string())};
  if (!check(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
          corrupt.write("{not-json", 9) == 9,
        "corruption fixture write failed")) {
    return 11;
  }
  corrupt.close();
  const auto invalid = store.load();
  if (!check(invalid.status.error == genplusgx::PersistenceError::invalidData &&
          invalid.model.empty(),
        "corrupt recent data was not isolated")) {
    return 12;
  }

  model.clear();
  return check(model.empty(), "clear did not empty recent history") ? 0 : 13;
}
