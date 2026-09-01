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

bool runDesktop(
  const QString& executable,
  const std::filesystem::path& root,
  const std::optional<std::filesystem::path>& game,
  bool relativeGameArgument = false)
{
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
  environment.insert(QStringLiteral("SDL_AUDIODRIVER"), QStringLiteral("dummy"));
  environment.insert(
    QStringLiteral("GENPLUSGX_FORCE_SOFTWARE_VIDEO"), QStringLiteral("1"));
  environment.insert(QStringLiteral("GENPLUSGX_TEST_MODE"), QStringLiteral("1"));
  environment.insert(QStringLiteral("GENPLUSGX_TEST_DATA_ROOT"), pathText(root));
  environment.insert(
    QStringLiteral("GENPLUSGX_TEST_AUTO_QUIT_MS"), QStringLiteral("1200"));
  QProcess process;
  process.setProcessEnvironment(environment);
  process.setProgram(executable);
  if (game) {
    if (relativeGameArgument) {
      process.setWorkingDirectory(pathText(game->parent_path()));
      process.setArguments({pathText(game->filename())});
    } else {
      process.setArguments({pathText(*game)});
    }
  }
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
      }), "Could not enable automatic session resume")) {
    return 3;
  }

  genplusgx::test::TemporaryFixture first{
    genplusgx::test::makeGenesisRamMarkerRom(), ".md"};
  auto secondBytes = genplusgx::test::makeGenesisRamMarkerRom();
  secondBytes.back() ^= 0x01U;
  genplusgx::test::TemporaryFixture second{std::move(secondBytes), ".md"};
  const QString executable = QString::fromLocal8Bit(argv[1]);

  if (!check(runDesktop(executable, root, first.path(), true),
        "Initial session did not shut down cleanly")) {
    return 4;
  }
  auto settings = settingsStore.load();
  if (!check(settings.status && settings.settings.resumeOnLaunch &&
        settings.settings.lastGamePath == first.path(),
      "Clean shutdown did not record the resumable game")) {
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
  if (!check(settings.status && settings.settings.lastGamePath == second.path(),
      "Explicit command-line game did not take precedence over automatic resume")) {
    return 11;
  }

  const auto secondIdentity = genplusgx::identifyGame(second.path());
  const auto secondResumePath = secondIdentity.status
    ? manager.resumeStatePath(secondIdentity.identity)
    : std::filesystem::path{};
  auto corrupt = genplusgx::readFileBounded(
    secondResumePath, genplusgx::SaveStateManager::maximumPayloadBytes + 128U);
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
  return 0;
}
