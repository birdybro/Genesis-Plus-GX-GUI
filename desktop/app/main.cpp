#include "genplusgx/ui/main_window.h"
#include "genplusgx/app/command_line.h"
#include "genplusgx/app/shutdown_report.h"
#include "genplusgx/version.h"
#include "genplusgx/audio_output.h"
#include "genplusgx/backup_store.h"
#include "genplusgx/bounded_queue.h"
#include "genplusgx/cheats/cheat_manager.h"
#include "genplusgx/diagnostics/diagnostics.h"
#include "genplusgx/emulation_worker.h"
#include "genplusgx/input/controller_input.h"
#include "genplusgx/input/input_aggregator.h"
#include "genplusgx/input/input_profile.h"
#include "genplusgx/input/keyboard_input.h"
#include "genplusgx/library/game_library_database.h"
#include "genplusgx/library/game_library_scanner.h"
#include "genplusgx/library/game_metadata_service.h"
#include "genplusgx/persistence.h"
#include "genplusgx/platform/bios_manager.h"
#include "genplusgx/recent_games.h"
#include "genplusgx/screenshots/screenshot_service.h"
#include "genplusgx/state_storage_service.h"
#include "genplusgx/timing/frame_rate_sampler.h"
#include "genplusgx/settings/appearance_settings.h"
#include "genplusgx/settings/screenshot_settings.h"
#include "genplusgx/settings/video_settings.h"
#include "genplusgx/settings/audio_settings.h"
#include "genplusgx/settings/per_game_settings.h"
#include "genplusgx/settings/system_settings.h"
#include "genplusgx/ui/dialog_service.h"
#include "genplusgx/ui/theme_controller.h"
#include "genplusgx/video/display_widget.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QMessageBox>
#include <QTextStream>
#include <QTimer>

#include <chrono>
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace {

std::array<genplusgx::ui::StateSlotView, 10> stateSlotViews(
  const genplusgx::StateSlotSummaries& summaries)
{
  std::array<genplusgx::ui::StateSlotView, 10> views{};
  for (std::size_t index = 0U; index < summaries.size(); ++index) {
    const auto& summary = summaries[index];
    auto& view = views[index];
    view.slot = summary.slot;
    view.timestamp = summary.metadata.timestamp;
    view.emulatedFrameNumber = summary.metadata.emulatedFrameNumber;
    view.detail = summary.message;
    switch (summary.availability) {
      case genplusgx::StateSlotAvailability::empty:
        view.state = genplusgx::ui::StateSlotViewState::empty;
        break;
      case genplusgx::StateSlotAvailability::available:
        view.state = genplusgx::ui::StateSlotViewState::available;
        break;
      case genplusgx::StateSlotAvailability::invalid:
        view.state = genplusgx::ui::StateSlotViewState::invalid;
        break;
    }
  }
  return views;
}

genplusgx::CoreFirmwareSettings coreFirmwareSettings(
  const genplusgx::platform::BiosSnapshot& snapshot)
{
  const auto validPath = [&snapshot](genplusgx::platform::BiosSlot slot) {
    const auto index = static_cast<std::size_t>(slot);
    return snapshot.validation[index].valid()
      ? snapshot.configuration.path(slot) : std::filesystem::path{};
  };
  return {
    .genesis = validPath(genplusgx::platform::BiosSlot::genesis),
    .masterSystemUsa = validPath(genplusgx::platform::BiosSlot::masterSystemUsa),
    .masterSystemEurope = validPath(
      genplusgx::platform::BiosSlot::masterSystemEurope),
    .masterSystemJapan = validPath(
      genplusgx::platform::BiosSlot::masterSystemJapan),
    .gameGear = validPath(genplusgx::platform::BiosSlot::gameGear),
    .segaCdUsa = validPath(genplusgx::platform::BiosSlot::segaCdUsa),
    .segaCdEurope = validPath(genplusgx::platform::BiosSlot::segaCdEurope),
    .segaCdJapan = validPath(genplusgx::platform::BiosSlot::segaCdJapan),
  };
}

genplusgx::CoreFirmwareSettings coreFirmwareSettings(
  const genplusgx::platform::BiosConfiguration& configuration)
{
  const auto validPath = [&configuration](genplusgx::platform::BiosSlot slot) {
    const auto& path = configuration.path(slot);
    return genplusgx::platform::validateBios(slot, path).valid()
      ? path
      : std::filesystem::path{};
  };
  return {
    .genesis = validPath(genplusgx::platform::BiosSlot::genesis),
    .masterSystemUsa = validPath(genplusgx::platform::BiosSlot::masterSystemUsa),
    .masterSystemEurope = validPath(
      genplusgx::platform::BiosSlot::masterSystemEurope),
    .masterSystemJapan = validPath(
      genplusgx::platform::BiosSlot::masterSystemJapan),
    .gameGear = validPath(genplusgx::platform::BiosSlot::gameGear),
    .segaCdUsa = validPath(genplusgx::platform::BiosSlot::segaCdUsa),
    .segaCdEurope = validPath(genplusgx::platform::BiosSlot::segaCdEurope),
    .segaCdJapan = validPath(genplusgx::platform::BiosSlot::segaCdJapan),
  };
}

std::optional<genplusgx::GameIdentity> metadataIdentity(
  const genplusgx::library::GameMetadata& metadata)
{
  auto title = genplusgx::sanitizeFilename(metadata.displayTitle());
  if (title.empty()) {
    title = "game";
  }
  genplusgx::GameIdentity identity{
    .sha256 = metadata.sha256,
    .titleSlug = std::move(title),
  };
  return identity.valid() ? std::optional{std::move(identity)} : std::nullopt;
}

std::string discRegionName(genplusgx::CoreDiscRegion region)
{
  switch (region) {
    case genplusgx::CoreDiscRegion::usa:
      return "USA";
    case genplusgx::CoreDiscRegion::europe:
      return "Europe";
    case genplusgx::CoreDiscRegion::japan:
      return "Japan";
    case genplusgx::CoreDiscRegion::unknown:
      return "Unknown";
  }
  return "Unknown";
}

genplusgx::cheats::CheatSystem cheatSystem(std::uint32_t hardware)
{
  constexpr std::uint32_t pbcMask = 0x81U;
  constexpr std::uint32_t genesisHardware = 0x80U;
  return (hardware & pbcMask) == genesisHardware
    ? genplusgx::cheats::CheatSystem::genesis
    : genplusgx::cheats::CheatSystem::masterSystem;
}

genplusgx::PersistenceStatus workerPersistenceFailure(std::string message)
{
  return {
    .error = genplusgx::PersistenceError::invalidData,
    .message = std::move(message),
  };
}

std::string biosValidationStateName(
  genplusgx::platform::BiosValidationState state)
{
  using State = genplusgx::platform::BiosValidationState;
  switch (state) {
    case State::notConfigured: return "Not configured";
    case State::missing: return "Missing";
    case State::notRegularFile: return "Not a regular file";
    case State::unreadable: return "Unreadable";
    case State::pathTooLong: return "Path too long";
    case State::invalidSize: return "Invalid size";
    case State::invalidContent: return "Invalid content";
    case State::valid: return "Valid";
  }
  return "Unknown";
}

} // namespace

