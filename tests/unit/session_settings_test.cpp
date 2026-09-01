#include "genplusgx/settings/session_settings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>

namespace {

bool check(bool condition, const std::string& message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

} // namespace

int main()
{
  using namespace genplusgx;
  using namespace genplusgx::settings;

  QTemporaryDir directory;
  if (!check(directory.isValid(), "Could not create session settings directory")) {
    return EXIT_FAILURE;
  }
  const auto root = std::filesystem::path{directory.path().toStdString()};
  SessionSettingsStore store{root / "nested" / "session-settings.json"};
  const auto missing = store.load();
  if (!check(missing.status && missing.settings == defaultSessionSettings(),
        "Missing session settings did not use opt-in defaults")) {
    return EXIT_FAILURE;
  }

  const SessionSettings enabled{
    .resumeOnLaunch = true,
    .lastGamePath = root / std::filesystem::path{u8"RÖMs"} / "game.md",
    .lastPatchPath = root / std::filesystem::path{u8"PÄtches"} / "game.bps",
  };
  if (!check(store.save(enabled), "Session settings could not be saved")) {
    return EXIT_FAILURE;
  }
  const auto loaded = store.load();
  if (!check(loaded.status && loaded.settings == enabled,
        "Session settings and Unicode game path did not round-trip")) {
    return EXIT_FAILURE;
  }

  const SessionSettings disabled{
    .resumeOnLaunch = false,
    .lastGamePath = std::nullopt,
    .lastPatchPath = std::nullopt,
  };
  if (!check(store.save(disabled), "Disabled session settings could not be saved") ||
      !check(store.load().settings == disabled,
        "Cleared session marker was not persisted")) {
    return EXIT_FAILURE;
  }

  const SessionSettings emptyPath{
    .resumeOnLaunch = true,
    .lastGamePath = std::filesystem::path{},
    .lastPatchPath = std::nullopt,
  };
  if (!check(!validateSessionSettings(emptyPath) && !store.save(emptyPath),
        "An invalid empty session path was accepted")) {
    return EXIT_FAILURE;
  }
  const SessionSettings relativePath{
    .resumeOnLaunch = true,
    .lastGamePath = std::filesystem::path{"relative-game.md"},
    .lastPatchPath = std::nullopt,
  };
  if (!check(!validateSessionSettings(relativePath) && !store.save(relativePath),
        "A working-directory-dependent session path was accepted")) {
    return EXIT_FAILURE;
  }

  const SessionSettings orphanPatch{
    .resumeOnLaunch = true,
    .lastGamePath = std::nullopt,
    .lastPatchPath = root / "orphan.ips",
  };
  if (!check(!validateSessionSettings(orphanPatch) && !store.save(orphanPatch),
        "A patch without a session game was accepted")) {
    return EXIT_FAILURE;
  }

  const auto legacyPath = root / "legacy.md";
#if defined(_WIN32)
  const auto legacyPathText = QString::fromStdWString(legacyPath.native());
#else
  const auto legacyPathText = QString::fromUtf8(legacyPath.native());
#endif
  const auto legacy = QJsonDocument{QJsonObject{
    {QStringLiteral("schemaVersion"), 1},
    {QStringLiteral("resumeOnLaunch"), true},
    {QStringLiteral("lastGamePath"), legacyPathText},
  }}.toJson(QJsonDocument::Compact);
  if (!check(writeFileAtomically(store.path(),
        std::span<const std::uint8_t>{
          reinterpret_cast<const std::uint8_t*>(legacy.constData()),
          static_cast<std::size_t>(legacy.size())},
        SessionSettingsStore::maximumFileBytes),
        "Could not write legacy session fixture")) {
    return EXIT_FAILURE;
  }
  const auto migrated = store.load();
  if (!check(migrated.status && migrated.migrated &&
        migrated.settings.lastGamePath == legacyPath &&
        !migrated.settings.lastPatchPath,
        "Schema-1 session settings were not migrated safely")) {
    return EXIT_FAILURE;
  }
  if (!check(store.save(migrated.settings) && !store.load().migrated,
        "Migrated session settings could not be rewritten as schema 2")) {
    return EXIT_FAILURE;
  }

  const std::string corrupt =
    "{\"schemaVersion\":2,\"resumeOnLaunch\":\"yes\"}";
  if (!check(writeFileAtomically(store.path(),
        std::span<const std::uint8_t>{
          reinterpret_cast<const std::uint8_t*>(corrupt.data()), corrupt.size()},
        SessionSettingsStore::maximumFileBytes),
        "Could not write corrupt session fixture")) {
    return EXIT_FAILURE;
  }
  const auto rejected = store.load();
  if (!check(!rejected.status &&
        rejected.status.error == PersistenceError::invalidData &&
        rejected.settings == defaultSessionSettings(),
        "Corrupt session settings were not rejected safely")) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
