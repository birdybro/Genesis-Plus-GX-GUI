#include "genplusgx/persistence.h"
#include "genplusgx/settings/session_settings.h"
#include "genplusgx/state_manager.h"

#include "synthetic_rom.h"

#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const std::string& message)
{
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

QString pathText(const std::filesystem::path& path)
{
#if defined(_WIN32)
  return QString::fromStdWString(path.native());
#else
  return QString::fromUtf8(path.native());
#endif
}

bool isAbsolutePathTo(
  const std::optional<std::filesystem::path>& actual,
  const std::filesystem::path& expected)
{
  if (!actual || !actual->is_absolute()) {
    return false;
  }
  std::error_code error;
  const bool equivalent = std::filesystem::equivalent(*actual, expected, error);
  return equivalent && !error;
}

bool runDesktop(
  const QString& executable,
  const std::filesystem::path& root,
  const std::optional<std::filesystem::path>& game,
  bool relativeGameArgument = false,
  const std::optional<std::filesystem::path>& patch = std::nullopt)
{
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
  environment.insert(QStringLiteral("SDL_AUDIODRIVER"), QStringLiteral("dummy"));
  environment.insert(
    QStringLiteral("GENPLUSGX_FORCE_SOFTWARE_VIDEO"), QStringLiteral("1"));
  environment.insert(QStringLiteral("GENPLUSGX_TEST_MODE"), QStringLiteral("1"));
  environment.insert(QStringLiteral("GENPLUSGX_TEST_DATA_ROOT"), pathText(root));
  environment.insert(
    QStringLiteral("GENPLUSGX_TEST_QUIT_WHEN_GAME_READY"), QStringLiteral("1"));
  environment.insert(
    QStringLiteral("GENPLUSGX_TEST_AUTO_QUIT_MS"), QStringLiteral("1200"));
  QProcess process;
  process.setProcessEnvironment(environment);
  process.setProgram(executable);
  QStringList arguments;
  if (patch) {
    arguments.append(QStringLiteral("--patch"));
    arguments.append(pathText(*patch));
  }
  if (game) {
    if (relativeGameArgument) {
      process.setWorkingDirectory(pathText(game->parent_path()));
      arguments.append(pathText(game->filename()));
    } else {
      arguments.append(pathText(*game));
    }
  }
  process.setArguments(arguments);
  process.start();
  if (!process.waitForStarted(5'000) || !process.waitForFinished(20'000)) {
    process.kill();
    process.waitForFinished(2'000);
    std::cerr << process.readAllStandardError().constData();
    return false;
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    std::cerr << "stdout:\n" << process.readAllStandardOutput().constData()
              << "\nstderr:\n" << process.readAllStandardError().constData();
    return false;
  }
  return true;
}

} // namespace

int main(int argc, char** argv)
{
  QCoreApplication application{argc, argv};
  if (!check(argc == 2, "Desktop executable path argument is required")) {
    return 1;
  }
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "Could not create isolated application root")) {
    return 2;
  }
  const auto root = std::filesystem::path{temporary.path().toStdString()} / "app";
  const genplusgx::ApplicationPaths paths{root};
  genplusgx::settings::SessionSettingsStore settingsStore{
    paths.configDirectory() / "session-settings.json"};
  if (!check(settingsStore.save({
        .resumeOnLaunch = true,
        .lastGamePath = std::nullopt,
        .lastPatchPath = std::nullopt,
      }), "Could not enable automatic session resume")) {
    return 3;
  }

  genplusgx::test::TemporaryFixture first{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  auto firstLaunchPath = first.path();
#if !defined(_WIN32)
  const auto aliasedFixtureDirectory =
    std::filesystem::path{temporary.path().toStdString()} / "rom-directory-link";
  std::error_code symlinkError;
  std::filesystem::create_directory_symlink(
    first.path().parent_path(), aliasedFixtureDirectory, symlinkError);
  if (!check(!symlinkError, "Could not create the path-alias fixture")) {
    return 4;
  }
  firstLaunchPath = aliasedFixtureDirectory / first.path().filename();
#endif
  auto secondBytes = genplusgx::test::makeGenesisRamMarkerRom();
  secondBytes.back() ^= 0x01U;
  genplusgx::test::TemporaryFixture second{std::move(secondBytes), ".md"};
  const QString executable = QString::fromLocal8Bit(argv[1]);

  if (!check(runDesktop(executable, root, firstLaunchPath, true),
        "Initial session did not shut down cleanly")) {
    return 4;
  }
  auto settings = settingsStore.load();
  if (!check(settings.status && settings.settings.resumeOnLaunch &&
        isAbsolutePathTo(settings.settings.lastGamePath, firstLaunchPath),
      "Clean shutdown did not record the absolute resumable game identity")) {
    if (settings.settings.lastGamePath) {
      std::cerr << "Recorded path: "
                << settings.settings.lastGamePath->generic_string() << '\n'
                << "Expected file: " << firstLaunchPath.generic_string() << '\n';
    }
    return 5;
  }
  const auto identity = genplusgx::identifyGame(first.path());
  genplusgx::SaveStateManager manager{paths};
  const auto firstCheckpoint = identity.status
    ? manager.loadResumeState(identity.identity, 0x80U)
    : genplusgx::SaveStateLoadResult{};
  if (!check(identity.status && firstCheckpoint.status &&
        !firstCheckpoint.rawPayload.empty(),
      "Clean shutdown did not write a validated resume checkpoint")) {
    return 6;
  }

  if (!check(runDesktop(executable, root, std::nullopt),
        "Automatic session restore launch did not shut down cleanly")) {
    return 7;
  }
  const auto resumedCheckpoint = manager.loadResumeState(identity.identity, 0x80U);
  if (!check(resumedCheckpoint.status &&
        resumedCheckpoint.metadata.timestamp > firstCheckpoint.metadata.timestamp,
      "Automatic restore launch did not replace the clean-session checkpoint")) {
    return 8;
  }
  QFile log{pathText(paths.logsDirectory() / "frontend.jsonl")};
  if (!check(log.open(QIODevice::ReadOnly), "Could not open frontend session log") ||
      !check(log.readAll().contains("Automatic session checkpoint restored."),
        "Frontend log did not confirm automatic checkpoint restoration")) {
    return 9;
  }

  settings.settings.lastGamePath = first.path();
  if (!check(settingsStore.save(settings.settings),
        "Could not restore first-game marker for precedence test") ||
      !check(runDesktop(executable, root, second.path()),
        "Explicit command-line game launch did not shut down cleanly")) {
    return 10;
  }
  settings = settingsStore.load();
  if (!check(settings.status &&
        isAbsolutePathTo(settings.settings.lastGamePath, second.path()),
      "Explicit command-line game did not take precedence over automatic resume")) {
    return 11;
  }

  const auto secondIdentity = genplusgx::identifyGame(second.path());
  const auto secondResumePath = secondIdentity.status
    ? manager.resumeStatePath(secondIdentity.identity)
    : std::filesystem::path{};
  auto corrupt = genplusgx::readFileBounded(
    secondResumePath, genplusgx::SaveStateManager::maximumFileBytes);
  if (!check(secondIdentity.status && corrupt.status && corrupt.exists &&
        !corrupt.data.empty(),
        "Explicit game did not produce its own checkpoint")) {
    return 12;
  }
  corrupt.data.back() ^= 0x01U;
  if (!check(genplusgx::writeFileAtomically(
        secondResumePath, corrupt.data, corrupt.data.size()),
      "Could not create corrupt automatic-resume checkpoint")) {
    return 13;
  }
  log.close();
  if (!check(log.open(QIODevice::WriteOnly | QIODevice::Truncate),
        "Could not reset the frontend log for fallback validation")) {
    return 14;
  }
  log.close();
  if (!check(runDesktop(executable, root, std::nullopt),
        "Corrupt checkpoint did not fall back to normal emulation")) {
    return 15;
  }
  const auto replacementCheckpoint = manager.loadResumeState(
    secondIdentity.identity, 0x80U);
  if (!check(replacementCheckpoint.status,
        "Normal fallback did not replace the corrupt checkpoint safely") ||
      !check(log.open(QIODevice::ReadOnly),
        "Could not open fallback frontend log") ||
      !check(log.readAll().contains("Automatic session resume failed safely:"),
        "Frontend log did not explain the corruption fallback")) {
    return 16;
  }

  const auto archivePath =
    std::filesystem::path{temporary.path().toStdString()} / "single-game.zip";
  const bool archiveWritten = genplusgx::test::writeZipFixture(archivePath, {
        {.name = "Archived Fixture.md",
          .data = genplusgx::test::makeGenesisRamMarkerRom()},
      });
  if (!check(archiveWritten, "Could not create the command-line ZIP fixture") ||
      !check(runDesktop(executable, root, archivePath),
        "Single-entry ZIP command-line launch did not shut down cleanly")) {
    return 17;
  }
  settings = settingsStore.load();
  if (!check(settings.status &&
        isAbsolutePathTo(settings.settings.lastGamePath, archivePath),
      "ZIP launch did not retain the archive source for session resume")) {
    return 18;
  }
  std::error_code cacheError;
  const auto archiveCache = paths.cacheDirectory() / "archives";
  if (!check(std::filesystem::is_directory(archiveCache, cacheError) &&
        !cacheError && !std::filesystem::is_empty(archiveCache, cacheError),
      "ZIP launch did not create a bounded per-user extraction cache")) {
    return 19;
  }

  const auto patchPath =
    std::filesystem::path{temporary.path().toStdString()} / "marker.ips";
  const std::vector<std::uint8_t> patch{
    'P', 'A', 'T', 'C', 'H',
    0x00, 0x02, 0x0a, 0x00, 0x02, 0x24, 0x68,
    'E', 'O', 'F',
  };
  {
    std::ofstream output(patchPath, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(patch.data()),
      static_cast<std::streamsize>(patch.size()));
    if (!check(static_cast<bool>(output),
          "Could not create the command-line soft-patch fixture")) {
      return 20;
    }
  }
  if (!check(runDesktop(executable, root, first.path(), false, patchPath),
        "Command-line soft-patch launch did not shut down cleanly")) {
    return 21;
  }
  settings = settingsStore.load();
  if (!check(settings.status &&
        isAbsolutePathTo(settings.settings.lastGamePath, first.path()) &&
        isAbsolutePathTo(settings.settings.lastPatchPath, patchPath),
      "Clean shutdown did not preserve the game and patch resume pair")) {
    return 22;
  }
  const auto patchCache = paths.cacheDirectory() / "patches";
  std::filesystem::path patchedRuntime;
  for (std::filesystem::directory_iterator iterator{
         patchCache, cacheError}, end;
       !cacheError && iterator != end;
       iterator.increment(cacheError)) {
    if (iterator->is_regular_file(cacheError) && !cacheError) {
      patchedRuntime = iterator->path();
      break;
    }
  }
  const auto patchedIdentity = genplusgx::identifyGame(patchedRuntime);
  const auto patchedCheckpoint = patchedIdentity.status
    ? manager.loadResumeState(patchedIdentity.identity, 0x80U)
    : genplusgx::SaveStateLoadResult{};
  if (!check(!patchedRuntime.empty() && patchedIdentity.status &&
        patchedIdentity.identity.sha256 != identity.identity.sha256 &&
        patchedCheckpoint.status,
      "Patched runtime content did not receive its own resumable identity")) {
    return 23;
  }
  if (!check(runDesktop(executable, root, std::nullopt),
        "Automatic patched-session restore did not shut down cleanly")) {
    return 24;
  }
  settings = settingsStore.load();
  if (!check(settings.status &&
        isAbsolutePathTo(settings.settings.lastGamePath, first.path()) &&
        isAbsolutePathTo(settings.settings.lastPatchPath, patchPath),
      "Automatic resume silently discarded the selected soft patch")) {
    return 25;
  }
  return 0;
}