int main(int argc, char* argv[])
{
  genplusgx::ui::configureHighDpiPolicy();
  QApplication application(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("Genesis Plus GX GUI"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("genesisplusgx.org"));
  QCoreApplication::setApplicationName(QString::fromLatin1(GENPLUSGX_APP_NAME));
  QCoreApplication::setApplicationVersion(QString::fromLatin1(GENPLUSGX_VERSION));
  QApplication::setDesktopFileName(QString::fromLatin1(GENPLUSGX_APP_ID));
  genplusgx::ui::ThemeController themeController{application};

  const auto commandLine = genplusgx::app::parseCommandLine(
    application.arguments().mid(1));
  if (!commandLine.valid) {
    QTextStream{stderr} << commandLine.error << '\n'
                        << "Use --help to list supported arguments.\n";
    return 2;
  }
  if (commandLine.showHelp) {
    QTextStream{stdout} << genplusgx::app::commandLineHelp() << '\n';
    return 0;
  }
  if (commandLine.showVersion) {
    QTextStream{stdout} << GENPLUSGX_APP_NAME << ' ' << GENPLUSGX_VERSION << '\n';
    return 0;
  }

  const bool automatedTestMode =
    qEnvironmentVariable("GENPLUSGX_TEST_MODE") == QStringLiteral("1");
  std::optional<int> automatedQuitMilliseconds;
  genplusgx::ApplicationPaths applicationPaths;
  if (automatedTestMode) {
    const auto testRoot = qEnvironmentVariable("GENPLUSGX_TEST_DATA_ROOT");
    bool quitDelayValid = false;
    const int quitDelay = qEnvironmentVariableIntValue(
      "GENPLUSGX_TEST_AUTO_QUIT_MS", &quitDelayValid);
    const auto root = genplusgx::ui::pathFromQString(testRoot);
    if (testRoot.isEmpty() || !root.is_absolute() || !quitDelayValid ||
        quitDelay < 1 || quitDelay > 10'000) {
      QTextStream{stderr}
        << "Invalid automated startup-test environment. The data root must be "
           "absolute and the quit delay must be between 1 and 10000 ms.\n";
      return 2;
    }
    applicationPaths = genplusgx::ApplicationPaths{root};
    automatedQuitMilliseconds = quitDelay;
  } else {
    applicationPaths = genplusgx::ApplicationPaths::fromPlatform();
  }
  std::vector<std::string> startupIssues;
  const auto recordStartupIssue = [&startupIssues](
    std::string area, const std::string& detail) {
    if (!detail.empty()) {
      area += ": " + detail;
    }
    startupIssues.push_back(std::move(area));
  };
  const auto pathsInitialized = applicationPaths.initialize();
  if (!pathsInitialized) {
    qWarning().noquote() << QString::fromStdString(pathsInitialized.message);
    if (automatedTestMode) {
      return 2;
    }
    recordStartupIssue("Application data", pathsInitialized.message);
  }
  genplusgx::diagnostics::FrontendLogger frontendLogger;
  const auto loggerInitialized = frontendLogger.initialize(
    applicationPaths.logsDirectory() / "frontend.jsonl");
  if (!loggerInitialized || !frontendLogger.install()) {
    const auto detail = loggerInitialized
      ? std::string{"The frontend logger could not install its Qt message handler."}
      : loggerInitialized.message;
    qWarning().noquote() << QString::fromStdString(detail);
    recordStartupIssue("Diagnostics logging", detail);
  }
  qInfo().noquote() << "Application startup:" << GENPLUSGX_APP_NAME
                    << GENPLUSGX_VERSION << '(' << GENPLUSGX_GIT_COMMIT << ')';
  genplusgx::settings::AppearanceSettingsStore appearanceSettingsStore{
    applicationPaths.configDirectory() / "appearance-settings.json"};
  auto loadedAppearanceSettings = appearanceSettingsStore.load();
  if (!loadedAppearanceSettings.status) {
    qWarning().noquote() << QString::fromStdString(
      loadedAppearanceSettings.status.message);
    recordStartupIssue(
      "Appearance settings", loadedAppearanceSettings.status.message);
  }
  auto appearanceSettings = loadedAppearanceSettings.settings;
  if (!themeController.apply(appearanceSettings)) {
    appearanceSettings = genplusgx::settings::defaultAppearanceSettings();
    static_cast<void>(themeController.apply(appearanceSettings));
  }
  if (loadedAppearanceSettings.migrated) {
    const auto migrated = appearanceSettingsStore.save(appearanceSettings);
    if (!migrated) {
      qWarning().noquote() << QString::fromStdString(migrated.message);
      recordStartupIssue("Appearance settings migration", migrated.message);
    }
  }
  const auto gameLibraryPath =
    applicationPaths.libraryDirectory() / "game-library.sqlite3";
  genplusgx::cheats::CheatStore cheatStore{
    applicationPaths.configDirectory() / "cheats"};
  genplusgx::settings::PerGameSettingsStore perGameSettingsStore{
    applicationPaths.configDirectory() / "per-game-settings"};
  genplusgx::library::GameLibraryDatabase gameLibrary{gameLibraryPath};
  const auto gameLibraryInitialized = gameLibrary.initialize();
  if (!gameLibraryInitialized) {
    qWarning().noquote() << QString::fromStdString(gameLibraryInitialized.message);
    recordStartupIssue("Game library", gameLibraryInitialized.message);
  } else if (gameLibrary.recoveredCorruption()) {
    qWarning().noquote()
      << "A corrupt game-library database was preserved as"
      << genplusgx::ui::pathToQString(gameLibrary.recoveryBackupPath());
    recordStartupIssue(
      "Game library",
      "A corrupt database was preserved and a new library was created.");
  }
  genplusgx::library::GameLibraryScanner gameLibraryScanner{gameLibraryPath};
  const auto gameLibraryScannerStarted = gameLibraryInitialized
    ? gameLibraryScanner.start()
    : genplusgx::library::GameLibraryScannerStatus{
        .error = genplusgx::library::GameLibraryScannerError::databaseFailure,
        .message = gameLibraryInitialized.message,
      };
  if (!gameLibraryScannerStarted) {
    qWarning().noquote() << QString::fromStdString(
      gameLibraryScannerStarted.message);
    recordStartupIssue(
      "Game-library scanner", gameLibraryScannerStarted.message);
  }
  genplusgx::input::InputProfileStore inputProfileStore{
    applicationPaths.configDirectory() / "input-profiles.json"};
  auto loadedInputProfiles = inputProfileStore.load();
  if (!loadedInputProfiles.status) {
    qWarning().noquote() << QString::fromStdString(loadedInputProfiles.status.message);
    recordStartupIssue(
      "Input profiles", loadedInputProfiles.status.message);
  }
  auto inputConfiguration = std::move(loadedInputProfiles.configuration);
  if (loadedInputProfiles.migrated) {
    const auto migrated = inputProfileStore.save(inputConfiguration);
    if (!migrated) {
      qWarning().noquote() << QString::fromStdString(migrated.message);
      recordStartupIssue("Input profile migration", migrated.message);
    }
  }
  genplusgx::RecentGamesStore recentGamesStore{
    applicationPaths.configDirectory() / "recent-games.json"};
  auto loadedRecentGames = recentGamesStore.load();
  if (!loadedRecentGames.status) {
    qWarning().noquote() << QString::fromStdString(loadedRecentGames.status.message);
    recordStartupIssue("Recent games", loadedRecentGames.status.message);
  }
  auto recentGames = std::move(loadedRecentGames.model);
  if (loadedRecentGames.migrated) {
    const auto migrated = recentGamesStore.save(recentGames);
    if (!migrated) {
      qWarning().noquote() << QString::fromStdString(migrated.message);
      recordStartupIssue("Recent-games migration", migrated.message);
    }
  }
  genplusgx::settings::VideoSettingsStore videoSettingsStore{
    applicationPaths.configDirectory() / "video-settings.json"};
  auto loadedVideoSettings = videoSettingsStore.load();
  if (!loadedVideoSettings.status) {
    qWarning().noquote() << QString::fromStdString(
      loadedVideoSettings.status.message);
    recordStartupIssue("Video settings", loadedVideoSettings.status.message);
  }
  auto videoSettings = loadedVideoSettings.settings;
  if (loadedVideoSettings.migrated) {
    const auto migrated = videoSettingsStore.save(videoSettings);
    if (!migrated) {
      qWarning().noquote() << QString::fromStdString(migrated.message);
      recordStartupIssue("Video settings migration", migrated.message);
    }
  }

  genplusgx::settings::AudioSettingsStore audioSettingsStore{
    applicationPaths.configDirectory() / "audio-settings.json"};
  auto loadedAudioSettings = audioSettingsStore.load();
  if (!loadedAudioSettings.status) {
    qWarning().noquote() << QString::fromStdString(
      loadedAudioSettings.status.message);
    recordStartupIssue("Audio settings", loadedAudioSettings.status.message);
  }
  auto audioSettings = loadedAudioSettings.settings;
  if (loadedAudioSettings.migrated) {
    const auto migrated = audioSettingsStore.save(audioSettings);
    if (!migrated) {
      qWarning().noquote() << QString::fromStdString(migrated.message);
      recordStartupIssue("Audio settings migration", migrated.message);
    }
  }
  genplusgx::settings::SystemSettingsStore systemSettingsStore{
    applicationPaths.configDirectory() / "system-settings.json"};
  auto loadedSystemSettings = systemSettingsStore.load();
  if (!loadedSystemSettings.status) {
    qWarning().noquote() << QString::fromStdString(
      loadedSystemSettings.status.message);
    recordStartupIssue("System settings", loadedSystemSettings.status.message);
  }
  auto systemSettings = loadedSystemSettings.settings;
  if (loadedSystemSettings.migrated) {
    const auto migrated = systemSettingsStore.save(systemSettings);
    if (!migrated) {
      qWarning().noquote() << QString::fromStdString(migrated.message);
      recordStartupIssue("System settings migration", migrated.message);
    }
  }
  genplusgx::settings::ScreenshotSettingsStore screenshotSettingsStore{
    applicationPaths.configDirectory() / "screenshot-settings.json",
    applicationPaths.screenshotsDirectory()};
  auto loadedScreenshotSettings = screenshotSettingsStore.load();
  if (!loadedScreenshotSettings.status) {
    qWarning().noquote() << QString::fromStdString(
      loadedScreenshotSettings.status.message);
    recordStartupIssue(
      "Screenshot settings", loadedScreenshotSettings.status.message);
  }
  auto screenshotSettings = loadedScreenshotSettings.settings;
  if (loadedScreenshotSettings.migrated) {
    const auto migrated = screenshotSettingsStore.save(screenshotSettings);
    if (!migrated) {
      qWarning().noquote() << QString::fromStdString(migrated.message);
      recordStartupIssue("Screenshot settings migration", migrated.message);
    }
  }
  genplusgx::platform::BiosManager biosManager{
    genplusgx::platform::BiosConfigurationStore{
      applicationPaths.configDirectory() / "bios.json"}};
  const auto biosLoaded = biosManager.load();
  if (!biosLoaded) {
    qWarning().noquote() << QString::fromStdString(biosLoaded.message);
    recordStartupIssue("BIOS configuration", biosLoaded.message);
  }
  for (std::size_t index = 0U;
       index < genplusgx::platform::biosSlotCount;
       ++index) {
    const auto slot = static_cast<genplusgx::platform::BiosSlot>(index);
    const auto& descriptor = genplusgx::platform::biosDescriptor(slot);
    const auto& validation = biosManager.snapshot().validation[index];
    qInfo().noquote() << "BIOS status:"
                      << QString::fromUtf8(descriptor.displayName.data(),
                           static_cast<qsizetype>(descriptor.displayName.size()))
                      << '-'
                      << QString::fromStdString(
                           biosValidationStateName(validation.state));
  }

  const auto audioDevices = genplusgx::availableAudioOutputDevices();
  genplusgx::AudioOutputConfig audioOutputConfig{
    .sampleRate = 48'000,
    .latency = std::chrono::milliseconds{audioSettings.latencyMilliseconds},
    .volumePercent = audioSettings.masterVolumePercent,
    .muted = audioSettings.muted,
  };
  if (!audioSettings.outputDeviceName.empty()) {
    const auto found = std::ranges::find_if(audioDevices,
      [&audioSettings](const auto& device) {
        return device.name == audioSettings.outputDeviceName;
      });
    if (found != audioDevices.end()) {
      audioOutputConfig.deviceId = found->id;
    } else {
      qWarning().noquote() << "Configured audio device is unavailable; using default:"
        << QString::fromStdString(audioSettings.outputDeviceName);
      recordStartupIssue(
        "Audio output",
        "The configured device is unavailable; the system default is in use.");
    }
  }

  genplusgx::AudioOutput audioOutput{audioOutputConfig};
  const auto audioInitialized = audioOutput.initialize();
  if (!audioInitialized) {
    qWarning().noquote() << QString::fromStdString(audioInitialized.message);
    recordStartupIssue("Audio output", audioInitialized.message);
  } else {
    qInfo().noquote() << "Audio output:"
                      << QString::fromStdString(audioOutput.deviceName());
  }

  auto videoFrames = std::make_shared<genplusgx::VideoFrameExchange>();
  auto backupStore = std::make_shared<genplusgx::PerGameBackupStore>(
    genplusgx::PersistenceStore{applicationPaths});
  genplusgx::StateStorageService stateStorage{applicationPaths};
  const auto stateStorageStarted = stateStorage.start();
  if (!stateStorageStarted) {
    qWarning().noquote() << QString::fromStdString(stateStorageStarted.message);
    recordStartupIssue("Save-state service", stateStorageStarted.message);
  }
  genplusgx::library::GameMetadataService metadataService;
  const auto metadataServiceStarted = metadataService.start();
  if (!metadataServiceStarted) {
    qWarning().noquote() << QString::fromStdString(metadataServiceStarted.message);
    recordStartupIssue("Game metadata service", metadataServiceStarted.message);
  }
  genplusgx::screenshots::ScreenshotService screenshotService;
  const auto screenshotServiceStarted = screenshotService.start();
  if (!screenshotServiceStarted) {
    qWarning().noquote() << QString::fromStdString(
      screenshotServiceStarted.message);
    recordStartupIssue("Screenshot service", screenshotServiceStarted.message);
  }
  genplusgx::EmulationWorker worker{
    64U,
    64U,
    audioOutput.config().sampleRate,
    videoFrames,
    audioOutput.ringBuffer(),
    backupStore};
  const auto workerStarted = worker.start();
  if (!workerStarted) {
    qCritical().noquote() << QString::fromStdString(workerStarted.message);
    static_cast<void>(metadataService.stop());
    static_cast<void>(stateStorage.stop());
    static_cast<void>(screenshotService.stop());
    static_cast<void>(gameLibraryScanner.stop());
    static_cast<void>(audioOutput.shutdown());
    QMessageBox::critical(
      nullptr,
      QObject::tr("Unable to Start Emulation"),
      QObject::tr(
        "The emulation service could not start, so the application must close.\n\n%1")
        .arg(QString::fromStdString(workerStarted.message)));
    return 1;
  }

  genplusgx::ui::MainWindow window;
  window.displayWidget()->setRendererFailureSink(
    [&window](std::string detail) {
      window.showStartupIssues({"Video renderer: " + std::move(detail)});
    });
  std::set<std::int64_t> libraryScansInFlight;
  std::uint64_t libraryScanOperationId = 5'000'000U;
  genplusgx::BoundedQueue<
    std::pair<std::filesystem::path, std::int64_t>> deferredLibraryLaunches{32U};
  const auto refreshGameLibrary = [&gameLibrary, &window] {
    const auto directories = gameLibrary.directories();
    if (!directories.status) {
      return directories.status;
    }
    const auto games = gameLibrary.games();
    if (!games.status) {
      return games.status;
    }
    window.setGameLibrarySnapshot(directories.directories, games.games);
    return genplusgx::library::GameLibraryStatus{};
  };
  const auto showLibraryStatus = [&window](
    const genplusgx::library::GameLibraryStatus& status) {
    if (!status) {
      window.showGameLibraryError(status.message);
    }
  };
  const auto requestLibraryScan =
    [&gameLibraryScanner, &libraryScanOperationId, &libraryScansInFlight,
     &window](std::int64_t directoryId, const std::filesystem::path& path) {
      if (libraryScansInFlight.contains(directoryId)) {
        window.showGameLibraryError(
          "That game-library directory is already queued for scanning.");
        return;
      }
      const auto submitted = gameLibraryScanner.requestScan(
        ++libraryScanOperationId, directoryId);
      if (!submitted) {
        window.showGameLibraryError(submitted.message);
        return;
      }
      libraryScansInFlight.insert(directoryId);
      window.showGameLibraryScanStarted(directoryId, path);
    };
  if (gameLibraryInitialized) {
    const auto refreshed = refreshGameLibrary();
    if (!refreshed) {
      qWarning().noquote() << QString::fromStdString(refreshed.message);
      window.setGameLibraryAvailable(false, refreshed.message);
      recordStartupIssue("Game library", refreshed.message);
    } else {
      window.setGameLibraryAvailable(true);
    }
  } else {
    window.setGameLibraryAvailable(false, gameLibraryInitialized.message);
  }
  window.setGameLibraryActions({
    .addDirectory =
      [&gameLibrary, &libraryScansInFlight, &refreshGameLibrary,
       &requestLibraryScan, &window](const std::filesystem::path& path,
                                     bool recursive) {
        if (!libraryScansInFlight.empty()) {
          window.showGameLibraryError(
            "Wait for the active library scan before changing directories.");
          return;
        }
        const auto added = gameLibrary.addDirectory(path, recursive);
        if (!added.status) {
          window.showGameLibraryError(added.status.message);
          return;
        }
        const auto refreshed = refreshGameLibrary();
        if (!refreshed) {
          window.showGameLibraryError(refreshed.message);
          return;
        }
        requestLibraryScan(added.directory.id, added.directory.path);
      },
    .removeDirectory =
      [&gameLibrary, &libraryScansInFlight, &refreshGameLibrary,
       &showLibraryStatus, &window](std::int64_t directoryId) {
        if (!libraryScansInFlight.empty()) {
          window.showGameLibraryError(
            "Wait for the active library scan before removing a directory.");
          return;
        }
        const auto removed = gameLibrary.removeDirectory(directoryId);
        if (!removed) {
          window.showGameLibraryError(removed.message);
          return;
        }
        showLibraryStatus(refreshGameLibrary());
      },
    .updateDirectory =
      [&gameLibrary, &libraryScansInFlight, &refreshGameLibrary,
       &showLibraryStatus, &window](std::int64_t directoryId, bool recursive) {
        if (!libraryScansInFlight.empty()) {
          window.showGameLibraryError(
            "Wait for the active library scan before changing a directory.");
          return;
        }
        const auto updated = gameLibrary.updateDirectory(directoryId, recursive);
        if (!updated) {
          window.showGameLibraryError(updated.message);
          return;
        }
        showLibraryStatus(refreshGameLibrary());
      },
    .scanDirectory =
      [&gameLibrary, &requestLibraryScan, &window](std::int64_t directoryId) {
        const auto directories = gameLibrary.directories();
        if (!directories.status) {
          window.showGameLibraryError(directories.status.message);
          return;
        }
        const auto found = std::ranges::find_if(
          directories.directories, [directoryId](const auto& directory) {
            return directory.id == directoryId;
          });
        if (found == directories.directories.end()) {
          window.showGameLibraryError(
            "The selected game-library directory no longer exists.");
          return;
        }
        requestLibraryScan(directoryId, found->path);
      },
    .setFavorite =
      [&gameLibrary, &libraryScansInFlight, &refreshGameLibrary,
       &showLibraryStatus, &window](std::int64_t gameId, bool favorite) {
        if (!libraryScansInFlight.empty()) {
          window.showGameLibraryError(
            "Wait for the active library scan before changing favorites.");
          return;
        }
        const auto updated = gameLibrary.setFavorite(gameId, favorite);
        if (!updated) {
          window.showGameLibraryError(updated.message);
          return;
        }
        showLibraryStatus(refreshGameLibrary());
      },
    .setArtwork =
      [&gameLibrary, &libraryScansInFlight, &refreshGameLibrary,
       &showLibraryStatus, &window](std::int64_t gameId,
                                    const std::filesystem::path& path) {
        if (!libraryScansInFlight.empty()) {
          window.showGameLibraryError(
            "Wait for the active library scan before changing artwork.");
          return;
        }
        const auto updated = gameLibrary.setArtworkPath(gameId, path);
        if (!updated) {
          window.showGameLibraryError(updated.message);
          return;
        }
        showLibraryStatus(refreshGameLibrary());
      },
    .launchGame = [&window](std::int64_t, const std::filesystem::path& path) {
      static_cast<void>(window.requestGameLoad(path));
    },
  });
  const auto recordLibraryLaunch =
    [&gameLibrary, &refreshGameLibrary](const std::filesystem::path& path,
                                        std::int64_t timestamp) {
      const auto games = gameLibrary.games();
      if (!games.status) {
        return games.status;
      }
      std::error_code error;
      const auto canonical = std::filesystem::weakly_canonical(path, error);
      const auto found = std::ranges::find_if(
        games.games, [&path, &canonical, &error](const auto& game) {
          if (game.metadata.path == path) {
            return true;
          }
          if (error) {
            return false;
          }
          std::error_code gameError;
          const auto gameCanonical = std::filesystem::weakly_canonical(
            game.metadata.path, gameError);
          return !gameError && gameCanonical == canonical;
        });
      if (found == games.games.end()) {
        return genplusgx::library::GameLibraryStatus{};
      }
      const auto recorded = gameLibrary.recordLaunch(found->id, timestamp);
      if (!recorded) {
        return recorded;
      }
      return refreshGameLibrary();
    };
  const auto flushDeferredLibraryLaunches =
    [&deferredLibraryLaunches, &recordLibraryLaunch, &window] {
      while (auto launch = deferredLibraryLaunches.pop()) {
        const auto recorded = recordLibraryLaunch(
          launch->first, launch->second);
        if (!recorded) {
          qWarning().noquote() << QString::fromStdString(recorded.message);
          window.showGameLibraryError(recorded.message);
        }
      }
    };
  window.displayWidget()->setFrameExchange(videoFrames);
  window.setApplicationPaths(applicationPaths);
  window.setAppearanceSettings(appearanceSettings);
  window.setAppearanceSettingsSink(
    [&appearanceSettings, &appearanceSettingsStore, &themeController](
      const genplusgx::settings::AppearanceSettings& settings) {
      const auto saved = appearanceSettingsStore.save(settings);
      if (!saved) {
        return saved;
      }
      if (!themeController.apply(settings)) {
        return genplusgx::PersistenceStatus{
          .error = genplusgx::PersistenceError::invalidData,
          .message = "The selected application theme could not be applied.",
        };
      }
      appearanceSettings = settings;
      return genplusgx::PersistenceStatus{};
    });
  window.setVideoSettings(videoSettings);
  window.setAudioSettings(audioSettings);
  window.setSystemSettings(systemSettings);
  window.setScreenshotSettings(
    screenshotSettings, applicationPaths.screenshotsDirectory());
  window.setScreenshotSettingsSink(
    [&screenshotSettings, &screenshotSettingsStore](
      const genplusgx::settings::ScreenshotSettings& settings) {
      const auto saved = screenshotSettingsStore.save(settings);
      if (saved) {
        screenshotSettings = settings;
      }
      return saved;
    });
  const auto globalGameSettings = [&audioSettings, &biosManager,
                                    &inputConfiguration, &systemSettings,
                                    &videoSettings] {
    return genplusgx::settings::GlobalGameSettings{
      .video = videoSettings,
      .audio = audioSettings,
      .system = systemSettings,
      .inputProfile = inputConfiguration.activeProfile,
      .bios = biosManager.snapshot().configuration,
    };
  };
  std::optional<genplusgx::GameIdentity> activePerGameIdentity;
  genplusgx::settings::PerGameSettings activePerGameSettings;
  auto activeEffectiveSettings = genplusgx::settings::resolvePerGameSettings(
    globalGameSettings(), activePerGameSettings);
  window.setBiosSnapshot(biosManager.snapshot());
  std::uint64_t firmwareSettingsOperationId = 800'000U;
  const auto initialFirmwareSettings = worker.submit(
    genplusgx::EmulationCommand::updateFirmwareSettings(
      ++firmwareSettingsOperationId,
      coreFirmwareSettings(biosManager.snapshot())));
  if (!initialFirmwareSettings) {
    qWarning().noquote() << QString::fromStdString(
      initialFirmwareSettings.message);
    recordStartupIssue(
      "BIOS runtime settings", initialFirmwareSettings.message);
  }
  window.setBiosConfigurationSink(
    [&activeEffectiveSettings, &activePerGameIdentity, &activePerGameSettings,
     &biosManager, &firmwareSettingsOperationId, &globalGameSettings, &window,
     &worker](
      const genplusgx::platform::BiosConfiguration& configuration) {
      const auto saved = biosManager.apply(configuration);
      if (!saved) {
        return saved;
      }
      window.setBiosSnapshot(biosManager.snapshot());
      const bool overridesActiveGame =
        activePerGameIdentity && activePerGameSettings.bios;
      if (!overridesActiveGame) {
        activeEffectiveSettings.bios = biosManager.snapshot().configuration;
      }
      const auto submitted = worker.submit(
        genplusgx::EmulationCommand::updateFirmwareSettings(
          ++firmwareSettingsOperationId,
          coreFirmwareSettings(activeEffectiveSettings.bios)));
      if (!submitted) {
        return genplusgx::PersistenceStatus{
          .error = genplusgx::PersistenceError::invalidData,
          .message = "BIOS paths were saved, but the emulation worker could not "
                     "accept the update: " + submitted.message,
        };
      }
      if (activePerGameIdentity) {
        window.setPerGameSettingsSession(
          activePerGameSettings, globalGameSettings());
      }
      return genplusgx::PersistenceStatus{};
    });
  std::vector<std::string> audioDeviceNames;
  audioDeviceNames.reserve(audioDevices.size());
  for (const auto& device : audioDevices) {
    audioDeviceNames.push_back(device.name);
  }
  window.setAvailableAudioDevices(std::move(audioDeviceNames));
  std::uint64_t videoSettingsOperationId = 500'000U;
  const auto initialVideoSettings = worker.submit(
    genplusgx::EmulationCommand::updateVideoSettings(
      ++videoSettingsOperationId, videoSettings.core));
  if (!initialVideoSettings) {
    qWarning().noquote() << QString::fromStdString(initialVideoSettings.message);
    recordStartupIssue("Video runtime settings", initialVideoSettings.message);
  }
  window.setVideoSettingsSink(
    [&activeEffectiveSettings, &activePerGameIdentity, &activePerGameSettings,
     &globalGameSettings, &perGameSettingsStore, &videoSettings,
     &videoSettingsOperationId,
     &videoSettingsStore, &window, &worker](
      const genplusgx::settings::VideoSettings& settings) {
      const auto previous = activeEffectiveSettings.video;
      const auto submitted = worker.submit(
        genplusgx::EmulationCommand::updateVideoSettings(
          ++videoSettingsOperationId, settings.core));
      if (!submitted) {
        qWarning().noquote() << QString::fromStdString(submitted.message);
        return genplusgx::PersistenceStatus{
          .error = genplusgx::PersistenceError::invalidData,
          .message = "Video settings could not reach the emulation worker: " +
            submitted.message,
        };
      }
      const bool overridesActiveGame =
        activePerGameIdentity && activePerGameSettings.video;
      auto saved = genplusgx::PersistenceStatus{};
      if (overridesActiveGame) {
        auto candidate = activePerGameSettings;
        candidate.video = settings;
        saved = perGameSettingsStore.save(*activePerGameIdentity, candidate);
        if (saved) {
          activePerGameSettings = std::move(candidate);
          activeEffectiveSettings.video = settings;
        }
      } else {
        saved = videoSettingsStore.save(settings);
        if (saved) {
          videoSettings = settings;
          activeEffectiveSettings.video = settings;
        }
      }
      if (!saved) {
        qWarning().noquote() << QString::fromStdString(saved.message);
        const auto rolledBack = worker.submit(
          genplusgx::EmulationCommand::updateVideoSettings(
            ++videoSettingsOperationId, previous.core));
        if (!rolledBack) {
          saved.message += " The runtime rollback also failed: " +
            rolledBack.message;
        }
      } else if (activePerGameIdentity) {
        window.setPerGameSettingsSession(
          activePerGameSettings, globalGameSettings());
      }
      return saved;
    });
  std::uint64_t audioSettingsOperationId = 600'000U;
  const auto initialAudioSettings = worker.submit(
    genplusgx::EmulationCommand::updateAudioSettings(
      ++audioSettingsOperationId, audioSettings.core));
  if (!initialAudioSettings) {
    qWarning().noquote() << QString::fromStdString(initialAudioSettings.message);
    recordStartupIssue("Audio runtime settings", initialAudioSettings.message);
  }
  window.setAudioSettingsSink(
    [&activeEffectiveSettings, &activePerGameIdentity, &activePerGameSettings,
     &audioOutput, &audioSettings, &audioSettingsOperationId,
     &audioSettingsStore, &globalGameSettings, &perGameSettingsStore, &window,
     &worker](
      const genplusgx::settings::AudioSettings& settings) {
      const auto previous = activeEffectiveSettings.audio;
      const auto previousOutputConfig = audioOutput.config();
      const auto restoreRuntime = [&](const std::string& detail) {
        static_cast<void>(worker.submit(
          genplusgx::EmulationCommand::updateAudioSettings(
            ++audioSettingsOperationId, previous.core)));
        if (audioOutput.config().latency != previousOutputConfig.latency ||
            audioOutput.config().deviceId != previousOutputConfig.deviceId) {
          const auto restored = audioOutput.reconfigure(previousOutputConfig);
          if (!restored) {
            qWarning().noquote() << "Audio rollback failed:"
                                 << QString::fromStdString(restored.message);
          }
        }
        static_cast<void>(audioOutput.setVolumePercent(
          previous.masterVolumePercent));
        audioOutput.setMuted(previous.muted);
        window.setAudioSettings(previous);
        window.showAudioSettingsError(detail);
      };
      const auto submitted = worker.submit(
        genplusgx::EmulationCommand::updateAudioSettings(
          ++audioSettingsOperationId, settings.core));
      if (!submitted) {
        qWarning().noquote() << QString::fromStdString(submitted.message);
        window.setAudioSettings(previous);
        window.showAudioSettingsError(submitted.message);
        return;
      }

      const bool hostPreferenceChanged =
        settings.latencyMilliseconds != previous.latencyMilliseconds ||
        settings.outputDeviceName != previous.outputDeviceName;
      if (hostPreferenceChanged) {
        auto requestedOutputConfig = previousOutputConfig;
        requestedOutputConfig.latency =
          std::chrono::milliseconds{settings.latencyMilliseconds};
        requestedOutputConfig.volumePercent = settings.masterVolumePercent;
        requestedOutputConfig.muted = settings.muted;
        if (settings.outputDeviceName != previous.outputDeviceName) {
          requestedOutputConfig.deviceId = 0U;
          if (!settings.outputDeviceName.empty()) {
            const auto devices = genplusgx::availableAudioOutputDevices();
            const auto selected = std::ranges::find_if(devices,
              [&settings](const auto& device) {
                return device.name == settings.outputDeviceName;
              });
            if (selected == devices.end()) {
              restoreRuntime("The selected audio output is no longer available.");
              return;
            }
            requestedOutputConfig.deviceId = selected->id;
          }
        }
        const auto reconfigured = audioOutput.reconfigure(requestedOutputConfig);
        if (!reconfigured) {
          qWarning().noquote() << QString::fromStdString(reconfigured.message);
          restoreRuntime(reconfigured.message);
          return;
        }
        qInfo().noquote() << "Audio output reconfigured live:"
                          << QString::fromStdString(audioOutput.deviceName())
                          << settings.latencyMilliseconds << "ms";
      } else {
        const auto volume = audioOutput.setVolumePercent(
          settings.masterVolumePercent);
        if (!volume) {
          qWarning().noquote() << QString::fromStdString(volume.message);
          restoreRuntime(volume.message);
          return;
        }
        audioOutput.setMuted(settings.muted);
      }

      const bool overridesActiveGame =
        activePerGameIdentity && activePerGameSettings.audio;
      auto saved = genplusgx::PersistenceStatus{};
      auto candidate = activePerGameSettings;
      const auto layerUpdate = genplusgx::settings::planAudioSettingsLayerUpdate(
        audioSettings,
        overridesActiveGame ? activePerGameSettings.audio : std::nullopt,
        settings);
      auto globalCandidate = layerUpdate.global;
      if (overridesActiveGame) {
        candidate.audio = layerUpdate.perGame;
        saved = perGameSettingsStore.save(*activePerGameIdentity, candidate);
        if (saved && globalCandidate != audioSettings) {
          saved = audioSettingsStore.save(globalCandidate);
          if (!saved) {
            const auto rolledBack = perGameSettingsStore.save(
              *activePerGameIdentity, activePerGameSettings);
            if (!rolledBack) {
              saved.message += " The per-game audio rollback also failed: " +
                rolledBack.message;
            }
          }
        }
      } else {
        globalCandidate = settings;
        saved = audioSettingsStore.save(globalCandidate);
      }
      if (!saved) {
        qWarning().noquote() << QString::fromStdString(saved.message);
        restoreRuntime(saved.message);
        return;
      }
      audioSettings = std::move(globalCandidate);
      if (overridesActiveGame) {
        activePerGameSettings = std::move(candidate);
      }
      activeEffectiveSettings = genplusgx::settings::resolvePerGameSettings(
        globalGameSettings(), activePerGameSettings);
      window.setAudioSettings(activeEffectiveSettings.audio);
      if (activePerGameIdentity) {
        window.setPerGameSettingsSession(
          activePerGameSettings, globalGameSettings());
      }
    });
  std::uint64_t systemSettingsOperationId = 700'000U;
  const auto initialSystemSettings = worker.submit(
    genplusgx::EmulationCommand::updateSystemSettings(
      ++systemSettingsOperationId, systemSettings));
  if (!initialSystemSettings) {
    qWarning().noquote() << QString::fromStdString(initialSystemSettings.message);
    recordStartupIssue("System runtime settings", initialSystemSettings.message);
  }
  window.setSystemSettingsSink(
    [&activeEffectiveSettings, &activePerGameIdentity, &activePerGameSettings,
     &globalGameSettings, &perGameSettingsStore, &systemSettings,
     &systemSettingsOperationId, &systemSettingsStore, &window, &worker](
      const genplusgx::CoreSystemSettings& settings) {
      const auto previous = activeEffectiveSettings.system;
      const auto submitted = worker.submit(
        genplusgx::EmulationCommand::updateSystemSettings(
          ++systemSettingsOperationId, settings));
      if (!submitted) {
        qWarning().noquote() << QString::fromStdString(submitted.message);
        return genplusgx::PersistenceStatus{
          .error = genplusgx::PersistenceError::invalidData,
          .message = "System settings could not reach the emulation worker: " +
            submitted.message,
        };
      }
      const bool overridesActiveGame =
        activePerGameIdentity && activePerGameSettings.system;
      auto saved = genplusgx::PersistenceStatus{};
      if (overridesActiveGame) {
        auto candidate = activePerGameSettings;
        candidate.system = settings;
        saved = perGameSettingsStore.save(*activePerGameIdentity, candidate);
        if (saved) {
          activePerGameSettings = std::move(candidate);
          activeEffectiveSettings.system = settings;
        }
      } else {
        saved = systemSettingsStore.save(settings);
        if (saved) {
          systemSettings = settings;
          activeEffectiveSettings.system = settings;
        }
      }
      if (!saved) {
        qWarning().noquote() << QString::fromStdString(saved.message);
        const auto rolledBack = worker.submit(
          genplusgx::EmulationCommand::updateSystemSettings(
            ++systemSettingsOperationId, previous));
        if (!rolledBack) {
          saved.message += " The runtime rollback also failed: " +
            rolledBack.message;
        }
      } else if (activePerGameIdentity) {
        window.setPerGameSettingsSession(
          activePerGameSettings, globalGameSettings());
      }
      return saved;
    });
  const auto refreshRecentGamesMenu = [&recentGames, &window] {
    std::vector<std::filesystem::path> paths;
    paths.reserve(recentGames.size());
    for (const auto& entry : recentGames.entries()) {
      paths.push_back(entry.path);
    }
    window.setRecentGames(std::move(paths));
  };
  refreshRecentGamesMenu();
  window.setClearRecentGamesSink(
    [&recentGames, &recentGamesStore, &refreshRecentGamesMenu] {
      auto candidate = recentGames;
      candidate.clear();
      const auto saved = recentGamesStore.save(candidate);
      if (saved) {
        recentGames = std::move(candidate);
        refreshRecentGamesMenu();
      } else {
        qWarning().noquote() << QString::fromStdString(saved.message);
      }
      return saved;
    });
  genplusgx::input::InputAggregator inputAggregator;
  genplusgx::input::KeyboardInput keyboardInput{&window};
  genplusgx::input::ControllerInput controllerInput;
  std::uint64_t inputSettingsOperationId = 900'000U;
  window.setInputConfiguration(inputConfiguration);
  const auto applyInputProfile =
    [&inputSettingsOperationId, &keyboardInput, &controllerInput, &worker](
      const genplusgx::input::InputConfiguration& source,
      const std::string& name) {
      const auto found = std::ranges::find_if(source.profiles,
        [&name](const auto& profile) { return profile.name == name; });
      if (found == source.profiles.end()) {
        return false;
      }
      const auto* profile = &*found;
      bool applied = true;
      if (!keyboardInput.setBindings(profile->keyboardBindings)) {
        qWarning() << "The active keyboard profile was rejected by the runtime.";
        applied = false;
      }
      if (!controllerInput.setBindings(profile->controllerBindings)) {
        qWarning() << "The active controller profile was rejected by the runtime.";
        applied = false;
      }
      if (!controllerInput.setAxisBindings(profile->controllerAxisBindings)) {
        qWarning() << "The active controller axis profile was rejected by the runtime.";
        applied = false;
      }
      controllerInput.setDeadzone(profile->deadzone);
      if (applied) {
        const auto submitted = worker.submit(
          genplusgx::EmulationCommand::updateInputSettings(
            ++inputSettingsOperationId,
            genplusgx::input::coreInputSettings(*profile)));
        if (!submitted) {
          qWarning().noquote() << QString::fromStdString(submitted.message);
          applied = false;
        }
      }
      return applied;
    };
  const auto applyActiveInputProfile =
    [&applyInputProfile, &inputConfiguration] {
      return applyInputProfile(
        inputConfiguration, inputConfiguration.activeProfile);
    };
  if (!applyActiveInputProfile()) {
    recordStartupIssue(
      "Input configuration",
      "The active input profile could not be applied; some controls may be unavailable.");
  }
  window.setInputConfigurationSink(
    [&activeEffectiveSettings, &activePerGameIdentity, &activePerGameSettings,
     &globalGameSettings, &inputConfiguration, &inputProfileStore,
     &applyInputProfile, &window](
      const genplusgx::input::InputConfiguration& configuration) {
      auto requestedProfile = configuration.activeProfile;
      if (activePerGameIdentity && activePerGameSettings.inputProfile) {
        const auto overrideExists = std::ranges::any_of(
          configuration.profiles, [&activePerGameSettings](const auto& profile) {
            return profile.name == *activePerGameSettings.inputProfile;
          });
        if (overrideExists) {
          requestedProfile = *activePerGameSettings.inputProfile;
        }
      }
      const auto rollbackProfile = activeEffectiveSettings.inputProfile.empty()
        ? inputConfiguration.activeProfile
        : activeEffectiveSettings.inputProfile;
      const auto rollback = [&applyInputProfile, &inputConfiguration,
                             &rollbackProfile] {
        return applyInputProfile(inputConfiguration, rollbackProfile) ||
          (rollbackProfile != inputConfiguration.activeProfile &&
            applyInputProfile(
              inputConfiguration, inputConfiguration.activeProfile));
      };
      if (!applyInputProfile(configuration, requestedProfile)) {
        const bool rolledBack = rollback();
        return genplusgx::PersistenceStatus{
          .error = genplusgx::PersistenceError::invalidData,
          .message = std::string{
            "The selected input profile could not be applied to the runtime."} +
            (rolledBack ? "" : " The previous runtime profile could not be restored."),
        };
      }
      const auto saved = inputProfileStore.save(configuration);
      if (!saved) {
        qWarning().noquote() << QString::fromStdString(saved.message);
        auto failure = genplusgx::PersistenceStatus{
          .error = genplusgx::PersistenceError::fileWriteFailed,
          .message = saved.message,
        };
        if (!rollback()) {
          failure.message += " The previous runtime profile could not be restored.";
        }
        return failure;
      }
      inputConfiguration = configuration;
      activeEffectiveSettings.inputProfile = requestedProfile;
      if (activePerGameIdentity) {
        window.setPerGameSettingsSession(
          activePerGameSettings, globalGameSettings());
      }
      return genplusgx::PersistenceStatus{};
    });
  window.setControllerAssignmentSink(
    [&controllerInput, &window](std::uint32_t instanceId, std::size_t player) {
      if (!controllerInput.assignPlayer(instanceId, player)) {
        qWarning() << "A controller player assignment was rejected.";
        return genplusgx::PersistenceStatus{
          .error = genplusgx::PersistenceError::invalidData,
          .message = "The selected controller or player slot is no longer available.",
        };
      }
      window.setConnectedControllers(controllerInput.controllers());
      return genplusgx::PersistenceStatus{};
    });
  keyboardInput.attach(*window.displayWidget());
  std::uint64_t inputOperationId = 1'000'000U;
  bool runtimeFailureReported = false;
  const auto reportRuntimeFailure = [&runtimeFailureReported, &window](
                                      const std::string& detail) {
    qWarning().noquote() << QString::fromStdString(detail);
    if (!runtimeFailureReported) {
      runtimeFailureReported = true;
      window.showEmulationRuntimeError(detail);
    }
  };
  inputAggregator.setSnapshotSink(
    [&inputOperationId, &reportRuntimeFailure, &worker](
      const genplusgx::InputSnapshot& snapshot) {
      const auto state = worker.state();
      if (state != genplusgx::EmulationWorkerState::paused &&
          state != genplusgx::EmulationWorkerState::running) {
        return;
      }
      const auto submitted = worker.submit(
        genplusgx::EmulationCommand::updateInput(++inputOperationId, snapshot));
      if (!submitted) {
        reportRuntimeFailure(
          "Controller input could not reach the emulation service: " +
          submitted.message);
      }
    });
  keyboardInput.setSnapshotSink(
    [&inputAggregator](const genplusgx::InputSnapshot& snapshot) {
      static_cast<void>(inputAggregator.updateKeyboard(snapshot));
    });
  controllerInput.setSnapshotSink(
    [&inputAggregator](const genplusgx::InputSnapshot& snapshot) {
      static_cast<void>(inputAggregator.updateControllers(snapshot));
    });
  controllerInput.setConnectionSink(
    [&controllerInput, &window](
      const genplusgx::input::ControllerInfo& controller, bool connected) {
      qInfo().noquote()
        << (connected ? "Controller connected:" : "Controller disconnected:")
        << QString::fromStdString(controller.name)
        << "(player" << static_cast<qulonglong>(controller.player + 1U) << ')';
      window.setConnectedControllers(controllerInput.controllers());
    });
  controllerInput.setCaptureSink(
    [&window](SDL_GamepadButton button) {
      return window.captureControllerButton(button);
    });
  static_cast<void>(inputAggregator.updateKeyboard(keyboardInput.snapshot()));
  const auto controllerInitialized = controllerInput.initialize();
  if (!controllerInitialized) {
    qWarning().noquote() << QString::fromStdString(controllerInitialized.message);
    recordStartupIssue("Controller service", controllerInitialized.message);
  }

  const auto applyEffectiveSettings =
    [&applyInputProfile, &audioOutput, &audioSettingsOperationId,
     &firmwareSettingsOperationId, &inputConfiguration,
     &systemSettingsOperationId, &videoSettingsOperationId, &window, &worker](
      const genplusgx::settings::EffectiveGameSettings& settings) {
      const auto profile = std::ranges::find_if(inputConfiguration.profiles,
        [&settings](const auto& candidate) {
          return candidate.name == settings.inputProfile;
        });
      if (profile == inputConfiguration.profiles.end()) {
        return workerPersistenceFailure(
          "The selected per-game input profile no longer exists.");
      }
      const std::array submitted{
        worker.submit(genplusgx::EmulationCommand::updateSystemSettings(
          ++systemSettingsOperationId, settings.system)),
        worker.submit(genplusgx::EmulationCommand::updateFirmwareSettings(
          ++firmwareSettingsOperationId, coreFirmwareSettings(settings.bios))),
        worker.submit(genplusgx::EmulationCommand::updateVideoSettings(
          ++videoSettingsOperationId, settings.video.core)),
        worker.submit(genplusgx::EmulationCommand::updateAudioSettings(
          ++audioSettingsOperationId, settings.audio.core)),
      };
      for (const auto& status : submitted) {
        if (!status) {
          return workerPersistenceFailure(
            "The emulation worker could not accept effective settings: " +
            status.message);
        }
      }
      const auto volume = audioOutput.setVolumePercent(
        settings.audio.masterVolumePercent);
      if (!volume) {
        return workerPersistenceFailure(volume.message);
      }
      audioOutput.setMuted(settings.audio.muted);
      if (!applyInputProfile(inputConfiguration, settings.inputProfile)) {
        return workerPersistenceFailure(
          "The selected input profile could not be applied safely.");
      }
      window.setVideoSettings(settings.video);
      window.setAudioSettings(settings.audio);
      window.setSystemSettings(settings.system);
      return genplusgx::PersistenceStatus{};
    };

  enum class PendingLoadPhase {
    metadata,
    coreLoad,
  };
  std::string diagnosticLoadedGame;
  std::string diagnosticLoadedSystem;
  std::string diagnosticLoadedRegion;
  struct PendingLoad final {
    std::uint64_t operationId{0};
    std::uint64_t metadataOperationId{0};
    std::filesystem::path path;
    std::optional<genplusgx::GameIdentity> identity;
    genplusgx::settings::PerGameSettings overrides;
    genplusgx::settings::EffectiveGameSettings effective;
    std::optional<genplusgx::GameIdentity> previousIdentity;
    genplusgx::settings::PerGameSettings previousOverrides;
    genplusgx::settings::EffectiveGameSettings previousEffective;
    std::string diagnosticGame;
    std::string diagnosticSystem;
    std::string diagnosticRegion;
    std::string warning;
    PendingLoadPhase phase{PendingLoadPhase::metadata};
  };
  std::uint64_t lifecycleOperationId = 2'000'000U;
  std::uint64_t emulationControlOperationId = 2'100'000U;
  std::uint64_t loadMetadataOperationId = 2'250'000U;
  std::uint64_t stateOperationId = 3'000'000U;
  std::uint64_t gameGeneration = 0U;
  bool stateSessionAvailable = false;
  std::optional<PendingLoad> pendingLoad;
  std::optional<std::uint64_t> pendingUnload;
  struct PendingDisc final {
    std::uint64_t operationId{0};
    genplusgx::ui::DiscUiOperation operation{
      genplusgx::ui::DiscUiOperation::change};
  };
  std::uint64_t discOperationId = 2'500'000U;
  std::optional<PendingDisc> pendingDisc;
  std::filesystem::path closingGamePath;
  enum class PendingStatePhase {
    capturing,
    saving,
    loading,
    restoring,
    deleting,
  };
  struct PendingState final {
    std::uint64_t operationId{0};
    std::uint64_t gameGeneration{0};
    std::uint32_t slot{0};
    genplusgx::ui::StateUiOperation operation{
      genplusgx::ui::StateUiOperation::save};
    PendingStatePhase phase{PendingStatePhase::capturing};
  };
  std::optional<PendingState> pendingState;
  std::optional<std::uint64_t> stateActivationOperation;
  struct PendingMetadata final {
    std::uint64_t operationId{0};
    std::filesystem::path path;
  };
  std::uint64_t metadataOperationId = 4'000'000U;
  std::optional<PendingMetadata> pendingMetadata;
  struct PendingCheatMetadata final {
    std::uint64_t operationId{0};
    std::filesystem::path path;
    genplusgx::cheats::CheatSystem system{
      genplusgx::cheats::CheatSystem::genesis};
  };
  std::uint64_t cheatMetadataOperationId = 4'250'000U;
  std::optional<PendingCheatMetadata> pendingCheatMetadata;
  std::uint64_t cheatOperationId = 4'300'000U;
  std::optional<std::uint64_t> pendingCheatOperation;
  std::optional<genplusgx::GameIdentity> activeCheatIdentity;
  std::optional<genplusgx::cheats::CheatSystem> activeCheatSystem;
  genplusgx::cheats::CheatConfiguration activeCheatConfiguration;
  std::uint64_t screenshotOperationId = 4'500'000U;
  std::optional<std::uint64_t> pendingScreenshot;
  window.setDiagnosticsSnapshotProvider(
    [&audioOutput, &biosManager, &controllerInput, &diagnosticLoadedGame,
     &diagnosticLoadedRegion, &diagnosticLoadedSystem, &frontendLogger,
     &window] {
      auto snapshot = genplusgx::diagnostics::staticDiagnosticsSnapshot();
      snapshot.renderer = window.displayWidget()->usesAcceleratedRenderer()
        ? "OpenGL texture renderer"
        : "Qt software painter";
      snapshot.audioDevice = audioOutput.isInitialized()
        ? audioOutput.deviceName()
        : "Unavailable";
      if (window.isGameLoaded()) {
        snapshot.loadedGame = diagnosticLoadedGame.empty()
          ? "Loaded game (metadata unavailable)"
          : diagnosticLoadedGame;
        snapshot.loadedSystem = diagnosticLoadedSystem.empty()
          ? "Unknown"
          : diagnosticLoadedSystem;
        snapshot.loadedRegion = diagnosticLoadedRegion.empty()
          ? "Unknown"
          : diagnosticLoadedRegion;
      } else {
        snapshot.loadedGame = "None";
        snapshot.loadedSystem = "None";
        snapshot.loadedRegion = "None";
      }
      snapshot.controllerCount = controllerInput.controllers().size();
      const auto audioMetrics = audioOutput.metrics();
      snapshot.audioUnderruns = audioMetrics.ring.underrunCount;
      snapshot.audioOverruns = audioMetrics.ring.overrunCount;
      if (const auto ring = audioOutput.ringBuffer()) {
        snapshot.audioBufferedFrames = ring->occupancyFrames();
        snapshot.audioCapacityFrames = ring->capacityFrames();
      }
      snapshot.loggerActive = frontendLogger.installed();
      snapshot.logger = frontendLogger.metrics();
      const auto& bios = biosManager.snapshot();
      snapshot.bios.reserve(genplusgx::platform::biosSlotCount);
      for (std::size_t index = 0U;
           index < genplusgx::platform::biosSlotCount;
           ++index) {
        const auto slot = static_cast<genplusgx::platform::BiosSlot>(index);
        const auto& descriptor = genplusgx::platform::biosDescriptor(slot);
        const auto& validation = bios.validation[index];
        snapshot.bios.push_back({
          .name = std::string{descriptor.displayName},
          .status = biosValidationStateName(validation.state),
          .sha256Prefix = validation.valid()
            ? validation.sha256.substr(0U, 12U)
            : std::string{},
        });
      }
      return snapshot;
    });
  if (screenshotServiceStarted) {
    window.setScreenshotSink(
      [&pendingScreenshot, &screenshotOperationId, &screenshotService,
       &screenshotSettings, &window](genplusgx::CoreVideoFrameInfo frame,
                                     std::vector<std::uint16_t> pixels) {
        const auto operationId = ++screenshotOperationId;
        pendingScreenshot = operationId;
        auto title = genplusgx::ui::pathToQString(
          window.loadedGamePath().stem()).toUtf8().toStdString();
        if (title.empty()) {
          title = "screenshot";
        }
        const auto submitted = screenshotService.request(
          operationId, screenshotSettings.directory, std::move(title), frame,
          std::move(pixels));
        if (!submitted) {
          pendingScreenshot.reset();
          window.showScreenshotError(submitted.message);
        }
      });
  }
  window.setPerGameSettingsSink(
    [&activeEffectiveSettings, &activePerGameIdentity, &activePerGameSettings,
     &applyEffectiveSettings, &globalGameSettings, &perGameSettingsStore,
     &window](const genplusgx::settings::PerGameSettings& overrides) {
      if (!activePerGameIdentity) {
        return workerPersistenceFailure(
          "No active per-game settings identity is available.");
      }
      if (!genplusgx::settings::validatePerGameSettings(overrides)) {
        return workerPersistenceFailure(
          "The requested per-game settings are invalid.");
      }
      const auto previous = activeEffectiveSettings;
      const auto effective = genplusgx::settings::resolvePerGameSettings(
        globalGameSettings(), overrides);
      const auto applied = applyEffectiveSettings(effective);
      if (!applied) {
        static_cast<void>(applyEffectiveSettings(previous));
        return applied;
      }
      const auto saved = perGameSettingsStore.save(
        *activePerGameIdentity, overrides);
      if (!saved) {
        static_cast<void>(applyEffectiveSettings(previous));
        return saved;
      }
      activePerGameSettings = overrides;
      activeEffectiveSettings = effective;
      window.setPerGameSettingsSession(
        activePerGameSettings, globalGameSettings());
      return genplusgx::PersistenceStatus{};
    });
  window.setGameLoadSink(
    [&activeEffectiveSettings, &activePerGameIdentity, &activePerGameSettings,
     &applyEffectiveSettings, &globalGameSettings, &lifecycleOperationId,
     &loadMetadataOperationId, &metadataService, &pendingLoad, &window, &worker](
      const std::filesystem::path& path) {
      const auto metadataId = ++loadMetadataOperationId;
      pendingLoad = PendingLoad{
        .operationId = 0U,
        .metadataOperationId = metadataId,
        .path = path,
        .identity = std::nullopt,
        .overrides = {},
        .effective = genplusgx::settings::resolvePerGameSettings(
          globalGameSettings(), {}),
        .previousIdentity = activePerGameIdentity,
        .previousOverrides = activePerGameSettings,
        .previousEffective = activeEffectiveSettings,
        .diagnosticGame = {},
        .diagnosticSystem = {},
        .diagnosticRegion = {},
        .warning = {},
        .phase = PendingLoadPhase::metadata,
      };
      const auto requested = metadataService.request(metadataId, path);
      if (requested) {
        return;
      }
      pendingLoad->warning =
        "Per-game metadata preflight failed; global settings were used: " +
        requested.message;
      qWarning().noquote() << QString::fromStdString(pendingLoad->warning);
      const auto applied = applyEffectiveSettings(pendingLoad->effective);
      if (!applied) {
        const auto previous = pendingLoad->previousEffective;
        pendingLoad.reset();
        static_cast<void>(applyEffectiveSettings(previous));
        window.showGameLoadError(path, applied.message, false);
        return;
      }
      pendingLoad->operationId = ++lifecycleOperationId;
      pendingLoad->phase = PendingLoadPhase::coreLoad;
      const auto submitted = worker.submit(genplusgx::EmulationCommand::load(
        pendingLoad->operationId, path));
      if (!submitted) {
        const auto previous = pendingLoad->previousEffective;
        pendingLoad.reset();
        static_cast<void>(applyEffectiveSettings(previous));
        window.showGameLoadError(path, submitted.message, false);
      }
    });
  window.setGameCloseSink(
    [&closingGamePath, &lifecycleOperationId, &pendingUnload, &window, &worker] {
      closingGamePath = window.loadedGamePath();
      const auto operationId = ++lifecycleOperationId;
      pendingUnload = operationId;
      const auto submitted = worker.submit(genplusgx::EmulationCommand::simple(
        genplusgx::EmulationCommandType::unloadGame, operationId));
      if (!submitted) {
        pendingUnload.reset();
        window.setGameLoaded(closingGamePath);
        window.showGameCloseError(submitted.message);
        qWarning().noquote() << QString::fromStdString(submitted.message);
      }
    });
  window.setEmulationControlSink(
    [&emulationControlOperationId, &worker](
      genplusgx::ui::EmulationUiOperation operation, bool enabled) {
      using UiOperation = genplusgx::ui::EmulationUiOperation;
      using CommandType = genplusgx::EmulationCommandType;
      const auto operationId = ++emulationControlOperationId;
      genplusgx::EmulationCommand command;
      switch (operation) {
        case UiOperation::pause:
          command = genplusgx::EmulationCommand::simple(
            CommandType::pause, operationId);
          break;
        case UiOperation::resume:
          command = genplusgx::EmulationCommand::simple(
            CommandType::resume, operationId);
          break;
        case UiOperation::hardReset:
          command = genplusgx::EmulationCommand::simple(
            CommandType::hardReset, operationId);
          break;
        case UiOperation::softReset:
          command = genplusgx::EmulationCommand::simple(
            CommandType::softReset, operationId);
          break;
        case UiOperation::frameAdvance:
          command = genplusgx::EmulationCommand::simple(
            CommandType::frameAdvance, operationId);
          break;
        case UiOperation::setFastForward:
          command = genplusgx::EmulationCommand::fastForward(
            operationId, enabled);
          break;
      }
      const auto submitted = worker.submit(std::move(command));
      if (!submitted) {
        qWarning().noquote() << QString::fromStdString(submitted.message);
      }
      return submitted.ok();
    });
  window.setDiscOperationSink(
    [&discOperationId, &pendingDisc, &window, &worker](
      genplusgx::ui::DiscUiOperation operation,
      const std::filesystem::path& path,
      bool ejected) {
      const auto operationId = ++discOperationId;
      const auto command = operation == genplusgx::ui::DiscUiOperation::change
        ? genplusgx::EmulationCommand::changeDisc(operationId, path)
        : genplusgx::EmulationCommand::discEjected(operationId, ejected);
      const auto submitted = worker.submit(command);
      if (!submitted) {
        window.setDiscOperationBusy(false);
        window.showDiscOperationError(operation, submitted.message);
        return;
      }
      pendingDisc = PendingDisc{
        .operationId = operationId,
        .operation = operation,
      };
    });
  window.setStateOperationSink(
    [&gameGeneration, &pendingState, &stateOperationId, &stateStorage,
     &window, &worker](genplusgx::ui::StateUiOperation operation,
                       std::uint32_t slot) {
      if (gameGeneration == 0U || pendingState) {
        window.setStateOperationBusy(false);
        window.showStateOperationError(
          operation, "A save-state operation is already in progress.");
        return;
      }
      const auto operationId = ++stateOperationId;
      PendingState pending{
        .operationId = operationId,
        .gameGeneration = gameGeneration,
        .slot = slot,
        .operation = operation,
        .phase = PendingStatePhase::capturing,
      };
      genplusgx::StateStorageStatus submitted;
      if (operation == genplusgx::ui::StateUiOperation::save) {
        const auto workerSubmitted = worker.submit(
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::captureState, operationId));
        if (!workerSubmitted) {
          window.setStateOperationBusy(false);
          window.showStateOperationError(operation, workerSubmitted.message);
          return;
        }
        pending.phase = PendingStatePhase::capturing;
      } else {
        const auto commandType = operation == genplusgx::ui::StateUiOperation::load
          ? genplusgx::StateStorageCommandType::loadSlot
          : genplusgx::StateStorageCommandType::deleteSlot;
        submitted = stateStorage.submit(genplusgx::StateStorageCommand::simple(
          commandType, operationId, gameGeneration, slot));
        if (!submitted) {
          window.setStateOperationBusy(false);
          window.showStateOperationError(operation, submitted.message);
          return;
        }
        pending.phase = operation == genplusgx::ui::StateUiOperation::load
          ? PendingStatePhase::loading
          : PendingStatePhase::deleting;
      }
      pendingState = std::move(pending);
    });
  window.setGameInformationRequestSink(
    [&metadataOperationId, &metadataService, &pendingMetadata, &window](
      const std::filesystem::path& path) {
      const auto operationId = ++metadataOperationId;
      pendingMetadata = PendingMetadata{
        .operationId = operationId,
        .path = path,
      };
      const auto submitted = metadataService.request(operationId, path);
      if (!submitted) {
        pendingMetadata.reset();
        window.showGameInformationError(submitted.message);
      }
    });
  window.setCheatConfigurationSink(
    [&activeCheatConfiguration, &activeCheatIdentity, &activeCheatSystem,
     &cheatOperationId, &cheatStore, &pendingCheatOperation, &worker](
      const genplusgx::cheats::CheatConfiguration& configuration) {
      if (!activeCheatIdentity || !activeCheatSystem) {
        return workerPersistenceFailure(
          "No active per-game cheat session is available.");
      }
      std::vector<genplusgx::CoreCheatPatch> patches;
      const auto validated = genplusgx::cheats::validateCheatConfiguration(
        *activeCheatSystem, configuration, &patches);
      if (!validated) {
        return workerPersistenceFailure(validated.message);
      }
      const auto saved = cheatStore.save(
        *activeCheatIdentity, *activeCheatSystem, configuration);
      if (!saved) {
        return saved;
      }
      const auto operationId = ++cheatOperationId;
      const auto submitted = worker.submit(
        genplusgx::EmulationCommand::updateCheats(operationId, patches));
      if (!submitted) {
        const auto rollback = cheatStore.save(
          *activeCheatIdentity,
          *activeCheatSystem,
          activeCheatConfiguration);
        std::string detail = "The emulation worker could not accept the cheat update: " +
          submitted.message;
        if (!rollback) {
          detail += " The previous cheat file also could not be restored: " +
            rollback.message;
        }
        return workerPersistenceFailure(std::move(detail));
      }
      pendingCheatOperation = operationId;
      activeCheatConfiguration = configuration;
      return genplusgx::PersistenceStatus{};
    });
  auto nextInstrumentationLog = std::chrono::steady_clock::now() +
    std::chrono::seconds{5};
  std::uint64_t loggedAudioUnderruns = 0U;
  std::uint64_t loggedAudioOverruns = 0U;
  std::uint64_t loggedLateFrames = 0U;
  std::uint64_t loggedPacingResynchronizations = 0U;
  bool audioControlFailureReported = false;
  bool stateServiceFailureReported = false;
  genplusgx::FrameRateSampler frameRateSampler;
  auto nextFrameRateSample = std::chrono::steady_clock::now();
  QTimer eventPump;
  eventPump.setInterval(8);
  QObject::connect(
    &eventPump,
    &QTimer::timeout,
    &window,
    [&activeEffectiveSettings, &activePerGameIdentity, &activePerGameSettings,
     &applyEffectiveSettings, &audioControlFailureReported, &audioOutput,
     &controllerInput,
     &loggedAudioOverruns, &loggedAudioUnderruns, &loggedLateFrames,
     &loggedPacingResynchronizations, &nextInstrumentationLog,
     &frameRateSampler, &nextFrameRateSample,
     &deferredLibraryLaunches, &flushDeferredLibraryLaunches, &gameGeneration,
     &gameLibraryScanner, &globalGameSettings,
     &diagnosticLoadedGame, &diagnosticLoadedRegion, &diagnosticLoadedSystem,
     &inputAggregator, &inputConfiguration, &inputOperationId,
     &libraryScansInFlight,
     &lifecycleOperationId,
     &metadataService, &pendingDisc,
     &pendingLoad, &pendingMetadata, &pendingState, &pendingUnload,
     &activeCheatConfiguration, &activeCheatIdentity, &activeCheatSystem,
     &cheatMetadataOperationId, &cheatOperationId, &cheatStore,
     &pendingCheatMetadata, &pendingCheatOperation, &perGameSettingsStore,
     &pendingScreenshot, &screenshotService,
     &closingGamePath, &recentGames, &recentGamesStore, &recordLibraryLaunch,
     &refreshGameLibrary, &refreshRecentGamesMenu, &stateActivationOperation,
     &stateOperationId,
     &stateServiceFailureReported, &stateSessionAvailable, &stateStorage,
     &reportRuntimeFailure, &runtimeFailureReported, &worker, &window] {
    static_cast<void>(controllerInput.pollEvents());
    const auto audioDeviceEvents = audioOutput.pollDeviceEvents();
    if (audioDeviceEvents.playbackDevicesChanged) {
      std::vector<std::string> names;
      const auto devices = genplusgx::availableAudioOutputDevices();
      names.reserve(devices.size());
      for (const auto& device : devices) {
        names.push_back(device.name);
      }
      window.setAvailableAudioDevices(std::move(names));
    }
    if (audioDeviceEvents.selectedDeviceRemoved) {
      if (audioDeviceEvents.recoveredToDefault) {
        qWarning().noquote()
          << "Selected audio device disconnected; recovered with:"
          << QString::fromStdString(audioOutput.deviceName());
      } else {
        const auto& detail = audioDeviceEvents.recoveryStatus.message;
        qWarning().noquote() << QString::fromStdString(detail);
        window.showAudioOutputError(detail);
      }
    } else if (audioDeviceEvents.formatChanged) {
      qInfo() << "Audio device format changed; SDL stream conversion remains active.";
    }
    const auto instrumentationNow = std::chrono::steady_clock::now();
    std::optional<genplusgx::EmulationWorkerMetrics> runtimeMetrics;
    if (instrumentationNow >= nextFrameRateSample) {
      runtimeMetrics = worker.metrics();
      window.setNominalVideoRate(runtimeMetrics->targetFramesPerSecond);
      const bool emulationRunning = window.isGameLoaded() &&
        worker.state() == genplusgx::EmulationWorkerState::running;
      if (const auto measured = frameRateSampler.observe(
            runtimeMetrics->pacedFrameCount,
            emulationRunning,
            instrumentationNow)) {
        window.setMeasuredFrameRate(*measured);
      }
      nextFrameRateSample = instrumentationNow + std::chrono::milliseconds{500};
    }
    if (instrumentationNow >= nextInstrumentationLog) {
      const auto audioMetrics = audioOutput.metrics().ring;
      if (audioMetrics.underrunCount > loggedAudioUnderruns ||
          audioMetrics.overrunCount > loggedAudioOverruns) {
        qWarning().noquote()
          << "Audio buffer anomaly: underruns="
          << static_cast<qulonglong>(audioMetrics.underrunCount)
          << "overruns="
          << static_cast<qulonglong>(audioMetrics.overrunCount);
      }
      loggedAudioUnderruns = audioMetrics.underrunCount;
      loggedAudioOverruns = audioMetrics.overrunCount;
      if (!runtimeMetrics) {
        runtimeMetrics = worker.metrics();
      }
      const auto& timingMetrics = *runtimeMetrics;
      if (timingMetrics.lateFrameCount > loggedLateFrames ||
          timingMetrics.pacingResynchronizations >
            loggedPacingResynchronizations) {
        qWarning().noquote()
          << "Timing anomaly: late frames="
          << static_cast<qulonglong>(timingMetrics.lateFrameCount)
          << "resynchronizations="
          << static_cast<qulonglong>(timingMetrics.pacingResynchronizations)
          << "maximum lateness us="
          << static_cast<qlonglong>(
               timingMetrics.maximumLatenessMicroseconds);
      }
      loggedLateFrames = timingMetrics.lateFrameCount;
      loggedPacingResynchronizations = timingMetrics.pacingResynchronizations;
      nextInstrumentationLog = instrumentationNow + std::chrono::seconds{5};
    }
    while (auto event = worker.pollEvent()) {
      if (event->type != genplusgx::EmulationEventType::frameCompleted &&
          window.isGameLoaded() &&
          (event->workerState == genplusgx::EmulationWorkerState::paused ||
           event->workerState == genplusgx::EmulationWorkerState::running)) {
        window.setEmulationControlState(
          event->workerState == genplusgx::EmulationWorkerState::paused,
          event->fastForward);
      }
      if (event->type == genplusgx::EmulationEventType::frameCompleted) {
        static_cast<void>(window.presentLatestFrame());
        continue;
      }
      if (pendingCheatOperation &&
          event->operationId == *pendingCheatOperation &&
          event->command == genplusgx::EmulationCommandType::cheats) {
        pendingCheatOperation.reset();
        if (!event->succeeded()) {
          window.clearCheatSession();
          window.showCheatError(event->message);
        }
      }
      if (pendingState && event->operationId == pendingState->operationId &&
          pendingState->gameGeneration == gameGeneration) {
        if (pendingState->phase == PendingStatePhase::capturing &&
            event->command == genplusgx::EmulationCommandType::captureState) {
          if (!event->succeeded()) {
            const auto operation = pendingState->operation;
            pendingState.reset();
            window.setStateOperationBusy(false);
            window.showStateOperationError(operation, event->message);
          } else {
            const auto submitted = stateStorage.submit(
              genplusgx::StateStorageCommand::save(
                event->operationId,
                gameGeneration,
                pendingState->slot,
                event->frameNumber,
                std::move(event->rawState)));
            if (!submitted) {
              const auto operation = pendingState->operation;
              pendingState.reset();
              window.setStateOperationBusy(false);
              window.showStateOperationError(operation, submitted.message);
            } else {
              pendingState->phase = PendingStatePhase::saving;
            }
          }
        } else if (pendingState->phase == PendingStatePhase::restoring &&
                   event->command ==
                     genplusgx::EmulationCommandType::restoreState) {
          const auto operation = pendingState->operation;
          const auto slot = pendingState->slot;
          pendingState.reset();
          window.setStateOperationBusy(false);
          if (event->succeeded()) {
            window.showStateOperationSuccess(operation, slot);
            qInfo().noquote() << "Save state loaded: slot"
                              << static_cast<qulonglong>(slot);
          } else {
            qWarning().noquote() << "Save-state restore failed:"
                                 << QString::fromStdString(event->message);
            window.showStateOperationError(operation, event->message);
          }
        }
      }
      if (pendingLoad && pendingLoad->phase == PendingLoadPhase::coreLoad &&
          event->operationId == pendingLoad->operationId &&
          event->command == genplusgx::EmulationCommandType::loadGame) {
        const auto completedLoad = std::move(*pendingLoad);
        const auto loadedPath = completedLoad.path;
        pendingLoad.reset();
        if (event->succeeded()) {
          runtimeFailureReported = false;
          audioControlFailureReported = false;
          window.clearCheatSession();
          window.clearPerGameSettingsSession();
          activeCheatIdentity.reset();
          activeCheatSystem.reset();
          activeCheatConfiguration = {};
          pendingCheatMetadata.reset();
          pendingCheatOperation.reset();
          activePerGameIdentity = completedLoad.identity;
          activePerGameSettings = completedLoad.overrides;
          activeEffectiveSettings = completedLoad.effective;
          diagnosticLoadedGame = completedLoad.diagnosticGame;
          diagnosticLoadedSystem = event->disc.segaCd
            ? "Sega CD / Mega CD"
            : completedLoad.diagnosticSystem;
          diagnosticLoadedRegion = event->disc.segaCd
            ? discRegionName(event->disc.region)
            : completedLoad.diagnosticRegion;
          window.setGameLoaded(loadedPath);
          window.setEmulationControlState(true, false);
          if (activePerGameIdentity) {
            window.setPerGameSettingsSession(
              activePerGameSettings, globalGameSettings());
          }
          if (!completedLoad.warning.empty()) {
            window.showPerGameSettingsError(completedLoad.warning);
          }
          window.setSegaCdSession(
            event->disc.segaCd,
            discRegionName(event->disc.region),
            event->disc.path,
            event->disc.trayOpen,
            event->disc.discPresent);
          window.setGameRuntimeIdentity(
            diagnosticLoadedSystem, diagnosticLoadedRegion);
          qInfo().noquote() << "Game loaded:"
                            << QString::fromStdString(
                                 diagnosticLoadedGame.empty()
                                   ? "Metadata unavailable"
                                   : diagnosticLoadedGame)
                            << '-'
                            << QString::fromStdString(
                                 diagnosticLoadedSystem.empty()
                                   ? "Unknown system"
                                   : diagnosticLoadedSystem);
          ++gameGeneration;
          stateSessionAvailable = false;
          const auto activationId = ++stateOperationId;
          stateActivationOperation = activationId;
          const auto stateActivated = stateStorage.submit(
            genplusgx::StateStorageCommand::activate(
              activationId, gameGeneration, loadedPath, event->hardware));
          if (!stateActivated) {
            stateActivationOperation.reset();
            stateSessionAvailable = false;
            window.showStateOperationError(
              genplusgx::ui::StateUiOperation::load,
              "Save states are unavailable: " + stateActivated.message);
          }
          const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
          auto recentCandidate = recentGames;
          if (recentCandidate.add(loadedPath, now)) {
            const auto saved = recentGamesStore.save(recentCandidate);
            if (!saved) {
              qWarning().noquote() << QString::fromStdString(saved.message);
              window.showRecentGamesError(saved.message);
            } else {
              recentGames = std::move(recentCandidate);
              refreshRecentGamesMenu();
            }
          }
          if (libraryScansInFlight.empty()) {
            const auto recorded = recordLibraryLaunch(loadedPath, now);
            if (!recorded) {
              qWarning().noquote() << QString::fromStdString(recorded.message);
              window.showGameLibraryError(recorded.message);
            }
          } else {
            if (!deferredLibraryLaunches.tryPush({loadedPath, now})) {
              const std::string detail =
                "The bounded library-history queue is full; this launch "
                "could not be recorded.";
              qWarning().noquote() << QString::fromStdString(detail);
              window.showGameLibraryError(detail);
            }
          }
          const auto inputSubmitted = worker.submit(
            genplusgx::EmulationCommand::updateInput(
              ++inputOperationId, inputAggregator.snapshot()));
          if (!inputSubmitted) {
            reportRuntimeFailure(
              "The game loaded, but its initial input state could not reach "
              "the emulation service: " + inputSubmitted.message);
          }
          const auto started = worker.submit(genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::start, ++lifecycleOperationId));
          if (!started) {
            reportRuntimeFailure(
              "The game loaded, but emulation could not start: " +
              started.message);
          }
          const auto cheatMetadataId = ++cheatMetadataOperationId;
          pendingCheatMetadata = PendingCheatMetadata{
            .operationId = cheatMetadataId,
            .path = loadedPath,
            .system = cheatSystem(event->hardware),
          };
          const auto cheatMetadataSubmitted = metadataService.request(
            cheatMetadataId, loadedPath);
          if (!cheatMetadataSubmitted) {
            pendingCheatMetadata.reset();
            window.showCheatError(
              "Cheat metadata could not be read: " +
              cheatMetadataSubmitted.message);
          }
        } else {
          qWarning().noquote() << "Game load failed:"
                               << QString::fromStdString(event->message);
          const bool previousGameRemainsLoaded =
            event->coreError == genplusgx::CoreError::persistenceFailed &&
            (event->workerState == genplusgx::EmulationWorkerState::paused ||
             event->workerState == genplusgx::EmulationWorkerState::running);
          window.showGameLoadError(
            loadedPath, event->message, !previousGameRemainsLoaded);
          if (previousGameRemainsLoaded) {
            const auto restored = applyEffectiveSettings(
              completedLoad.previousEffective);
            if (!restored) {
              qWarning().noquote() << QString::fromStdString(restored.message);
              window.showPerGameSettingsError(
                "The previous game's runtime settings could not be restored: " +
                restored.message);
            }
            activePerGameIdentity = completedLoad.previousIdentity;
            activePerGameSettings = completedLoad.previousOverrides;
            activeEffectiveSettings = completedLoad.previousEffective;
            if (activePerGameIdentity) {
              window.setPerGameSettingsSession(
                activePerGameSettings, globalGameSettings());
            }
            window.setStateSessionReady(stateSessionAvailable);
            window.setSegaCdSession(
              event->disc.segaCd,
              discRegionName(event->disc.region),
              event->disc.path,
              event->disc.trayOpen,
              event->disc.discPresent);
            window.setGameRuntimeIdentity(
              diagnosticLoadedSystem, diagnosticLoadedRegion);
          }
          if (!previousGameRemainsLoaded && gameGeneration != 0U) {
            static_cast<void>(stateStorage.submit(
              genplusgx::StateStorageCommand::simple(
                genplusgx::StateStorageCommandType::deactivateGame,
                ++stateOperationId,
                gameGeneration)));
            gameGeneration = 0U;
            stateSessionAvailable = false;
            stateActivationOperation.reset();
            pendingState.reset();
            window.setStateOperationBusy(false);
            window.setStateSessionReady(false);
          }
          if (!previousGameRemainsLoaded) {
            const auto globalEffective =
              genplusgx::settings::resolvePerGameSettings(
                globalGameSettings(), {});
            const auto restored = applyEffectiveSettings(globalEffective);
            if (!restored) {
              qWarning().noquote() << QString::fromStdString(restored.message);
              window.showPerGameSettingsError(
                "Global runtime settings could not be restored after the "
                "failed load: " + restored.message);
            }
            activePerGameIdentity.reset();
            activePerGameSettings = {};
            activeEffectiveSettings = globalEffective;
            window.clearPerGameSettingsSession();
            activeCheatIdentity.reset();
            activeCheatSystem.reset();
            activeCheatConfiguration = {};
            pendingCheatMetadata.reset();
            pendingCheatOperation.reset();
          }
        }
      } else if (pendingUnload && event->operationId == *pendingUnload &&
                 event->command == genplusgx::EmulationCommandType::unloadGame) {
        pendingUnload.reset();
        if (event->succeeded()) {
          if (gameGeneration != 0U) {
            static_cast<void>(stateStorage.submit(
              genplusgx::StateStorageCommand::simple(
                genplusgx::StateStorageCommandType::deactivateGame,
                ++stateOperationId,
                gameGeneration)));
          }
          gameGeneration = 0U;
          stateSessionAvailable = false;
          stateActivationOperation.reset();
          pendingState.reset();
          const auto globalEffective =
            genplusgx::settings::resolvePerGameSettings(
              globalGameSettings(), {});
          const auto restored = applyEffectiveSettings(globalEffective);
          if (!restored) {
            qWarning().noquote() << QString::fromStdString(restored.message);
            window.showPerGameSettingsError(
              "Global runtime settings could not be restored after closing "
              "the game: " + restored.message);
          }
          activePerGameIdentity.reset();
          activePerGameSettings = {};
          activeEffectiveSettings = globalEffective;
          window.setNoGameLoaded();
          qInfo() << "Game unloaded.";
          window.clearCheatSession();
          activeCheatIdentity.reset();
          activeCheatSystem.reset();
          activeCheatConfiguration = {};
          pendingCheatMetadata.reset();
          pendingCheatOperation.reset();
          closingGamePath.clear();
        } else {
          window.setGameLoaded(closingGamePath);
          window.setSegaCdSession(
            event->disc.segaCd,
            discRegionName(event->disc.region),
            event->disc.path,
            event->disc.trayOpen,
            event->disc.discPresent);
          window.setGameRuntimeIdentity(
            diagnosticLoadedSystem, diagnosticLoadedRegion);
          window.showGameCloseError(event->message);
          qWarning().noquote() << QString::fromStdString(event->message);
        }
      }
      if (pendingDisc && event->operationId == pendingDisc->operationId &&
          (event->command == genplusgx::EmulationCommandType::changeDisc ||
           event->command == genplusgx::EmulationCommandType::setDiscEjected)) {
        const auto operation = pendingDisc->operation;
        pendingDisc.reset();
        window.setSegaCdSession(
          event->disc.segaCd,
          discRegionName(event->disc.region),
          event->disc.path,
          event->disc.trayOpen,
          event->disc.discPresent);
        if (event->succeeded()) {
          window.showDiscOperationSuccess(operation);
        } else {
          window.showDiscOperationError(operation, event->message);
        }
      }
      if (event->type == genplusgx::EmulationEventType::workerStopped) {
        const auto detail = event->message.empty()
          ? std::string{
              "The emulation worker stopped unexpectedly. The application "
              "will close to avoid exposing an unusable session."}
          : "The emulation worker stopped unexpectedly: " + event->message;
        reportRuntimeFailure(detail);
        window.setNoGameLoaded();
        window.setStateSessionReady(false);
        QCoreApplication::exit(1);
      } else if (event->type ==
                   genplusgx::EmulationEventType::commandFailed) {
        const bool hasWorkflowSpecificError = event->command &&
          (*event->command == genplusgx::EmulationCommandType::loadGame ||
           *event->command == genplusgx::EmulationCommandType::unloadGame ||
           *event->command == genplusgx::EmulationCommandType::setDiscEjected ||
           *event->command == genplusgx::EmulationCommandType::changeDisc ||
           *event->command == genplusgx::EmulationCommandType::cheats ||
           *event->command == genplusgx::EmulationCommandType::captureState ||
           *event->command == genplusgx::EmulationCommandType::restoreState);
        if (!hasWorkflowSpecificError) {
          reportRuntimeFailure(
            event->message.empty()
              ? "An emulation command failed without diagnostic detail."
              : event->message);
        }
      }
      if (!audioOutput.isInitialized()) {
        continue;
      }
      const bool audioShouldRun =
        event->workerState == genplusgx::EmulationWorkerState::running &&
        !event->fastForward;
      if (audioShouldRun &&
          audioOutput.isPaused()) {
        const auto resumed = audioOutput.resume();
        if (!resumed) {
          qWarning().noquote() << QString::fromStdString(resumed.message);
          if (!audioControlFailureReported) {
            audioControlFailureReported = true;
            window.showAudioOutputError(
              "Audio playback could not resume: " + resumed.message);
          }
        }
      } else if (!audioShouldRun && !audioOutput.isPaused()) {
        const auto paused = audioOutput.pause();
        if (!paused) {
          qWarning().noquote() << QString::fromStdString(paused.message);
          if (!audioControlFailureReported) {
            audioControlFailureReported = true;
            window.showAudioOutputError(
              "Audio playback could not pause: " + paused.message);
          }
        }
      }
    }
    while (const auto event = stateStorage.pollEvent()) {
      if (event->type == genplusgx::StateStorageEventType::serviceStarted) {
        qInfo() << "Save-state storage service started.";
        continue;
      }
      if (event->type == genplusgx::StateStorageEventType::sessionActivated &&
          event->gameGeneration == gameGeneration &&
          stateActivationOperation &&
          event->operationId == *stateActivationOperation) {
        stateActivationOperation.reset();
        stateSessionAvailable = true;
        window.setStateSlotViews(stateSlotViews(event->slotSummaries));
        window.setStateSessionReady(true);
        continue;
      }
      if (event->type == genplusgx::StateStorageEventType::operationFailed) {
        if (stateActivationOperation &&
            event->operationId == *stateActivationOperation &&
            event->gameGeneration == gameGeneration) {
          stateActivationOperation.reset();
          stateSessionAvailable = false;
          window.setStateSessionReady(false);
          window.showStateOperationError(
            genplusgx::ui::StateUiOperation::load,
            "Save states are unavailable: " + event->message);
        } else if (pendingState &&
                   event->operationId == pendingState->operationId &&
                   event->gameGeneration == pendingState->gameGeneration) {
          const auto operation = pendingState->operation;
          pendingState.reset();
          window.setStateOperationBusy(false);
          window.showStateOperationError(operation, event->message);
        } else if (event->operationId == 0U) {
          qWarning().noquote() << QString::fromStdString(event->message);
          stateSessionAvailable = false;
          stateActivationOperation.reset();
          pendingState.reset();
          window.setStateOperationBusy(false);
          window.setStateSessionReady(false);
          if (!stateServiceFailureReported) {
            stateServiceFailureReported = true;
            window.showStateOperationError(
              genplusgx::ui::StateUiOperation::load,
              "The save-state service is unavailable: " + event->message);
          }
        }
        continue;
      }
      if (!pendingState || event->operationId != pendingState->operationId ||
          event->gameGeneration != pendingState->gameGeneration ||
          event->gameGeneration != gameGeneration) {
        continue;
      }
      if (event->type == genplusgx::StateStorageEventType::slotLoaded &&
          pendingState->phase == PendingStatePhase::loading) {
        window.setStateSlotViews(stateSlotViews(event->slotSummaries));
        const auto restored = worker.submit(genplusgx::EmulationCommand::restore(
          event->operationId, event->rawPayload));
        if (!restored) {
          const auto operation = pendingState->operation;
          pendingState.reset();
          window.setStateOperationBusy(false);
          window.showStateOperationError(operation, restored.message);
        } else {
          pendingState->phase = PendingStatePhase::restoring;
        }
      } else if (event->type == genplusgx::StateStorageEventType::slotSaved &&
                 pendingState->phase == PendingStatePhase::saving) {
        const auto operation = pendingState->operation;
        const auto slot = pendingState->slot;
        window.setStateSlotViews(stateSlotViews(event->slotSummaries));
        pendingState.reset();
        window.setStateOperationBusy(false);
        window.showStateOperationSuccess(operation, slot);
        qInfo().noquote() << "Save state written: slot"
                          << static_cast<qulonglong>(slot);
      } else if (event->type == genplusgx::StateStorageEventType::slotDeleted &&
                 pendingState->phase == PendingStatePhase::deleting) {
        const auto operation = pendingState->operation;
        const auto slot = pendingState->slot;
        window.setStateSlotViews(stateSlotViews(event->slotSummaries));
        pendingState.reset();
        window.setStateOperationBusy(false);
        window.showStateOperationSuccess(operation, slot);
        qInfo().noquote() << "Save state deleted: slot"
                          << static_cast<qulonglong>(slot);
      }
    }
    while (auto event = screenshotService.pollEvent()) {
      using EventType = genplusgx::screenshots::ScreenshotEventType;
      if (event->type == EventType::serviceStarted) {
        qInfo() << "Screenshot service started.";
        continue;
      }
      if (event->type == EventType::serviceStopped) {
        if (!event->status) {
          qWarning().noquote() << QString::fromStdString(event->status.message);
        }
        if (pendingScreenshot) {
          pendingScreenshot.reset();
          window.showScreenshotError(
            event->status.message.empty()
              ? "The screenshot service stopped before saving the image."
              : event->status.message);
        }
        continue;
      }
      if (!pendingScreenshot || event->operationId != *pendingScreenshot) {
        continue;
      }
      pendingScreenshot.reset();
      if (event->succeeded()) {
        qInfo().noquote() << "Screenshot saved:"
                          << genplusgx::ui::pathToQString(event->path);
        window.showScreenshotSaved(event->path);
      } else {
        qWarning().noquote() << QString::fromStdString(event->status.message);
        window.showScreenshotError(event->status.message);
      }
    }
    while (auto event = metadataService.pollEvent()) {
      if (event->type ==
          genplusgx::library::GameMetadataEventType::serviceStarted) {
        qInfo() << "Game metadata service started.";
        continue;
      }
      if (event->type ==
          genplusgx::library::GameMetadataEventType::serviceStopped) {
        constexpr auto stoppedDetail =
          "The game metadata service stopped before completing the request.";
        if (pendingLoad && pendingLoad->phase == PendingLoadPhase::metadata) {
          const auto path = pendingLoad->path;
          const auto previous = pendingLoad->previousEffective;
          pendingLoad.reset();
          const auto restored = applyEffectiveSettings(previous);
          std::string detail{stoppedDetail};
          if (!restored) {
            detail += " The previous runtime settings also could not be "
              "restored: " + restored.message;
          }
          window.showGameLoadError(path, detail, false);
        }
        if (pendingCheatMetadata) {
          pendingCheatMetadata.reset();
          window.showCheatError(stoppedDetail);
        }
        if (pendingMetadata) {
          pendingMetadata.reset();
          window.setGameInformationBusy(false);
          window.showGameInformationError(stoppedDetail);
        }
        continue;
      }
      if (pendingLoad && pendingLoad->phase == PendingLoadPhase::metadata &&
          event->operationId == pendingLoad->metadataOperationId &&
          event->path == pendingLoad->path) {
        if (event->succeeded()) {
          pendingLoad->diagnosticGame = event->metadata.displayTitle();
          pendingLoad->diagnosticSystem = std::string{
            genplusgx::library::gameSystemName(event->metadata.system)};
          pendingLoad->diagnosticRegion = event->metadata.region;
          pendingLoad->identity = metadataIdentity(event->metadata);
          if (!pendingLoad->identity) {
            pendingLoad->warning =
              "The game metadata did not produce a safe settings identity; "
              "global settings were used.";
          } else {
            auto loaded = perGameSettingsStore.load(*pendingLoad->identity);
            if (!loaded.status) {
              pendingLoad->warning =
                "Stored per-game settings could not be loaded; global settings "
                "were used: " + loaded.status.message;
              qWarning().noquote()
                << QString::fromStdString(pendingLoad->warning);
            } else {
              pendingLoad->overrides = std::move(loaded.settings);
            }
          }
        } else {
          pendingLoad->warning =
            "Game metadata preflight failed; global settings were used: " +
            event->status.message;
          qWarning().noquote() << QString::fromStdString(pendingLoad->warning);
        }
        pendingLoad->effective = genplusgx::settings::resolvePerGameSettings(
          globalGameSettings(), pendingLoad->overrides);
        const auto inputProfileExists = std::ranges::any_of(
          inputConfiguration.profiles,
          [&pendingLoad](const auto& profile) {
            return profile.name == pendingLoad->effective.inputProfile;
          });
        if (!inputProfileExists) {
          if (!pendingLoad->warning.empty()) {
            pendingLoad->warning += ' ';
          }
          pendingLoad->warning +=
            "The selected input profile no longer exists; the global profile "
            "was used.";
          pendingLoad->effective.inputProfile =
            globalGameSettings().inputProfile;
        }
        const auto applied = applyEffectiveSettings(pendingLoad->effective);
        if (!applied) {
          const auto path = pendingLoad->path;
          const auto previous = pendingLoad->previousEffective;
          pendingLoad.reset();
          static_cast<void>(applyEffectiveSettings(previous));
          window.showGameLoadError(path, applied.message, false);
          continue;
        }
        pendingLoad->operationId = ++lifecycleOperationId;
        pendingLoad->phase = PendingLoadPhase::coreLoad;
        const auto submitted = worker.submit(genplusgx::EmulationCommand::load(
          pendingLoad->operationId, pendingLoad->path));
        if (!submitted) {
          const auto path = pendingLoad->path;
          const auto previous = pendingLoad->previousEffective;
          pendingLoad.reset();
          static_cast<void>(applyEffectiveSettings(previous));
          window.showGameLoadError(path, submitted.message, false);
        }
        continue;
      }
      if (pendingCheatMetadata &&
          event->operationId == pendingCheatMetadata->operationId &&
          event->path == pendingCheatMetadata->path) {
        const auto requestedPath = pendingCheatMetadata->path;
        const auto system = pendingCheatMetadata->system;
        pendingCheatMetadata.reset();
        const bool belongsToVisibleGame =
          !window.isGameLoading() && window.isGameLoaded() &&
          window.loadedGamePath() == requestedPath;
        if (!belongsToVisibleGame) {
          continue;
        }
        if (!event->succeeded()) {
          window.showCheatError(
            "Cheat metadata could not be read: " + event->status.message);
          continue;
        }
        auto titleSlug = genplusgx::sanitizeFilename(
          event->metadata.displayTitle());
        if (titleSlug.empty()) {
          titleSlug = "game";
        }
        const genplusgx::GameIdentity identity{
          .sha256 = event->metadata.sha256,
          .titleSlug = std::move(titleSlug),
        };
        if (!identity.valid()) {
          window.showCheatError(
            "The loaded game could not be assigned a safe cheat identity.");
          continue;
        }
        auto loaded = cheatStore.load(identity, system);
        if (!loaded.status) {
          qWarning().noquote() << QString::fromStdString(loaded.status.message);
          window.showCheatError(
            "Stored cheats could not be loaded; an empty list will be used: " +
            loaded.status.message);
          loaded.configuration = {};
        }
        std::vector<genplusgx::CoreCheatPatch> patches;
        const auto validated = genplusgx::cheats::validateCheatConfiguration(
          system, loaded.configuration, &patches);
        if (!validated) {
          window.showCheatError(validated.message);
          continue;
        }
        const auto operationId = ++cheatOperationId;
        const auto submitted = worker.submit(
          genplusgx::EmulationCommand::updateCheats(operationId, patches));
        if (!submitted) {
          window.showCheatError(submitted.message);
          continue;
        }
        activeCheatIdentity = identity;
        activeCheatSystem = system;
        activeCheatConfiguration = loaded.configuration;
        pendingCheatOperation = operationId;
        window.setCheatSession(system, std::move(loaded.configuration));
        continue;
      }
      if (!pendingMetadata ||
          event->operationId != pendingMetadata->operationId ||
          event->path != pendingMetadata->path) {
        continue;
      }
      const bool belongsToVisibleGame =
        !window.isGameLoading() && window.isGameLoaded() &&
        window.loadedGamePath() == event->path;
      pendingMetadata.reset();
      if (!belongsToVisibleGame) {
        window.setGameInformationBusy(false);
        continue;
      }
      if (event->succeeded()) {
        window.showGameInformation(event->metadata);
      } else {
        window.showGameInformationError(event->status.message);
      }
    }
    while (auto event = gameLibraryScanner.pollEvent()) {
      using EventType = genplusgx::library::GameLibraryScanEventType;
      switch (event->type) {
        case EventType::serviceStarted:
          qInfo() << "Game-library scanner started.";
          break;
        case EventType::scanStarted:
          window.showGameLibraryScanStarted(
            event->directoryId, event->directoryPath);
          break;
        case EventType::scanProgress:
          window.showGameLibraryScanProgress(
            event->directoryId, event->summary);
          break;
        case EventType::scanCompleted: {
          libraryScansInFlight.erase(event->directoryId);
          const auto refreshed = refreshGameLibrary();
          if (!refreshed) {
            window.showGameLibraryError(refreshed.message);
          }
          window.showGameLibraryScanCompleted(
            event->directoryId, event->summary);
          if (libraryScansInFlight.empty()) {
            flushDeferredLibraryLaunches();
          }
          break;
        }
        case EventType::scanFailed:
          libraryScansInFlight.erase(event->directoryId);
          qWarning().noquote() << QString::fromStdString(event->status.message);
          window.showGameLibraryScanFailed(
            event->directoryId, event->status.message);
          if (libraryScansInFlight.empty()) {
            flushDeferredLibraryLaunches();
          }
          break;
        case EventType::serviceStopped:
          libraryScansInFlight.clear();
          deferredLibraryLaunches.clear();
          window.setGameLibraryAvailable(false,
            event->status.message.empty()
              ? "The background game-library scanner stopped unexpectedly."
              : event->status.message);
          if (!event->status) {
            qWarning().noquote()
              << QString::fromStdString(event->status.message);
            window.showGameLibraryError(event->status.message);
          }
          break;
      }
    }
  });
  eventPump.start();
  window.show();
  qInfo().noquote() << "Renderer selected:"
                    << (window.displayWidget()->usesAcceleratedRenderer()
                          ? "OpenGL texture renderer"
                          : "Qt software painter");
  if (!startupIssues.empty()) {
    QTimer::singleShot(
      0,
      &window,
      [&window, issues = std::move(startupIssues)]() mutable {
        window.showStartupIssues(std::move(issues));
      });
  }
  if (commandLine.fullscreen) {
    window.setFullscreen(true);
  }
  if (commandLine.gamePath) {
    const auto startupGame = genplusgx::ui::pathFromQString(*commandLine.gamePath);
    QTimer::singleShot(0, &window, [&window, startupGame] {
      static_cast<void>(window.requestGameLoad(startupGame));
    });
  }
  if (automatedQuitMilliseconds) {
    qInfo().noquote() << "Automated startup smoke test entered the event loop.";
    QTimer::singleShot(
      *automatedQuitMilliseconds, &application, &QCoreApplication::quit);
  }
  const int result = application.exec();
  eventPump.stop();
  window.displayWidget()->setRendererFailureSink({});
  keyboardInput.setSnapshotSink({});
  keyboardInput.detach();
  controllerInput.setSnapshotSink({});
  controllerInput.setConnectionSink({});
  controllerInput.setCaptureSink({});
  window.setInputConfigurationSink({});
  window.setControllerAssignmentSink({});
  window.setGameLoadSink({});
  window.setGameCloseSink({});
  window.setClearRecentGamesSink({});
  window.setStateOperationSink({});
  window.setGameInformationRequestSink({});
  window.setGameLibraryActions({});
  window.setScreenshotSink({});
  window.setScreenshotSettingsSink({});
  window.setCheatConfigurationSink({});
  window.setPerGameSettingsSink({});
  window.setAppearanceSettingsSink({});
  window.setDiagnosticsSnapshotProvider({});
  window.setVideoSettingsSink({});
  window.setAudioSettingsSink({});
  window.setSystemSettingsSink({});
  window.setBiosConfigurationSink({});
  inputAggregator.setSnapshotSink({});
  genplusgx::app::ShutdownReport shutdownReport{result};
  const auto recordCleanup = [&shutdownReport](
                               const char* service, const auto& status) {
    if (status) {
      return;
    }
    qWarning().noquote() << service << '-'
                         << QString::fromStdString(status.message);
    shutdownReport.addFailure(service, status.message);
  };
  const auto workerStopped = worker.stop();
  recordCleanup("Emulation worker", workerStopped);
  const auto audioOutputStopped = audioOutput.shutdown();
  recordCleanup("Audio output", audioOutputStopped);
  const auto controllerInputStopped = controllerInput.shutdown();
  recordCleanup("Controller input", controllerInputStopped);
  const auto stateStorageStopped = stateStorage.stop();
  recordCleanup("Save-state storage", stateStorageStopped);
  const auto metadataServiceStopped = metadataService.stop();
  recordCleanup("Game metadata service", metadataServiceStopped);
  const auto screenshotServiceStopped = screenshotService.stop();
  recordCleanup("Screenshot service", screenshotServiceStopped);
  const auto gameLibraryScannerStopped = gameLibraryScanner.stop();
  recordCleanup("Game-library scanner", gameLibraryScannerStopped);
  window.displayWidget()->setFrameExchange({});
  if (shutdownReport.succeeded()) {
    qInfo() << "Application shutdown complete.";
  } else {
    qCritical().noquote() << "Application shutdown incomplete:"
                           << QString::fromStdString(shutdownReport.summary());
  }
  frontendLogger.shutdown();
  return shutdownReport.exitCode();
}
