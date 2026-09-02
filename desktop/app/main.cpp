#include "genplusgx/ui/main_window.h"
#include "genplusgx/app/command_line.h"
#include "genplusgx/app/platform_bootstrap.h"
#include "genplusgx/app/shutdown_report.h"
#include "genplusgx/version.h"
#include "genplusgx/audio_output.h"
#include "genplusgx/achievements/achievement_credentials.h"
#include "genplusgx/achievements/achievement_network_client.h"
#include "genplusgx/achievements/achievement_settings.h"
#include "genplusgx/backup_store.h"
#include "genplusgx/bounded_queue.h"
#include "genplusgx/cheats/cheat_manager.h"
#include "genplusgx/cloud/cloud_credentials.h"
#include "genplusgx/cloud/cloud_settings.h"
#include "genplusgx/cloud/cloud_sync.h"
#include "genplusgx/capture/recording_service.h"
#include "genplusgx/diagnostics/diagnostics.h"
#include "genplusgx/emulation_worker.h"
#include "genplusgx/input/controller_input.h"
#include "genplusgx/input/input_aggregator.h"
#include "genplusgx/input/input_profile.h"
#include "genplusgx/input/keyboard_input.h"
#include "genplusgx/library/game_library_database.h"
#include "genplusgx/library/game_library_scanner.h"
#include "genplusgx/library/game_metadata_service.h"
#include "genplusgx/library/online_metadata_service.h"
#include "genplusgx/library/online_metadata_settings.h"
#include "genplusgx/localization/localization.h"
#include "genplusgx/netplay/netplay_bridge.h"
#include "genplusgx/netplay/netplay_session.h"
#include "genplusgx/persistence.h"
#include "genplusgx/platform/bios_manager.h"
#include "genplusgx/platform/physical_media.h"
#include "genplusgx/recent_games.h"
#include "genplusgx/screenshots/screenshot_service.h"
#include "genplusgx/state_storage_service.h"
#include "genplusgx/timing/frame_rate_sampler.h"
#include "genplusgx/settings/appearance_settings.h"
#include "genplusgx/settings/screenshot_settings.h"
#include "genplusgx/settings/video_settings.h"
#include "genplusgx/settings/audio_settings.h"
#include "genplusgx/settings/per_game_settings.h"
#include "genplusgx/settings/rewind_settings.h"
#include "genplusgx/settings/run_ahead_settings.h"
#include "genplusgx/settings/session_settings.h"
#include "genplusgx/settings/speed_settings.h"
#include "genplusgx/settings/system_settings.h"
#include "genplusgx/ui/dialog_service.h"
#include "genplusgx/ui/theme_controller.h"
#include "genplusgx/video/display_widget.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDebug>
#include <QLocale>
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
#include <system_error>
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
    view.schemaVersion = summary.metadata.schemaVersion;
    view.timestamp = summary.metadata.timestamp;
    view.emulatedFrameNumber = summary.metadata.emulatedFrameNumber;
    view.payloadBytes = summary.metadata.payloadBytes;
    view.name = summary.metadata.name;
    view.thumbnailPng = summary.metadata.thumbnailPng;
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

std::string presentationSyncDescription(
  const genplusgx::video::DisplayPresentationMetrics& metrics)
{
  using Mode = genplusgx::video::PresentationSyncMode;
  std::string result;
  switch (metrics.requested.sync) {
    case Mode::disabled: result = "Off"; break;
    case Mode::synchronized: result = "On"; break;
    case Mode::adaptive: result = "Adaptive"; break;
  }
  if (!metrics.accelerated) {
    return result + " requested; software renderer active";
  }
  if (!metrics.rendererInitialized) {
    return result + " requested; renderer initialization pending";
  }
  result += " requested; effective swap interval " +
    std::to_string(metrics.effectiveSwapInterval);
  if (!metrics.swapIntervalHonored) {
    result += " (host substituted)";
  }
  return result;
}

std::string presentationBufferingDescription(
  const genplusgx::video::DisplayPresentationMetrics& metrics)
{
  using Mode = genplusgx::video::PresentationBufferingMode;
  const auto name = [](Mode mode) {
    return mode == Mode::tripleBuffer ? "Triple buffer" : "Double buffer";
  };
  std::string result = std::string{name(metrics.requested.buffering)} +
    " requested";
  if (metrics.accelerated && metrics.rendererInitialized) {
    result += "; " + std::string{name(metrics.effectiveBuffering)} +
      " effective";
    if (!metrics.bufferingHonored) {
      result += " (host substituted)";
    }
  } else if (!metrics.accelerated) {
    result += "; software renderer active";
  } else {
    result += "; renderer initialization pending";
  }
  return result;
}

std::string netplaySettingsFingerprint(
  const genplusgx::settings::EffectiveGameSettings& settings,
  const genplusgx::input::InputConfiguration& inputConfiguration,
  const genplusgx::platform::BiosSnapshot& bios,
  const genplusgx::cheats::CheatConfiguration& cheats)
{
  QByteArray canonical;
  QDataStream stream{&canonical, QIODevice::WriteOnly};
  stream.setVersion(QDataStream::Qt_6_8);
  stream.setByteOrder(QDataStream::BigEndian);
  const auto& video = settings.video.core;
  const auto& audio = settings.audio.core;
  const auto& system = settings.system;
  stream << static_cast<quint8>(video.overscan)
         << static_cast<quint8>(video.ntscFilter)
         << static_cast<quint8>(video.interlacedRender)
         << video.gameGearExtendedScreen
         << static_cast<quint8>(audio.output)
         << static_cast<quint8>(audio.filter)
         << static_cast<quint8>(audio.ym2612Core)
         << static_cast<quint8>(audio.ym2413Mode)
         << static_cast<quint8>(audio.ym2413Core)
         << audio.psgLevelPercent << audio.fmLevelPercent
         << audio.cddaLevelPercent << audio.pcmLevelPercent
         << audio.lowPassPercent << audio.equalizerLowPercent
         << audio.equalizerMidPercent << audio.equalizerHighPercent
         << audio.highQualityFm << audio.highQualityPsg
         << static_cast<quint8>(system.hardware)
         << static_cast<quint8>(system.region)
         << static_cast<quint8>(system.videoStandard)
         << static_cast<quint8>(system.masterClock)
         << system.emulateIllegalAccessLockups << system.enableAddressErrors;

  const auto profile = std::ranges::find_if(inputConfiguration.profiles,
    [&settings](const auto& candidate) {
      return candidate.name == settings.inputProfile;
    });
  if (profile != inputConfiguration.profiles.end()) {
    const auto coreInput = genplusgx::input::coreInputSettings(*profile);
    for (const auto device : coreInput.devices) {
      stream << static_cast<quint8>(device);
    }
  }
  for (const auto& validation : bios.validation) {
    stream << QString::fromStdString(
      validation.valid() ? validation.sha256 : std::string{});
  }
  for (const auto& cheat : cheats.entries) {
    if (cheat.enabled) {
      stream << QString::fromStdString(cheat.code);
    }
  }
  return QCryptographicHash::hash(canonical, QCryptographicHash::Sha256)
    .toHex().toStdString();
}

} // namespace

int main(int argc, char* argv[])
{
  if (argc > 0 && argv[0] != nullptr) {
    static_cast<void>(genplusgx::app::configureBundledLinuxQtPlatform(argv[0]));
  }
  genplusgx::ui::configureHighDpiPolicy();
  genplusgx::video::configureOpenGLSurfaceFormat();
  QApplication application(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("Genesis Plus GX GUI"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("genesisplusgx.org"));
  QCoreApplication::setApplicationName(QString::fromLatin1(GENPLUSGX_APP_NAME));
  QCoreApplication::setApplicationVersion(QString::fromLatin1(GENPLUSGX_VERSION));
  QApplication::setDesktopFileName(QString::fromLatin1(GENPLUSGX_APP_ID));
  genplusgx::localization::TranslationManager translationManager{application};
  static_cast<void>(translationManager.apply(
    genplusgx::localization::systemLanguage));
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
  bool automatedQuitWhenGameReady = false;
  genplusgx::ApplicationPaths applicationPaths;
  if (automatedTestMode) {
    const auto testRoot = qEnvironmentVariable("GENPLUSGX_TEST_DATA_ROOT");
    const auto portableExecutableOverride =
      qEnvironmentVariable("GENPLUSGX_TEST_PORTABLE_EXECUTABLE");
    const auto readyQuitMode =
      qEnvironmentVariable("GENPLUSGX_TEST_QUIT_WHEN_GAME_READY");
    bool quitDelayValid = false;
    const int quitDelay = qEnvironmentVariableIntValue(
      "GENPLUSGX_TEST_AUTO_QUIT_MS", &quitDelayValid);
    const auto root = genplusgx::ui::pathFromQString(testRoot);
    if (testRoot.isEmpty() || !root.is_absolute() || !quitDelayValid ||
        (!readyQuitMode.isEmpty() &&
         readyQuitMode != QStringLiteral("1")) ||
        quitDelay < 1 || quitDelay > 10'000 ||
        (!commandLine.portable && !portableExecutableOverride.isEmpty())) {
      QTextStream{stderr}
        << "Invalid automated startup-test environment. The data root must be "
           "absolute, the quit delay must be between 1 and 10000 ms, and the "
           "optional ready-quit mode must be 1. A portable executable override "
           "is valid only with --portable.\n";
      return 2;
    }
    if (commandLine.portable) {
      const auto executablePath = portableExecutableOverride.isEmpty()
        ? genplusgx::ui::pathFromQString(
            QCoreApplication::applicationFilePath())
        : genplusgx::ui::pathFromQString(portableExecutableOverride);
      applicationPaths =
        genplusgx::ApplicationPaths::fromPortableExecutable(executablePath);
      if (applicationPaths.root() != root.lexically_normal()) {
        QTextStream{stderr}
          << "Invalid automated portable-mode root. The expected data root does "
             "not match the executable-relative portable-data directory.\n";
        return 2;
      }
    } else {
      applicationPaths = genplusgx::ApplicationPaths{root};
    }
    automatedQuitMilliseconds = quitDelay;
    automatedQuitWhenGameReady = !readyQuitMode.isEmpty();
  } else if (commandLine.portable) {
    applicationPaths = genplusgx::ApplicationPaths::fromPortableExecutable(
      genplusgx::ui::pathFromQString(QCoreApplication::applicationFilePath()));
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
    if (automatedTestMode) {
      QTextStream{stderr}
        << QString::fromStdString(pathsInitialized.message) << '\n';
      return 2;
    }
    qWarning().noquote() << QString::fromStdString(pathsInitialized.message);
    if (commandLine.portable) {
      QMessageBox::critical(
        nullptr,
        QObject::tr("Portable Mode Unavailable"),
        QObject::tr(
          "The portable-data directory beside this application could not be "
          "created or opened. Move the application to a writable folder or "
          "start it without --portable.\n\n%1")
          .arg(QString::fromStdString(pathsInitialized.message)));
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
  const auto applicationDataMode =
    genplusgx::applicationDataModeName(applicationPaths.mode());
  qInfo().noquote() << "Application data mode:"
                    << QString::fromLatin1(applicationDataMode.data(),
                         static_cast<qsizetype>(applicationDataMode.size()));
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
  genplusgx::achievements::SettingsStore achievementSettingsStore{
    applicationPaths.configDirectory() / "achievements.json"};
  auto loadedAchievementSettings = achievementSettingsStore.load();
  if (!loadedAchievementSettings.status) {
    qWarning().noquote() << QString::fromStdString(
      loadedAchievementSettings.status.message);
    recordStartupIssue(
      "RetroAchievements settings", loadedAchievementSettings.status.message);
  }
  auto achievementSettings = loadedAchievementSettings.settings;
  if (loadedAchievementSettings.migrated) {
    const auto migrated = achievementSettingsStore.save(achievementSettings);
    if (!migrated) {
      recordStartupIssue("RetroAchievements settings migration", migrated.message);
    }
  }
  genplusgx::cloud::SettingsStore cloudSettingsStore{
    applicationPaths.configDirectory() / "cloud-sync.json"};
  auto loadedCloudSettings = cloudSettingsStore.load();
  if (!loadedCloudSettings.status) {
    qWarning().noquote() << QString::fromStdString(
      loadedCloudSettings.status.message);
    recordStartupIssue(
      "Cloud synchronization settings", loadedCloudSettings.status.message);
  }
  auto cloudSettings = loadedCloudSettings.settings;
  genplusgx::library::OnlineMetadataSettingsStore onlineMetadataSettingsStore{
    applicationPaths.configDirectory() / "online-metadata.json"};
  auto loadedOnlineMetadataSettings = onlineMetadataSettingsStore.load();
  if (!loadedOnlineMetadataSettings.status) {
    qWarning().noquote() << QString::fromStdString(
      loadedOnlineMetadataSettings.status.message);
    recordStartupIssue(
      "Online metadata settings", loadedOnlineMetadataSettings.status.message);
  }
  auto onlineMetadataSettings = loadedOnlineMetadataSettings.settings;
  const auto translationApplied = translationManager.apply(
    appearanceSettings.language);
  if (!translationApplied) {
    qWarning().noquote() << QString::fromStdString(translationApplied.message);
    recordStartupIssue("Interface localization", translationApplied.message);
  }
  application.setLayoutDirection(QLocale{}.textDirection());
  qInfo().noquote()
    << "Interface language: requested"
    << QString::fromStdString(translationManager.requestedLanguage())
    << "effective"
    << QString::fromStdString(translationManager.effectiveLanguage())
    << "English fallback"
    << translationManager.usedEnglishFallback();
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
  // The OpenGL profile was established before QApplication for context
  // sharing. Refresh the swap request before the first window/context exists
  // so persisted presentation policy also reaches Qt's top-level compositor.
  genplusgx::video::configureOpenGLSurfaceFormat(videoSettings.presentation);

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
  genplusgx::settings::RewindSettingsStore rewindSettingsStore{
    applicationPaths.configDirectory() / "rewind-settings.json"};
  auto loadedRewindSettings = rewindSettingsStore.load();
  if (!loadedRewindSettings.status) {
    qWarning().noquote() << QString::fromStdString(
      loadedRewindSettings.status.message);
    recordStartupIssue("Rewind settings", loadedRewindSettings.status.message);
  }
  auto rewindSettings = loadedRewindSettings.settings;
  genplusgx::settings::RunAheadSettingsStore runAheadSettingsStore{
    applicationPaths.configDirectory() / "run-ahead-settings.json"};
  auto loadedRunAheadSettings = runAheadSettingsStore.load();
  if (!loadedRunAheadSettings.status) {
    qWarning().noquote() << QString::fromStdString(
      loadedRunAheadSettings.status.message);
    recordStartupIssue(
      "Run-ahead settings", loadedRunAheadSettings.status.message);
  }
  auto runAheadSettings = loadedRunAheadSettings.settings;
  genplusgx::settings::SpeedSettingsStore speedSettingsStore{
    applicationPaths.configDirectory() / "speed-settings.json"};
  auto loadedSpeedSettings = speedSettingsStore.load();
  if (!loadedSpeedSettings.status) {
    qWarning().noquote() << QString::fromStdString(
      loadedSpeedSettings.status.message);
    recordStartupIssue("Emulation speed settings",
      loadedSpeedSettings.status.message);
  }
  auto speedSettings = loadedSpeedSettings.settings;
  genplusgx::settings::SessionSettingsStore sessionSettingsStore{
    applicationPaths.configDirectory() / "session-settings.json"};
  auto loadedSessionSettings = sessionSettingsStore.load();
  if (!loadedSessionSettings.status) {
    qWarning().noquote() << QString::fromStdString(
      loadedSessionSettings.status.message);
    recordStartupIssue("Session settings", loadedSessionSettings.status.message);
  }
  auto sessionSettings = loadedSessionSettings.settings;
  if (loadedSessionSettings.status && loadedSessionSettings.migrated) {
    const auto migrated = sessionSettingsStore.save(sessionSettings);
    if (!migrated) {
      qWarning().noquote() << QString::fromStdString(migrated.message);
      recordStartupIssue("Session settings migration", migrated.message);
    }
  }
  if (achievementSettings.enabled && achievementSettings.hardcore &&
      sessionSettings.lastGamePath) {
    sessionSettings.lastGamePath.reset();
    sessionSettings.lastPatchPath.reset();
    const auto cleared = sessionSettingsStore.save(sessionSettings);
    if (!cleared) {
      recordStartupIssue("Hardcore session checkpoint cleanup", cleared.message);
    }
  }
  std::optional<std::filesystem::path> automaticResumePath;
  std::optional<std::filesystem::path> automaticResumePatchPath;
  if (!commandLine.gamePath && sessionSettings.resumeOnLaunch &&
      sessionSettings.lastGamePath) {
    std::error_code pathError;
    if (std::filesystem::is_regular_file(
          *sessionSettings.lastGamePath, pathError) && !pathError) {
      if (sessionSettings.lastPatchPath &&
          (!std::filesystem::is_regular_file(
             *sessionSettings.lastPatchPath, pathError) || pathError)) {
        sessionSettings.lastGamePath.reset();
        sessionSettings.lastPatchPath.reset();
      } else {
        automaticResumePath = *sessionSettings.lastGamePath;
        automaticResumePatchPath = sessionSettings.lastPatchPath;
      }
    } else {
      sessionSettings.lastGamePath.reset();
      sessionSettings.lastPatchPath.reset();
    }
    if (!automaticResumePath) {
      const auto cleared = sessionSettingsStore.save(sessionSettings);
      if (!cleared) {
        qWarning().noquote() << QString::fromStdString(cleared.message);
        recordStartupIssue("Session resume", cleared.message);
      }
      qWarning() << "The previous session game is unavailable; automatic resume was cleared.";
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
  genplusgx::library::OnlineMetadataService onlineMetadataService;
  const auto onlineMetadataServiceStarted = onlineMetadataService.start();
  if (!onlineMetadataServiceStarted && onlineMetadataSettings.enabled) {
    qWarning().noquote() << QString::fromStdString(
      onlineMetadataServiceStarted.message);
    recordStartupIssue(
      "Online metadata service", onlineMetadataServiceStarted.message);
  }
  const auto physicalMediaCache =
    applicationPaths.cacheDirectory() / "physical-media";
  genplusgx::platform::PhysicalMediaService physicalMediaService{
    genplusgx::platform::createNativePhysicalMediaBackend(),
    physicalMediaCache};
  const auto physicalMediaServiceStarted = physicalMediaService.start();
  if (!physicalMediaServiceStarted) {
    qWarning().noquote() << QString::fromStdString(
      physicalMediaServiceStarted.message);
    recordStartupIssue(
      "Physical optical media", physicalMediaServiceStarted.message);
  }
  genplusgx::screenshots::ScreenshotService screenshotService;
  const auto screenshotServiceStarted = screenshotService.start();
  if (!screenshotServiceStarted) {
    qWarning().noquote() << QString::fromStdString(
      screenshotServiceStarted.message);
    recordStartupIssue("Screenshot service", screenshotServiceStarted.message);
  }
  genplusgx::cloud::SyncService cloudSyncService;
#if defined(GENPLUSGX_HAVE_CLOUD_SYNC)
  const auto cloudSyncServiceStarted = cloudSyncService.start();
#else
  const auto cloudSyncServiceStarted = genplusgx::cloud::Status{
    genplusgx::cloud::Error::notRunning,
    "This build does not include cloud synchronization support."};
#endif
  if (!cloudSyncServiceStarted && cloudSettings.enabled) {
    qWarning().noquote() << QString::fromStdString(
      cloudSyncServiceStarted.message);
    recordStartupIssue(
      "Cloud synchronization service", cloudSyncServiceStarted.message);
  }
  auto recordingService =
    std::make_shared<genplusgx::capture::RecordingService>();
  const auto recordingServiceStarted = recordingService->start();
  if (!recordingServiceStarted) {
    qWarning().noquote() << QString::fromStdString(
      recordingServiceStarted.message);
    recordStartupIssue("Lossless recording service",
      recordingServiceStarted.message);
  }
  auto netplayBridge =
    std::make_shared<genplusgx::netplay::NetplayBridge>();
  auto achievementBridge =
    std::make_shared<genplusgx::achievements::ServerBridge>();
  genplusgx::achievements::NetworkClient achievementNetwork{
    achievementBridge,
    std::string{GENPLUSGX_APP_NAME} + "/" + GENPLUSGX_VERSION};
  genplusgx::achievements::CredentialStore achievementCredentials;
  genplusgx::cloud::CredentialStore cloudCredentials;
  genplusgx::EmulationWorker worker{
    64U,
    64U,
    audioOutput.config().sampleRate,
    videoFrames,
    audioOutput.ringBuffer(),
    backupStore,
    recordingServiceStarted ? recordingService : nullptr,
    netplayBridge,
    achievementBridge};
  const auto workerStarted = worker.start();
  if (!workerStarted) {
    qCritical().noquote() << QString::fromStdString(workerStarted.message);
    static_cast<void>(metadataService.stop());
    static_cast<void>(onlineMetadataService.stop());
    static_cast<void>(physicalMediaService.stop());
    static_cast<void>(stateStorage.stop());
    static_cast<void>(screenshotService.stop());
    static_cast<void>(cloudSyncService.stop());
    static_cast<void>(recordingService->stop());
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
  std::uint64_t achievementOperationId = 845'000U;
  const auto initialAchievementSettings = worker.submit(
    genplusgx::EmulationCommand::updateAchievementSettings(
      ++achievementOperationId, achievementSettings));
  if (!initialAchievementSettings) {
    recordStartupIssue(
      "RetroAchievements runtime", initialAchievementSettings.message);
  }
  std::uint64_t rewindSettingsOperationId = 850'000U;
  const auto initialRewindSettings = worker.submit(
    genplusgx::EmulationCommand::updateRewindSettings(
      ++rewindSettingsOperationId, rewindSettings));
  if (!initialRewindSettings) {
    qWarning().noquote() << QString::fromStdString(initialRewindSettings.message);
    recordStartupIssue("Rewind runtime settings", initialRewindSettings.message);
  }
  std::uint64_t runAheadSettingsOperationId = 855'000U;
  const auto initialRunAheadSettings = worker.submit(
    genplusgx::EmulationCommand::updateRunAheadSettings(
      ++runAheadSettingsOperationId, runAheadSettings));
  if (!initialRunAheadSettings) {
    qWarning().noquote() << QString::fromStdString(
      initialRunAheadSettings.message);
    recordStartupIssue(
      "Run-ahead runtime settings", initialRunAheadSettings.message);
  }
  std::uint64_t speedSettingsOperationId = 860'000U;
  const auto initialSpeedSettings = worker.submit(
    genplusgx::EmulationCommand::updateSpeedSettings(
      ++speedSettingsOperationId, speedSettings));
  if (!initialSpeedSettings) {
    qWarning().noquote() << QString::fromStdString(initialSpeedSettings.message);
    recordStartupIssue(
      "Emulation speed runtime settings", initialSpeedSettings.message);
  }

  genplusgx::ui::MainWindow window;
  genplusgx::netplay::NetplaySession netplaySession{&window};
  window.setArchiveCacheDirectory(
    applicationPaths.cacheDirectory() / "archives");
  window.setPatchCacheDirectory(
    applicationPaths.cacheDirectory() / "patches");
  window.setPhysicalMediaSupported(
    genplusgx::platform::nativePhysicalMediaSupported() &&
    physicalMediaServiceStarted.ok());
  window.setOnlineMetadataSettings(onlineMetadataSettings);
  window.setOnlineMetadataSettingsSink(
    [&onlineMetadataSettings, &onlineMetadataSettingsStore, &window](
      const genplusgx::library::OnlineMetadataSettings& settings) {
      const auto saved = onlineMetadataSettingsStore.save(settings);
      if (!saved) {
        return saved;
      }
      onlineMetadataSettings = settings;
      window.setOnlineMetadataSettings(settings);
      return genplusgx::PersistenceStatus{};
    });
  window.displayWidget()->setRendererFailureSink(
    [&window](std::string detail) {
      window.showStartupIssues({"Video renderer: " + std::move(detail)});
    });
  window.setAchievementSettings(achievementSettings);
  const auto restoreAchievementSession =
    [&achievementCredentials, &achievementOperationId,
     &achievementSettings, &window, &worker](const std::string& username) {
      achievementCredentials.readToken(username,
        [&achievementOperationId, &achievementSettings, &window, &worker,
         username](genplusgx::achievements::CredentialResult result) mutable {
          if (!achievementSettings.enabled ||
              achievementSettings.username != username) {
            std::fill(result.token.begin(), result.token.end(), '\0');
            return;
          }
          if (!result.succeeded) {
            window.showAchievementError(result.detail);
            return;
          }
          if (!result.found) {
            return;
          }
          const auto submitted = worker.submit(
            genplusgx::EmulationCommand::achievementTokenLogin(
              ++achievementOperationId, std::move(username),
              std::move(result.token)));
          if (!submitted) {
            window.showAchievementError(submitted.message);
          }
        });
    };
  window.setAchievementSettingsSink(
    [&achievementOperationId, &achievementSettings,
     &achievementSettingsStore, &restoreAchievementSession, &window, &worker](
      const genplusgx::achievements::Settings& settings) {
      const auto previous = achievementSettings;
      const auto saved = achievementSettingsStore.save(settings);
      if (!saved) {
        return saved;
      }
      const auto submitted = worker.submit(
        genplusgx::EmulationCommand::updateAchievementSettings(
          ++achievementOperationId, settings));
      if (!submitted) {
        const auto rolledBack = achievementSettingsStore.save(previous);
        std::string detail = submitted.message;
        if (!rolledBack) {
          detail +=
            " The previous achievement settings also could not be restored: " +
            rolledBack.message;
        }
        return genplusgx::PersistenceStatus{
          genplusgx::PersistenceError::invalidData, std::move(detail)};
      }
      achievementSettings = settings;
      window.setAchievementSettings(settings);
      if (settings.enabled && !settings.username.empty() &&
          (!previous.enabled || previous.username != settings.username)) {
        restoreAchievementSession(settings.username);
      }
      return genplusgx::PersistenceStatus{};
    });
  window.setAchievementLoginSink(
    [&achievementOperationId, &worker](std::string username,
      std::string password) {
      const auto submitted = worker.submit(
        genplusgx::EmulationCommand::achievementPasswordLogin(
          ++achievementOperationId, std::move(username), std::move(password)));
      if (!submitted) {
        qWarning().noquote() << "RetroAchievements sign-in command rejected:"
                             << QString::fromStdString(submitted.message);
      }
    });
  window.setAchievementLogoutSink(
    [&achievementCredentials, &achievementOperationId,
     &achievementSettings, &window, &worker] {
      if (!achievementSettings.username.empty()) {
        achievementCredentials.deleteToken(achievementSettings.username,
          [&window](genplusgx::achievements::CredentialResult result) {
            if (!result.succeeded) {
              window.showAchievementError(result.detail);
            }
          });
      }
      const auto submitted = worker.submit(genplusgx::EmulationCommand::simple(
        genplusgx::EmulationCommandType::achievementLogout,
        ++achievementOperationId));
      if (!submitted) {
        window.showAchievementError(submitted.message);
      }
    });
  if (achievementSettings.enabled &&
      !achievementSettings.username.empty()) {
    restoreAchievementSession(achievementSettings.username);
  }
  window.setCloudSettings(cloudSettings);
  std::uint64_t cloudOperationId = 875'000U;
  std::optional<std::uint64_t> pendingCloudOperation;
  bool cloudOperationAutomatic = false;
  bool cloudCredentialReadPending = false;
  bool startupCloudSyncDeferred = false;
  const auto submitCloudSync =
    [&applicationPaths, &cloudOperationAutomatic, &cloudSettings,
     &cloudSyncService, &pendingCloudOperation, &cloudOperationId,
     &window](std::string password, bool automatic) {
      if (pendingCloudOperation) {
        std::fill(password.begin(), password.end(), '\0');
        if (!automatic) {
          window.showCloudSyncStatus(
            "Another cloud synchronization is already in progress.");
        }
        return;
      }
      if (window.isGameLoaded() || window.isGameLoading()) {
        std::fill(password.begin(), password.end(), '\0');
        window.setCloudSyncBusy(false);
        if (!automatic) {
          window.showCloudSyncError(
            "Close the active game before synchronizing saves and states.");
        }
        return;
      }
      const auto operationId = ++cloudOperationId;
      const auto submitted = cloudSyncService.request(operationId,
        applicationPaths, cloudSettings, std::move(password));
      if (!submitted) {
        window.setCloudSyncBusy(false);
        if (automatic) {
          qWarning().noquote() << "Automatic cloud synchronization was not started:"
                               << QString::fromStdString(submitted.message);
          window.showCloudSyncStatus(
            "Automatic cloud synchronization was not started: " +
            submitted.message);
        } else {
          window.showCloudSyncError(submitted.message);
        }
        return;
      }
      pendingCloudOperation = operationId;
      cloudOperationAutomatic = automatic;
      window.setCloudSyncBusy(true);
      qInfo() << (automatic ? "Automatic" : "Manual")
              << "cloud synchronization started.";
    };
  const auto requestCloudSync =
    [&cloudCredentialReadPending, &cloudCredentials, &cloudOperationAutomatic,
     &cloudSettings, &pendingCloudOperation, &submitCloudSync,
     &window](std::string password, bool automatic) {
      if (cloudCredentialReadPending || pendingCloudOperation) {
        std::fill(password.begin(), password.end(), '\0');
        if (!automatic) {
          window.showCloudSyncStatus(
            "Another cloud synchronization is already in progress.");
        }
        return;
      }
      if (!cloudSettings.enabled) {
        std::fill(password.begin(), password.end(), '\0');
        if (!automatic) {
          window.showCloudSyncError("Cloud synchronization is disabled.");
        }
        return;
      }
      if (!password.empty()) {
        submitCloudSync(std::move(password), automatic);
        return;
      }
      const auto endpoint = cloudSettings.endpoint;
      const auto username = cloudSettings.username;
      cloudCredentialReadPending = true;
      cloudOperationAutomatic = automatic;
      window.setCloudSyncBusy(true);
      cloudCredentials.readPassword(endpoint, username,
        [&cloudCredentialReadPending, &cloudSettings, &submitCloudSync,
         &window, automatic, endpoint,
         username](genplusgx::cloud::CredentialResult result) mutable {
          cloudCredentialReadPending = false;
          if (!cloudSettings.enabled || cloudSettings.endpoint != endpoint ||
              cloudSettings.username != username) {
            std::fill(result.password.begin(), result.password.end(), '\0');
            window.setCloudSyncBusy(false);
            return;
          }
          if (!result.succeeded || !result.found) {
            window.setCloudSyncBusy(false);
            const auto detail = !result.succeeded
              ? result.detail
              : std::string{
                  "No WebDAV password is saved. Enter one or use Remember Password."};
            if (automatic) {
              qWarning().noquote()
                << "Automatic cloud synchronization could not read credentials:"
                << QString::fromStdString(detail);
              window.showCloudSyncStatus(
                "Automatic cloud synchronization needs a saved WebDAV password.");
            } else {
              window.showCloudSyncError(detail);
            }
            return;
          }
          submitCloudSync(std::move(result.password), automatic);
        });
    };
  window.setCloudSettingsSink(
    [&cloudSettings, &cloudSettingsStore, &window](
      const genplusgx::cloud::Settings& settings) {
      const auto saved = cloudSettingsStore.save(settings);
      if (!saved) {
        return saved;
      }
      cloudSettings = settings;
      window.setCloudSettings(settings);
      return genplusgx::PersistenceStatus{};
    });
  window.setCloudPasswordSink(
    [&cloudCredentials, &window](std::string endpoint, std::string username,
      std::string password) {
      cloudCredentials.writePassword(endpoint, username, std::move(password),
        [&window](genplusgx::cloud::CredentialResult result) {
          if (!result.succeeded) {
            window.showCloudSyncError(result.detail);
          } else {
            window.showCloudSyncStatus(
              "WebDAV password saved in the operating-system credential store.");
          }
        });
    });
  window.setCloudForgetSink(
    [&cloudCredentials, &window](std::string endpoint, std::string username) {
      cloudCredentials.deletePassword(endpoint, username,
        [&window](genplusgx::cloud::CredentialResult result) {
          if (!result.succeeded) {
            window.showCloudSyncError(result.detail);
          } else {
            window.showCloudSyncStatus("Saved WebDAV password removed.");
          }
        });
    });
  window.setCloudSyncSink(
    [&requestCloudSync](std::string password) {
      requestCloudSync(std::move(password), false);
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
  std::uint64_t onlineMetadataOperationId = 5'100'000U;
  std::optional<std::uint64_t> pendingOnlineMetadataOperation;
  std::int64_t pendingOnlineMetadataGameId = 0;
  bool pendingOnlineMetadataAutomatic = false;
  genplusgx::BoundedQueue<std::int64_t> automaticMetadataQueue{256U};
  std::function<void(std::int64_t, bool)> requestOnlineMetadata;
  std::function<void()> startNextAutomaticMetadata;
  requestOnlineMetadata =
    [&applicationPaths, &automaticMetadataQueue, &gameLibrary,
     &onlineMetadataOperationId, &onlineMetadataService,
     &onlineMetadataSettings, &pendingOnlineMetadataAutomatic,
     &pendingOnlineMetadataGameId, &pendingOnlineMetadataOperation,
     &window](std::int64_t gameId, bool automatic) {
      if (pendingOnlineMetadataOperation) {
        if (automatic) {
          if (!automaticMetadataQueue.tryPush(gameId)) {
            qWarning() << "Automatic online metadata queue reached its 256-game "
                          "capacity; skipping library game"
                       << gameId;
          }
        } else {
          window.showGameLibraryError(
            "Another online metadata lookup is already in progress.");
        }
        return;
      }
      if (!onlineMetadataSettings.enabled) {
        if (!automatic) {
          window.showGameLibraryError(
            "Enable online metadata before starting a lookup.");
        }
        return;
      }
      const auto games = gameLibrary.games();
      if (!games.status) {
        if (!automatic) {
          window.showGameLibraryError(games.status.message);
        }
        return;
      }
      const auto found = std::ranges::find_if(
        games.games, [gameId](const auto& game) { return game.id == gameId; });
      if (found == games.games.end()) {
        if (!automatic) {
          window.showGameLibraryError(
            "The selected library game no longer exists.");
        }
        return;
      }
      const auto operationId = ++onlineMetadataOperationId;
      const auto submitted = onlineMetadataService.request(operationId, gameId,
        onlineMetadataSettings, found->metadata,
        applicationPaths.cacheDirectory());
      if (!submitted) {
        if (!automatic) {
          window.showOnlineMetadataFailed(gameId, submitted.message);
        }
        return;
      }
      pendingOnlineMetadataOperation = operationId;
      pendingOnlineMetadataGameId = gameId;
      pendingOnlineMetadataAutomatic = automatic;
      if (!automatic) {
        window.showOnlineMetadataStarted(gameId);
      }
    };
  startNextAutomaticMetadata =
    [&automaticMetadataQueue, &pendingOnlineMetadataOperation,
     &requestOnlineMetadata] {
      if (pendingOnlineMetadataOperation) {
        return;
      }
      while (auto gameId = automaticMetadataQueue.pop()) {
        requestOnlineMetadata(*gameId, true);
        if (pendingOnlineMetadataOperation) {
          return;
        }
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
    .lookupOnlineMetadata =
      [&requestOnlineMetadata](std::int64_t gameId) {
        requestOnlineMetadata(gameId, false);
      },
    .clearOnlineMetadata =
      [&gameLibrary, &pendingOnlineMetadataOperation, &refreshGameLibrary,
       &showLibraryStatus, &window](std::int64_t gameId) {
        if (pendingOnlineMetadataOperation) {
          window.showGameLibraryError(
            "Wait for the online metadata lookup before clearing metadata.");
          return;
        }
        const auto cleared = gameLibrary.clearOnlineMetadata(gameId);
        if (!cleared) {
          window.showGameLibraryError(cleared.message);
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
  window.setRewindSettings(rewindSettings);
  window.setRewindSettingsSink(
    [&rewindSettings, &rewindSettingsOperationId, &rewindSettingsStore,
     &worker](const genplusgx::RewindConfiguration& settings) {
      const auto previous = rewindSettings;
      const auto saved = rewindSettingsStore.save(settings);
      if (!saved) {
        return saved;
      }
      const auto submitted = worker.submit(
        genplusgx::EmulationCommand::updateRewindSettings(
          ++rewindSettingsOperationId, settings));
      if (!submitted) {
        static_cast<void>(rewindSettingsStore.save(previous));
        return workerPersistenceFailure(submitted.message);
      }
      rewindSettings = settings;
      return genplusgx::PersistenceStatus{};
    });
  window.setRunAheadSettings(runAheadSettings);
  window.setRunAheadSettingsSink(
    [&runAheadSettings, &runAheadSettingsOperationId, &runAheadSettingsStore,
     &worker](const genplusgx::RunAheadConfiguration& settings) {
      const auto previous = runAheadSettings;
      const auto saved = runAheadSettingsStore.save(settings);
      if (!saved) {
        return saved;
      }
      const auto submitted = worker.submit(
        genplusgx::EmulationCommand::updateRunAheadSettings(
          ++runAheadSettingsOperationId, settings));
      if (!submitted) {
        static_cast<void>(runAheadSettingsStore.save(previous));
        return workerPersistenceFailure(submitted.message);
      }
      runAheadSettings = settings;
      return genplusgx::PersistenceStatus{};
    });
  window.setSpeedSettings(speedSettings);
  window.setSpeedSettingsSink(
    [&speedSettings, &speedSettingsOperationId, &speedSettingsStore,
     &worker](const genplusgx::EmulationSpeedConfiguration& settings) {
      const auto previous = speedSettings;
      const auto saved = speedSettingsStore.save(settings);
      if (!saved) {
        return saved;
      }
      const auto submitted = worker.submit(
        genplusgx::EmulationCommand::updateSpeedSettings(
          ++speedSettingsOperationId, settings));
      if (!submitted) {
        static_cast<void>(speedSettingsStore.save(previous));
        return workerPersistenceFailure(submitted.message);
      }
      speedSettings = settings;
      return genplusgx::PersistenceStatus{};
    });
  window.setSessionSettings(sessionSettings);
  window.setSessionSettingsSink(
    [&sessionSettings, &sessionSettingsStore](bool enabled) {
      auto candidate = sessionSettings;
      candidate.resumeOnLaunch = enabled;
      if (!enabled) {
        candidate.lastGamePath.reset();
        candidate.lastPatchPath.reset();
      }
      const auto saved = sessionSettingsStore.save(candidate);
      if (saved) {
        sessionSettings = std::move(candidate);
      }
      return saved;
    });
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
    genplusgx::GameLaunchTarget target;
    std::optional<genplusgx::GameIdentity> identity;
    genplusgx::settings::PerGameSettings overrides;
    genplusgx::settings::EffectiveGameSettings effective;
    std::optional<genplusgx::GameIdentity> previousIdentity;
    genplusgx::settings::PerGameSettings previousOverrides;
    genplusgx::settings::EffectiveGameSettings previousEffective;
    genplusgx::GameLaunchTarget previousTarget;
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
  std::uint64_t physicalMediaOperationId = 2'400'000U;
  std::optional<std::uint64_t> pendingPhysicalMediaOperation;
  std::optional<std::uint64_t> pendingUnload;
  struct PendingDisc final {
    std::uint64_t operationId{0};
    genplusgx::ui::DiscUiOperation operation{
      genplusgx::ui::DiscUiOperation::change};
  };
  std::uint64_t discOperationId = 2'500'000U;
  std::optional<PendingDisc> pendingDisc;
  genplusgx::GameLaunchTarget closingGameTarget;
  const auto releasePhysicalTarget =
    [&physicalMediaCache](const genplusgx::GameLaunchTarget& target) {
      if (!target.isPhysicalMedia()) {
        return genplusgx::platform::PhysicalMediaStatus{};
      }
      const auto& media = *target.physicalMedia;
      return genplusgx::platform::releasePhysicalMediaSnapshot(
        physicalMediaCache,
        {
          .disc = {
            .drive = {.id = media.driveId, .displayName = media.displayName},
            .tracks = {},
            .leadOutSector = 0U,
          },
          .cuePath = target.runtimePath,
          .storageDirectory = media.storageDirectory,
          .sha256 = media.sha256,
          .byteSize = media.byteSize,
        });
    };
  enum class PendingStatePhase {
    capturing,
    saving,
    loading,
    restoring,
    deleting,
    importing,
    exporting,
    renaming,
  };
  struct PendingState final {
    std::uint64_t operationId{0};
    std::uint64_t gameGeneration{0};
    std::uint32_t slot{0};
    genplusgx::ui::StateUiOperation operation{
      genplusgx::ui::StateUiOperation::save};
    PendingStatePhase phase{PendingStatePhase::capturing};
    std::filesystem::path path;
    std::string name;
    std::vector<std::uint8_t> thumbnailPng;
  };
  std::optional<PendingState> pendingState;
  std::optional<std::uint64_t> stateActivationOperation;
  enum class AutomaticResumePhase {
    waitingForStateSession,
    loadingCheckpoint,
    restoringCheckpoint,
  };
  struct PendingAutomaticResume final {
    std::uint64_t gameGeneration{0U};
    std::uint64_t operationId{0U};
    AutomaticResumePhase phase{AutomaticResumePhase::waitingForStateSession};
  };
  std::optional<PendingAutomaticResume> pendingAutomaticResume;
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
  std::uint64_t netplayOperationId = 4'400'000U;
  bool netplayRuntimeActive = false;
  QObject::connect(
    &netplaySession,
    &genplusgx::netplay::NetplaySession::stateChanged,
    &window,
    [&window](genplusgx::netplay::NetplaySessionState state) {
      window.setNetplaySessionState(state);
    });
  QObject::connect(
    &netplaySession,
    &genplusgx::netplay::NetplaySession::peerConnected,
    &window,
    [&netplayOperationId, &netplayRuntimeActive, &netplaySession,
     &reportRuntimeFailure, &worker]
    (genplusgx::netplay::NetplayConfiguration configuration) {
      const auto submitted = worker.submit(
        genplusgx::EmulationCommand::startNetplaySession(
          ++netplayOperationId, configuration));
      if (!submitted) {
        reportRuntimeFailure(
          "The authenticated netplay session could not start emulation: " +
          submitted.message);
        netplaySession.disconnectFromPeer(
          "The emulation worker rejected netplay startup.");
        return;
      }
      netplayRuntimeActive = true;
      qInfo().noquote()
        << "Authenticated netplay peer connected; local player"
        << static_cast<qulonglong>(configuration.localPlayer + 1U)
        << "delay" << configuration.inputDelayFrames
        << "rollback" << configuration.rollbackFrames;
    });
  QObject::connect(
    &netplaySession,
    &genplusgx::netplay::NetplaySession::inputReceived,
    &window,
    [&netplayOperationId, &netplayRuntimeActive, &netplaySession, &worker]
    (genplusgx::netplay::NetplayInputFrame frame) {
      if (!netplayRuntimeActive) {
        netplaySession.disconnectFromPeer(
          "Peer input arrived before the emulation session was ready.");
        return;
      }
      const auto submitted = worker.submit(
        genplusgx::EmulationCommand::remoteNetplayFrame(
          ++netplayOperationId, frame));
      if (!submitted) {
        netplaySession.disconnectFromPeer(
          "The bounded emulation command queue rejected peer input.");
      }
    });
  QObject::connect(
    &netplaySession,
    &genplusgx::netplay::NetplaySession::peerDisconnected,
    &window,
    [&netplayOperationId, &netplayRuntimeActive, &worker](const QString& detail) {
      if (netplayRuntimeActive) {
        const auto stopped = worker.submit(genplusgx::EmulationCommand::simple(
          genplusgx::EmulationCommandType::stopNetplay, ++netplayOperationId));
        if (!stopped) {
          qWarning().noquote() << "Netplay runtime stop failed:"
                               << QString::fromStdString(stopped.message);
        }
        netplayRuntimeActive = false;
      }
      qInfo().noquote() << detail;
    });
  QObject::connect(
    &netplaySession,
    &genplusgx::netplay::NetplaySession::sessionError,
    &window,
    [&window](genplusgx::netplay::NetplaySessionError, const QString& detail) {
      qWarning().noquote() << "Netplay session error:" << detail;
      window.showNetplayError(detail.toStdString());
    });
  window.setNetplayRequestSink(
    [&activeCheatConfiguration, &activeEffectiveSettings,
     &activePerGameIdentity, &biosManager, &inputConfiguration,
     &netplaySession, &window](genplusgx::ui::NetplayUiRequest request) {
      if (request.operation == genplusgx::ui::NetplayUiOperation::disconnect) {
        netplaySession.disconnectFromPeer("Disconnected locally.");
        return genplusgx::PersistenceStatus{};
      }
      if (!window.isGameLoaded() || !activePerGameIdentity) {
        return workerPersistenceFailure(
          "Load a game with a valid SHA-256 identity before starting netplay.");
      }
      if (window.isSegaCdSession()) {
        if (!window.isDiscPresent() || window.isDiscEjected()) {
          return workerPersistenceFailure(
            "Insert and close a validated Sega CD disc before starting netplay.");
        }
        if (window.loadedGameTarget().isPlaylist()) {
          return workerPersistenceFailure(
            "For deterministic netplay, load the current CUE or CHD directly "
            "instead of starting from an M3U playlist.");
        }
        std::error_code equivalentError;
        const bool originalDisc = std::filesystem::equivalent(
          window.currentDiscPath(), window.loadedRuntimePath(), equivalentError);
        if (equivalentError || !originalDisc) {
          return workerPersistenceFailure(
            "Reload the originally validated Sega CD image before starting netplay.");
        }
      }
      const genplusgx::netplay::NetplaySessionDescriptor descriptor{
        .gameSha256 = activePerGameIdentity->sha256,
        .settingsSha256 = netplaySettingsFingerprint(
          activeEffectiveSettings,
          inputConfiguration,
          biosManager.snapshot(),
          activeCheatConfiguration),
        .coreVersion = GENPLUSGX_GIT_COMMIT,
      };
      const auto status = request.operation == genplusgx::ui::NetplayUiOperation::host
        ? netplaySession.host(
            descriptor,
            std::move(request.sessionCode),
            request.port,
            request.inputDelayFrames,
            request.rollbackFrames)
        : netplaySession.join(
            QString::fromStdString(request.host),
            descriptor,
            std::move(request.sessionCode),
            request.port,
            request.inputDelayFrames,
            request.rollbackFrames);
      if (!status) {
        return workerPersistenceFailure(status.message);
      }
      if (request.operation == genplusgx::ui::NetplayUiOperation::host) {
        qInfo().noquote() << "Netplay host listening on port"
                          << netplaySession.listeningPort();
      } else {
        qInfo() << "Netplay guest connection started.";
      }
      return genplusgx::PersistenceStatus{};
    });
  std::uint64_t screenshotOperationId = 4'500'000U;
  std::uint64_t recordingOperationId = 4'600'000U;
  std::uint64_t debugOperationId = 4'750'000U;
  window.setDebugRequestSink(
    [&debugOperationId, &worker](genplusgx::CoreDebugRequest request) {
      return worker.submit(genplusgx::EmulationCommand::debug(
        ++debugOperationId, std::move(request))).ok();
    });
  std::optional<std::uint64_t> pendingScreenshot;
  window.setDiagnosticsSnapshotProvider(
    [&applicationPaths, &audioOutput, &biosManager, &controllerInput,
     &diagnosticLoadedGame,
     &diagnosticLoadedRegion, &diagnosticLoadedSystem, &frontendLogger,
     &achievementBridge, &achievementNetwork,
     &cloudCredentialReadPending, &cloudOperationAutomatic, &cloudSettings,
     &pendingCloudOperation,
     &netplayBridge, &netplaySession, &recordingService, &rewindSettings,
     &runAheadSettings, &speedSettings,
     &translationManager, &window, &worker] {
      auto snapshot = genplusgx::diagnostics::staticDiagnosticsSnapshot();
      snapshot.applicationDataMode = std::string{
        genplusgx::applicationDataModeName(applicationPaths.mode())};
      snapshot.requestedInterfaceLanguage =
        translationManager.requestedLanguage();
      snapshot.effectiveInterfaceLanguage =
        translationManager.effectiveLanguage();
      snapshot.interfaceLanguageFallback =
        translationManager.usedEnglishFallback();
      snapshot.renderer = window.displayWidget()->usesAcceleratedRenderer()
        ? "OpenGL texture renderer"
        : "Qt software painter";
      const auto presentation =
        window.displayWidget()->presentationMetrics();
      snapshot.presentationSync = presentationSyncDescription(presentation);
      snapshot.presentationBuffering =
        presentationBufferingDescription(presentation);
      const auto artworkMode =
        window.videoSettings().artwork.mode;
      snapshot.videoArtwork = artworkMode == genplusgx::video::ArtworkMode::bezel
        ? (window.displayWidget()->artworkAvailable()
            ? "Bezel active" : "Bezel unavailable")
        : artworkMode == genplusgx::video::ArtworkMode::overlay
          ? (window.displayWidget()->artworkAvailable()
              ? "Overlay active" : "Overlay unavailable")
          : "Disabled";
      snapshot.videoArtworkFormat =
        window.displayWidget()->artworkFormat().empty()
          ? "None" : window.displayWidget()->artworkFormat();
      snapshot.videoArtworkBytes =
        window.displayWidget()->artworkFileBytes();
      snapshot.videoArtworkWidth = window.displayWidget()->artworkWidth();
      snapshot.videoArtworkHeight = window.displayWidget()->artworkHeight();
      snapshot.videoPublishedFrames = presentation.exchange.publishedFrames;
      snapshot.videoCopiedFrames = presentation.exchange.copiedFrames;
      snapshot.videoSkippedFrames = presentation.exchange.skippedFrames;
      snapshot.videoProducerDrops = presentation.exchange.producerDrops;
      snapshot.videoRenderedFrames = presentation.telemetry.renderedFrames;
      snapshot.videoSwappedFrames = presentation.telemetry.swappedFrames;
      snapshot.videoCoalescedFrames = presentation.telemetry.coalescedFrames;
      snapshot.videoDuplicateRenders =
        presentation.telemetry.duplicateRenders;
      snapshot.videoPendingFrames = presentation.telemetry.pendingFrames;
      snapshot.videoMaximumPendingFrames =
        presentation.telemetry.maximumPendingFrames;
      snapshot.measuredPresentationFramesPerSecond =
        presentation.telemetry.measuredFramesPerSecond;
      snapshot.averageSwapIntervalMicroseconds =
        presentation.telemetry.averageSwapIntervalMicroseconds;
      snapshot.maximumSwapIntervalMicroseconds =
        presentation.telemetry.maximumSwapIntervalMicroseconds;
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
      const auto workerMetrics = worker.metrics();
      snapshot.normalSpeedPercent = speedSettings.normalPercent;
      snapshot.slowMotionSpeedPercent = speedSettings.slowMotionPercent;
      snapshot.fastForwardSpeedPercent = speedSettings.fastForwardPercent;
      snapshot.activeSpeedPercent = workerMetrics.speedPercent;
      snapshot.fastForwarding = workerMetrics.fastForward;
      snapshot.slowMotion = workerMetrics.slowMotion;
      snapshot.rewindEnabled = rewindSettings.enabled;
      snapshot.rewinding = workerMetrics.rewinding;
      snapshot.rewindSnapshots = workerMetrics.rewindSnapshotCount;
      snapshot.rewindPayloadBytes = workerMetrics.rewindPayloadBytes;
      snapshot.rewindMemoryLimitBytes = workerMetrics.rewindMemoryLimitBytes;
      snapshot.runAheadEnabled = runAheadSettings.enabled;
      snapshot.runAheadSupported = workerMetrics.runAheadSupported;
      snapshot.runAheadActive = workerMetrics.runAheadActive;
      snapshot.runAheadVerified = workerMetrics.runAheadVerified;
      snapshot.runAheadFrames = runAheadSettings.frames;
      snapshot.runAheadSpeculativeFrames =
        workerMetrics.runAheadSpeculativeFrames;
      snapshot.runAheadRollbacks = workerMetrics.runAheadRollbacks;
      snapshot.runAheadDeterminismFailures =
        workerMetrics.runAheadDeterminismFailures;
      snapshot.runAheadStateBytes = workerMetrics.runAheadStateBytes;
      snapshot.runAheadStateCapacityBytes =
        workerMetrics.runAheadStateCapacityBytes;
      const auto netplayMetrics = netplaySession.metrics();
      switch (netplayMetrics.state) {
        case genplusgx::netplay::NetplaySessionState::disconnected:
          snapshot.netplayState = "Disconnected";
          break;
        case genplusgx::netplay::NetplaySessionState::listening:
          snapshot.netplayState = "Listening";
          break;
        case genplusgx::netplay::NetplaySessionState::connecting:
          snapshot.netplayState = "Connecting";
          break;
        case genplusgx::netplay::NetplaySessionState::authenticating:
          snapshot.netplayState = "Authenticating";
          break;
        case genplusgx::netplay::NetplaySessionState::connected:
          snapshot.netplayState = "Authenticated peer connected";
          break;
      }
      snapshot.netplaySentPackets = netplayMetrics.sentPackets;
      snapshot.netplayReceivedPackets = netplayMetrics.receivedPackets;
      snapshot.netplaySentBytes = netplayMetrics.sentBytes;
      snapshot.netplayReceivedBytes = netplayMetrics.receivedBytes;
      snapshot.netplayAuthenticationFailures =
        netplayMetrics.authenticationFailures;
      snapshot.netplayProtocolFailures = netplayMetrics.protocolFailures;
      const auto netplayBridgeMetrics = netplayBridge->metrics();
      snapshot.netplayQueuedFrames = netplayBridgeMetrics.outgoingDepth;
      snapshot.netplayQueueCapacity = netplayBridgeMetrics.outgoingCapacity;
      snapshot.netplayPredictedFrames = workerMetrics.netplayPredictedFrames;
      snapshot.netplayRollbackRequests =
        workerMetrics.netplayRollbackRequests;
      snapshot.netplayRollbacks = workerMetrics.netplayRollbacks;
      snapshot.netplayHistoryFrames = workerMetrics.netplayHistoryFrames;
      snapshot.netplayHistoryBytes = workerMetrics.netplayHistoryBytes;
      snapshot.achievementsState = !workerMetrics.achievementsEnabled
        ? "Disabled"
        : !workerMetrics.achievementsAuthenticated
          ? "Enabled; signed out"
          : workerMetrics.achievementsGameLoaded
            ? "Authenticated; recognized game active"
            : "Authenticated; no recognized game";
      snapshot.achievementsHardcore = workerMetrics.achievementsHardcore;
      snapshot.achievementRequestQueueDepth =
        workerMetrics.achievementRequestQueueDepth;
      snapshot.achievementResponseQueueDepth =
        workerMetrics.achievementResponseQueueDepth;
      snapshot.achievementQueueCapacity = achievementBridge->metrics().capacity;
      snapshot.rejectedAchievementRequests =
        workerMetrics.rejectedAchievementRequests;
      snapshot.rejectedAchievementResponses =
        workerMetrics.rejectedAchievementResponses;
      const auto achievementNetworkMetrics = achievementNetwork.metrics();
      snapshot.activeAchievementRequests =
        achievementNetworkMetrics.activeRequests;
      snapshot.completedAchievementRequests =
        achievementNetworkMetrics.completedRequests;
      snapshot.failedAchievementRequests =
        achievementNetworkMetrics.failedRequests;
      snapshot.lastAchievementNetworkError =
        achievementNetworkMetrics.lastError;
      snapshot.cloudSyncState = !cloudSettings.enabled
        ? "Disabled"
        : (pendingCloudOperation || cloudCredentialReadPending)
          ? (cloudOperationAutomatic ? "Synchronizing automatically"
                                     : "Synchronizing manually")
          : "Enabled; idle";
      snapshot.cloudSyncSaves = cloudSettings.syncSaves;
      snapshot.cloudSyncStates = cloudSettings.syncStates;
      snapshot.cloudSyncOnStartup = cloudSettings.syncOnStartup;
      snapshot.cloudSyncOnGameClose = cloudSettings.syncOnGameClose;
      const auto recordingMetrics = recordingService->metrics();
      snapshot.recordingActive = recordingMetrics.active;
      snapshot.recordingQueuedFrames = recordingMetrics.queuedFrames;
      snapshot.recordingQueueCapacity = recordingMetrics.queueCapacity;
      snapshot.recordingWrittenFrames = recordingMetrics.writtenFrames;
      snapshot.recordingDroppedFrames = recordingMetrics.droppedFrames;
      snapshot.recordingOutputBytes = recordingMetrics.outputBytes;
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
  if (recordingServiceStarted) {
    window.setRecordingSink(
      [&activePerGameIdentity, &audioOutput, &recordingOperationId,
       &recordingService, &window, &worker](
        bool start, const std::filesystem::path& directory) {
        const auto operationId = ++recordingOperationId;
        genplusgx::capture::RecordingStatus status;
        if (start) {
          const auto workerMetrics = worker.metrics();
          auto title = genplusgx::ui::pathToQString(
            window.loadedGamePath().stem()).toUtf8().toStdString();
          if (title.empty()) {
            title = "recording";
          }
          status = recordingService->begin({
            .operationId = operationId,
            .baseDirectory = directory,
            .gameTitle = std::move(title),
            .gameId = activePerGameIdentity
              ? activePerGameIdentity->sha256 : std::string{},
            .audioSampleRate = static_cast<std::uint32_t>(
              audioOutput.config().sampleRate),
            .nominalFramesPerSecond =
              workerMetrics.targetFramesPerSecond > 0.0
                ? workerMetrics.targetFramesPerSecond : 60.0,
            .timestamp = std::chrono::system_clock::now(),
          });
        } else {
          status = recordingService->end(operationId);
        }
        if (!status) {
          window.showRecordingError(status.message);
          return false;
        }
        return true;
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
  window.setPhysicalMediaActions({
    .discover = [&pendingPhysicalMediaOperation, &physicalMediaOperationId,
                  &physicalMediaService, &window] {
      if (pendingPhysicalMediaOperation) {
        return;
      }
      const auto operationId = ++physicalMediaOperationId;
      const auto submitted = physicalMediaService.discover(operationId);
      if (!submitted) {
        window.showPhysicalMediaError(submitted.message);
        return;
      }
      pendingPhysicalMediaOperation = operationId;
    },
    .importDisc = [&pendingPhysicalMediaOperation, &physicalMediaOperationId,
                    &physicalMediaService, &window](const std::string& driveId) {
      if (pendingPhysicalMediaOperation) {
        return;
      }
      const auto operationId = ++physicalMediaOperationId;
      const auto submitted = physicalMediaService.importDisc(
        operationId, driveId);
      if (!submitted) {
        window.showPhysicalMediaError(submitted.message);
        return;
      }
      pendingPhysicalMediaOperation = operationId;
      window.setPhysicalMediaImportStarted();
    },
    .cancel = [&pendingPhysicalMediaOperation, &physicalMediaService, &window] {
      if (!pendingPhysicalMediaOperation) {
        return;
      }
      const auto cancelled = physicalMediaService.cancel(
        *pendingPhysicalMediaOperation);
      if (!cancelled) {
        window.showPhysicalMediaError(cancelled.message);
      }
    },
  });
  window.setGameLoadSink(
    [&activeEffectiveSettings, &activePerGameIdentity, &activePerGameSettings,
     &applyEffectiveSettings, &automaticResumePatchPath, &automaticResumePath,
     &globalGameSettings,
     &lifecycleOperationId, &loadMetadataOperationId, &metadataService,
     &netplaySession,
     &pendingAutomaticResume, &pendingLoad, &releasePhysicalTarget,
     &window, &worker](
      const genplusgx::GameLaunchTarget& target) {
      if (netplaySession.state() !=
          genplusgx::netplay::NetplaySessionState::disconnected) {
        netplaySession.disconnectFromPeer(
          "Netplay ended because another game was selected.");
      }
      if (pendingAutomaticResume) {
        pendingAutomaticResume.reset();
        automaticResumePath.reset();
        automaticResumePatchPath.reset();
        window.setSessionResumeBusy(false);
      }
      const auto metadataId = ++loadMetadataOperationId;
      pendingLoad = PendingLoad{
        .operationId = 0U,
        .metadataOperationId = metadataId,
        .target = target,
        .identity = std::nullopt,
        .overrides = {},
        .effective = genplusgx::settings::resolvePerGameSettings(
          globalGameSettings(), {}),
        .previousIdentity = activePerGameIdentity,
        .previousOverrides = activePerGameSettings,
        .previousEffective = activeEffectiveSettings,
        .previousTarget = window.loadedGameTarget(),
        .diagnosticGame = {},
        .diagnosticSystem = {},
        .diagnosticRegion = {},
        .warning = {},
        .phase = PendingLoadPhase::metadata,
      };
      const auto requested = metadataService.request(
        metadataId, target.runtimePath);
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
        const auto abandonedTarget = pendingLoad->target;
        pendingLoad.reset();
        static_cast<void>(applyEffectiveSettings(previous));
        const auto released = releasePhysicalTarget(abandonedTarget);
        if (!released) {
          qWarning().noquote() << QString::fromStdString(released.message);
        }
        window.showGameLoadError(target.sourcePath, applied.message, false);
        return;
      }
      pendingLoad->operationId = ++lifecycleOperationId;
      pendingLoad->phase = PendingLoadPhase::coreLoad;
      const auto submitted = worker.submit(genplusgx::EmulationCommand::load(
        pendingLoad->operationId, target.runtimePath));
      if (!submitted) {
        const auto previous = pendingLoad->previousEffective;
        const auto abandonedTarget = pendingLoad->target;
        pendingLoad.reset();
        static_cast<void>(applyEffectiveSettings(previous));
        const auto released = releasePhysicalTarget(abandonedTarget);
        if (!released) {
          qWarning().noquote() << QString::fromStdString(released.message);
        }
        window.showGameLoadError(target.sourcePath, submitted.message, false);
      }
    });
  window.setGameCloseSink(
    [&closingGameTarget, &lifecycleOperationId, &netplaySession,
     &pendingUnload, &window, &worker] {
      if (netplaySession.state() !=
          genplusgx::netplay::NetplaySessionState::disconnected) {
        netplaySession.disconnectFromPeer(
          "Netplay ended because the game was closed.");
      }
      closingGameTarget = window.loadedGameTarget();
      const auto operationId = ++lifecycleOperationId;
      pendingUnload = operationId;
      const auto submitted = worker.submit(genplusgx::EmulationCommand::simple(
        genplusgx::EmulationCommandType::unloadGame, operationId));
      if (!submitted) {
        pendingUnload.reset();
        window.setGameLoaded(closingGameTarget);
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
        case UiOperation::setSlowMotion:
          command = genplusgx::EmulationCommand::slowMotion(
            operationId, enabled);
          break;
        case UiOperation::setRewinding:
          command = genplusgx::EmulationCommand::rewinding(
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
     &window, &worker](genplusgx::ui::StateUiRequest request) {
      const auto operation = request.operation;
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
        .slot = request.slot,
        .operation = operation,
        .phase = PendingStatePhase::capturing,
        .path = std::move(request.path),
        .name = std::move(request.name),
        .thumbnailPng = {},
      };
      genplusgx::StateStorageStatus submitted;
      if (operation == genplusgx::ui::StateUiOperation::save) {
        pending.thumbnailPng = window.captureStateThumbnailPng();
        const auto workerSubmitted = worker.submit(
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::captureState, operationId));
        if (!workerSubmitted) {
          window.setStateOperationBusy(false);
          window.showStateOperationError(operation, workerSubmitted.message);
          return;
        }
        pending.phase = PendingStatePhase::capturing;
      } else if (operation == genplusgx::ui::StateUiOperation::load ||
                 operation == genplusgx::ui::StateUiOperation::remove) {
        const auto commandType = operation == genplusgx::ui::StateUiOperation::load
          ? genplusgx::StateStorageCommandType::loadSlot
          : genplusgx::StateStorageCommandType::deleteSlot;
        submitted = stateStorage.submit(genplusgx::StateStorageCommand::simple(
          commandType, operationId, gameGeneration, request.slot));
        if (!submitted) {
          window.setStateOperationBusy(false);
          window.showStateOperationError(operation, submitted.message);
          return;
        }
        pending.phase = operation == genplusgx::ui::StateUiOperation::load
          ? PendingStatePhase::loading
          : PendingStatePhase::deleting;
      } else if (operation == genplusgx::ui::StateUiOperation::importFile ||
                 operation == genplusgx::ui::StateUiOperation::exportFile) {
        const auto commandType =
          operation == genplusgx::ui::StateUiOperation::importFile
          ? genplusgx::StateStorageCommandType::importSlot
          : genplusgx::StateStorageCommandType::exportSlot;
        submitted = stateStorage.submit(genplusgx::StateStorageCommand::file(
          commandType, operationId, gameGeneration, request.slot, pending.path));
        if (!submitted) {
          window.setStateOperationBusy(false);
          window.showStateOperationError(operation, submitted.message);
          return;
        }
        pending.phase = operation == genplusgx::ui::StateUiOperation::importFile
          ? PendingStatePhase::importing
          : PendingStatePhase::exporting;
      } else {
        submitted = stateStorage.submit(genplusgx::StateStorageCommand::rename(
          operationId, gameGeneration, request.slot, pending.name));
        if (!submitted) {
          window.setStateOperationBusy(false);
          window.showStateOperationError(operation, submitted.message);
          return;
        }
        pending.phase = PendingStatePhase::renaming;
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
  const auto startLoadedGame =
    [&inputAggregator, &inputOperationId, &lifecycleOperationId,
     &reportRuntimeFailure, &worker] {
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
    };
  auto nextInstrumentationLog = std::chrono::steady_clock::now() +
    std::chrono::seconds{5};
  std::uint64_t loggedAudioUnderruns = 0U;
  std::uint64_t loggedAudioOverruns = 0U;
  std::uint64_t loggedVideoProducerDrops = 0U;
  std::uint64_t loggedVideoSkippedFrames = 0U;
  std::uint64_t loggedPresentationCoalesces = 0U;
  std::uint64_t loggedLateFrames = 0U;
  std::uint64_t loggedPacingResynchronizations = 0U;
  bool audioControlFailureReported = false;
  bool stateServiceFailureReported = false;
  bool automatedReadyQuitScheduled = false;
  genplusgx::FrameRateSampler frameRateSampler;
  auto nextFrameRateSample = std::chrono::steady_clock::now();
  QTimer eventPump;
  eventPump.setInterval(8);
  QObject::connect(
    &eventPump,
    &QTimer::timeout,
    &window,
    [&activeEffectiveSettings, &activePerGameIdentity, &activePerGameSettings,
     &automatedReadyQuitScheduled,
     &automaticResumePatchPath, &automaticResumePath,
     &applyEffectiveSettings, &audioControlFailureReported, &audioOutput,
     &controllerInput, &cloudOperationAutomatic, &cloudSettings,
     &cloudSyncService, &pendingCloudOperation, &requestCloudSync,
     &startupCloudSyncDeferred,
     &automaticMetadataQueue, &gameLibrary, &onlineMetadataService,
     &onlineMetadataSettings, &pendingOnlineMetadataAutomatic,
     &pendingOnlineMetadataGameId, &pendingOnlineMetadataOperation,
     &startNextAutomaticMetadata,
     &loggedAudioOverruns, &loggedAudioUnderruns, &loggedLateFrames,
     &loggedPresentationCoalesces, &loggedVideoProducerDrops,
     &loggedVideoSkippedFrames,
     &loggedPacingResynchronizations, &nextInstrumentationLog,
     &frameRateSampler, &nextFrameRateSample,
     &deferredLibraryLaunches, &flushDeferredLibraryLaunches, &gameGeneration,
     &gameLibraryScanner, &globalGameSettings,
     &diagnosticLoadedGame, &diagnosticLoadedRegion, &diagnosticLoadedSystem,
     &inputConfiguration,
     &libraryScansInFlight,
     &lifecycleOperationId,
     &metadataService, &pendingDisc, &physicalMediaCache,
     &physicalMediaService, &pendingPhysicalMediaOperation,
     &netplayBridge, &netplayRuntimeActive, &netplaySession,
     &pendingAutomaticResume, &pendingLoad, &pendingMetadata, &pendingState,
     &pendingUnload,
     &activeCheatConfiguration, &activeCheatIdentity, &activeCheatSystem,
     &cheatMetadataOperationId, &cheatOperationId, &cheatStore,
     &pendingCheatMetadata, &pendingCheatOperation, &perGameSettingsStore,
     &pendingScreenshot, &recordingService, &screenshotService,
     &closingGameTarget, &recentGames, &recentGamesStore, &recordLibraryLaunch,
     &refreshGameLibrary, &refreshRecentGamesMenu, &stateActivationOperation,
     &sessionSettings, &sessionSettingsStore, &startLoadedGame,
     &stateOperationId,
     &stateServiceFailureReported, &stateSessionAvailable, &stateStorage,
     &releasePhysicalTarget, &reportRuntimeFailure, &runtimeFailureReported,
     &worker, &window, &achievementCredentials, &achievementNetwork,
     automatedQuitMilliseconds, automatedQuitWhenGameReady] {
    while (auto event = cloudSyncService.pollEvent()) {
      if (event->type == genplusgx::cloud::EventType::serviceStarted) {
        qInfo() << "Cloud synchronization service started.";
        continue;
      }
      if (event->type == genplusgx::cloud::EventType::serviceStopped) {
        if (cloudSettings.enabled && !event->result.status) {
          qWarning().noquote() << "Cloud synchronization service stopped:"
                               << QString::fromStdString(
                                    event->result.status.message);
          window.showCloudSyncError(event->result.status.message);
        }
        continue;
      }
      if (!pendingCloudOperation ||
          event->operationId != *pendingCloudOperation) {
        continue;
      }
      pendingCloudOperation.reset();
      window.setCloudSyncBusy(false);
      if (event->type == genplusgx::cloud::EventType::syncCompleted &&
          event->result.status) {
        qInfo().noquote()
          << "Cloud synchronization complete: uploaded"
          << event->result.summary.uploaded << "downloaded"
          << event->result.summary.downloaded << "conflicts"
          << event->result.summary.conflicts;
        if (cloudOperationAutomatic) {
          window.showCloudSyncStatus(
            "Automatic cloud synchronization completed: " +
            std::to_string(event->result.summary.uploaded) + " uploaded, " +
            std::to_string(event->result.summary.downloaded) + " downloaded, " +
            std::to_string(event->result.summary.conflicts) + " conflicts.");
        } else {
          window.showCloudSyncResult(event->result);
        }
      } else {
        const auto detail = event->result.status.message.empty()
          ? std::string{"Cloud synchronization failed without diagnostic detail."}
          : event->result.status.message;
        qWarning().noquote() << "Cloud synchronization failed:"
                             << QString::fromStdString(detail);
        if (cloudOperationAutomatic) {
          window.showCloudSyncStatus(
            "Automatic cloud synchronization failed: " + detail);
        } else {
          window.showCloudSyncError(detail);
        }
      }
      cloudOperationAutomatic = false;
    }
    while (auto event = onlineMetadataService.pollEvent()) {
      using EventType = genplusgx::library::OnlineMetadataEventType;
      if (event->type == EventType::serviceStarted) {
        qInfo() << "Online metadata service started.";
        continue;
      }
      if (event->type == EventType::serviceStopped) {
        if (pendingOnlineMetadataOperation && !pendingOnlineMetadataAutomatic) {
          window.showOnlineMetadataFailed(pendingOnlineMetadataGameId,
            "The online metadata service stopped before completing the lookup.");
        }
        pendingOnlineMetadataOperation.reset();
        pendingOnlineMetadataGameId = 0;
        pendingOnlineMetadataAutomatic = false;
        automaticMetadataQueue.clear();
        continue;
      }
      if (!pendingOnlineMetadataOperation ||
          event->operationId != *pendingOnlineMetadataOperation ||
          event->libraryGameId != pendingOnlineMetadataGameId) {
        continue;
      }
      const auto gameId = pendingOnlineMetadataGameId;
      const bool automatic = pendingOnlineMetadataAutomatic;
      pendingOnlineMetadataOperation.reset();
      pendingOnlineMetadataGameId = 0;
      pendingOnlineMetadataAutomatic = false;
      if (event->type == EventType::lookupCompleted && event->result.status) {
        const auto stored = gameLibrary.setOnlineMetadata(gameId,
          event->result.record, event->result.artworkPath);
        if (!stored) {
          qWarning().noquote() << "Online metadata database update failed:"
                               << QString::fromStdString(stored.message);
          if (!automatic) {
            window.showOnlineMetadataFailed(gameId, stored.message);
          }
        } else {
          const auto refreshed = refreshGameLibrary();
          if (!refreshed) {
            qWarning().noquote() << "Online metadata library refresh failed:"
                                 << QString::fromStdString(refreshed.message);
            if (!automatic) {
              window.showOnlineMetadataFailed(gameId, refreshed.message);
            }
          } else if (!automatic) {
            window.showOnlineMetadataCompleted(gameId, event->result.fromCache,
              event->result.staleCache);
          }
          qInfo().noquote() << "Online metadata updated for library game"
                            << gameId << "from"
                            << QString::fromStdString(
                                 event->result.record.providerName)
                            << "cache" << event->result.fromCache;
        }
      } else {
        const auto detail = event->result.status.message.empty()
          ? std::string{"Online metadata lookup failed without diagnostic detail."}
          : event->result.status.message;
        qWarning().noquote() << "Online metadata lookup failed for library game"
                             << gameId << ':' << QString::fromStdString(detail);
        if (!automatic) {
          window.showOnlineMetadataFailed(gameId, detail);
        }
      }
      startNextAutomaticMetadata();
    }
    achievementNetwork.pump();
    static_cast<void>(controllerInput.pollEvents());
    while (auto outgoing = netplayBridge->pollOutgoing()) {
      if (!netplayRuntimeActive ||
          netplaySession.state() !=
            genplusgx::netplay::NetplaySessionState::connected) {
        netplaySession.disconnectFromPeer(
          "The netplay transport was unavailable for an emulated input frame.");
        break;
      }
      const auto sent = netplaySession.sendInput(*outgoing);
      if (!sent) {
        netplaySession.disconnectFromPeer(sent.message);
        break;
      }
    }
    while (auto event = physicalMediaService.pollEvent()) {
      using EventType = genplusgx::platform::PhysicalMediaEventType;
      if (event->type == EventType::serviceStarted) {
        qInfo() << "Physical-media service started.";
        continue;
      }
      if (event->type == EventType::serviceStopped) {
        pendingPhysicalMediaOperation.reset();
        window.setPhysicalMediaSupported(false);
        continue;
      }
      if (!pendingPhysicalMediaOperation ||
          event->operationId != *pendingPhysicalMediaOperation) {
        if (event->type == EventType::importReady && event->snapshot.valid()) {
          const auto released =
            genplusgx::platform::releasePhysicalMediaSnapshot(
              physicalMediaCache, event->snapshot);
          if (!released) {
            qWarning().noquote() << QString::fromStdString(released.message);
          }
        }
        continue;
      }
      switch (event->type) {
        case EventType::discoveryReady:
          pendingPhysicalMediaOperation.reset();
          window.setPhysicalMediaDrives(std::move(event->drives));
          break;
        case EventType::importStarted:
          window.setPhysicalMediaImportStarted();
          break;
        case EventType::importProgress:
          window.setPhysicalMediaImportProgress(
            event->completedSectors, event->totalSectors);
          break;
        case EventType::importReady: {
          pendingPhysicalMediaOperation.reset();
          qInfo().noquote()
            << "Physical Sega CD imported from"
            << QString::fromStdString(event->snapshot.disc.drive.displayName)
            << "bytes" << static_cast<qulonglong>(event->snapshot.byteSize);
          if (!window.requestPhysicalMediaLoad(event->snapshot)) {
            const auto released =
              genplusgx::platform::releasePhysicalMediaSnapshot(
                physicalMediaCache, event->snapshot);
            if (!released) {
              qWarning().noquote() << QString::fromStdString(released.message);
            }
          }
          break;
        }
        case EventType::operationFailed:
          pendingPhysicalMediaOperation.reset();
          qWarning().noquote() << "Physical-media operation failed:"
                               << QString::fromStdString(event->status.message);
          window.showPhysicalMediaError(event->status.message);
          break;
        case EventType::operationCancelled:
          pendingPhysicalMediaOperation.reset();
          qInfo() << "Physical-media import cancelled.";
          window.showPhysicalMediaCancelled();
          break;
        case EventType::serviceStarted:
        case EventType::serviceStopped:
          break;
      }
    }
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
      const auto videoMetrics =
        window.displayWidget()->presentationMetrics();
      if (videoMetrics.exchange.producerDrops > loggedVideoProducerDrops) {
        qWarning().noquote()
          << "Video producer drop: total="
          << static_cast<qulonglong>(videoMetrics.exchange.producerDrops)
          << "pending="
          << static_cast<qulonglong>(
               videoMetrics.telemetry.pendingFrames);
      }
      if (videoMetrics.exchange.skippedFrames > loggedVideoSkippedFrames ||
          videoMetrics.telemetry.coalescedFrames >
            loggedPresentationCoalesces) {
        qInfo().noquote()
          << "Video presentation coalescing: source skipped="
          << static_cast<qulonglong>(videoMetrics.exchange.skippedFrames)
          << "GUI coalesced="
          << static_cast<qulonglong>(
               videoMetrics.telemetry.coalescedFrames)
          << "maximum pending="
          << static_cast<qulonglong>(
               videoMetrics.telemetry.maximumPendingFrames);
      }
      loggedVideoProducerDrops = videoMetrics.exchange.producerDrops;
      loggedVideoSkippedFrames = videoMetrics.exchange.skippedFrames;
      loggedPresentationCoalesces =
        videoMetrics.telemetry.coalescedFrames;
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
      if (event->type ==
          genplusgx::EmulationEventType::achievementEvent) {
        auto achievementEvent = std::move(event->achievement);
        if (achievementEvent.type ==
              genplusgx::achievements::EventType::loginSucceeded &&
            !achievementEvent.sessionToken.empty()) {
          auto token = std::move(achievementEvent.sessionToken);
          achievementEvent.sessionToken.clear();
          achievementCredentials.writeToken(
            achievementEvent.snapshot.username, std::move(token),
            [&window](genplusgx::achievements::CredentialResult result) {
              if (!result.succeeded) {
                window.showAchievementError(result.detail);
              }
            });
        }
        window.presentAchievementEvent(achievementEvent);
        if (achievementEvent.type ==
              genplusgx::achievements::EventType::loginFailed ||
            achievementEvent.type ==
              genplusgx::achievements::EventType::gameLoadFailed ||
            achievementEvent.type ==
              genplusgx::achievements::EventType::serverError) {
          const auto detail = achievementEvent.detail.empty()
            ? achievementEvent.snapshot.detail : achievementEvent.detail;
          qWarning().noquote() << "RetroAchievements operation failed:"
                               << QString::fromStdString(detail);
          window.showAchievementError(detail);
        }
        continue;
      }
      if (window.isGameLoaded() &&
          (event->workerState == genplusgx::EmulationWorkerState::paused ||
           event->workerState == genplusgx::EmulationWorkerState::running)) {
        window.setEmulationControlState(
          event->workerState == genplusgx::EmulationWorkerState::paused,
          event->fastForward,
          event->slowMotion,
          event->speedPercent,
          event->rewinding,
          event->rewindAvailable);
        window.setRunAheadRuntimeState(
          event->runAheadSupported,
          event->runAheadActive,
          event->runAheadVerified);
      }
      if (event->type == genplusgx::EmulationEventType::runAheadDisabled) {
        const auto detail = event->message.empty()
          ? std::string{"Run-ahead was suspended after a determinism check."}
          : event->message;
        qWarning().noquote() << QString::fromStdString(detail);
        window.showEmulationRuntimeError(
          detail + " Normal authoritative emulation remains active.");
      }
      if (event->type == genplusgx::EmulationEventType::netplayRollback) {
        qInfo().noquote() << "Netplay rollback corrected from frame"
                          << static_cast<qulonglong>(event->netplayRollbackFrame)
                          << "to" << static_cast<qulonglong>(event->frameNumber);
      }
      if ((event->netplayActive ||
           event->command == genplusgx::EmulationCommandType::startNetplay ||
           event->command ==
             genplusgx::EmulationCommandType::remoteNetplayInput) &&
          !event->succeeded()) {
        if (event->command ==
              genplusgx::EmulationCommandType::startNetplay &&
            !event->netplayActive) {
          netplayRuntimeActive = false;
        }
        const auto detail = event->message.empty()
          ? std::string{"The emulation worker rejected netplay state."}
          : event->message;
        qWarning().noquote() << QString::fromStdString(detail);
        netplaySession.disconnectFromPeer(detail);
      }
      if (event->type == genplusgx::EmulationEventType::frameCompleted) {
        static_cast<void>(window.presentLatestFrame());
        continue;
      }
      if (event->command == genplusgx::EmulationCommandType::debugRequest) {
        if (event->succeeded() &&
            (event->type == genplusgx::EmulationEventType::debugResponse ||
             event->type ==
               genplusgx::EmulationEventType::debugBreakpointHit)) {
          window.presentDebugResponse(std::move(event->debug));
        } else if (!event->succeeded()) {
          window.showDebugRequestError(
            event->message.empty()
              ? "The emulator rejected a debug request."
              : event->message,
            event->debug.clientToken);
        }
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
      if (pendingAutomaticResume &&
          pendingAutomaticResume->phase ==
            AutomaticResumePhase::restoringCheckpoint &&
          event->operationId == pendingAutomaticResume->operationId &&
          event->command == genplusgx::EmulationCommandType::restoreState) {
        const bool restored = event->succeeded();
        pendingAutomaticResume.reset();
        automaticResumePath.reset();
        automaticResumePatchPath.reset();
        window.setSessionResumeBusy(false);
        if (restored) {
          qInfo() << "Automatic session checkpoint restored.";
        } else {
          sessionSettings.lastGamePath.reset();
          sessionSettings.lastPatchPath.reset();
          const auto cleared = sessionSettingsStore.save(sessionSettings);
          if (!cleared) {
            qWarning().noquote() << QString::fromStdString(cleared.message);
          }
          window.setSessionSettings(sessionSettings);
          window.showStateOperationError(
            genplusgx::ui::StateUiOperation::load,
            "The automatic session checkpoint could not be restored. Normal "
            "emulation has started instead: " + event->message);
        }
        startLoadedGame();
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
                std::move(event->rawState),
                pendingState->name,
                pendingState->thumbnailPng,
                std::move(event->achievementProgress)));
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
        const auto loadedPath = completedLoad.target.sourcePath;
        const auto runtimePath = completedLoad.target.runtimePath;
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
          window.setGameLoaded(completedLoad.target);
          window.setEmulationControlState(
            true, event->fastForward, event->slowMotion, event->speedPercent,
            event->rewinding, event->rewindAvailable);
          window.setRunAheadRuntimeState(
            event->runAheadSupported,
            event->runAheadActive,
            event->runAheadVerified);
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
          const bool previousSnapshotStillActive =
            completedLoad.previousTarget.isPhysicalMedia() &&
            completedLoad.target.isPhysicalMedia() &&
            completedLoad.previousTarget.physicalMedia->storageDirectory ==
              completedLoad.target.physicalMedia->storageDirectory;
          if (completedLoad.previousTarget.isPhysicalMedia() &&
              !previousSnapshotStillActive) {
            const auto released = releasePhysicalTarget(
              completedLoad.previousTarget);
            if (!released) {
              qWarning().noquote()
                << "Previous physical-media snapshot cleanup failed:"
                << QString::fromStdString(released.message);
            }
          }
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
          if (completedLoad.target.isPatched()) {
            qInfo().noquote() << "Soft patch active:"
                              << genplusgx::ui::pathToQString(
                                   completedLoad.target.patchPath.filename())
                              << "runtime"
                              << genplusgx::ui::pathToQString(runtimePath);
          }
          ++gameGeneration;
          stateSessionAvailable = false;
          const auto activationId = ++stateOperationId;
          stateActivationOperation = activationId;
          const auto stateActivated = stateStorage.submit(
            genplusgx::StateStorageCommand::activate(
              activationId, gameGeneration, runtimePath, event->hardware));
          const bool automaticResumeCandidate = automaticResumePath &&
            *automaticResumePath == loadedPath;
          if (!stateActivated) {
            stateActivationOperation.reset();
            stateSessionAvailable = false;
            window.showStateOperationError(
              genplusgx::ui::StateUiOperation::load,
              "Save states are unavailable: " + stateActivated.message);
            if (automaticResumeCandidate) {
              automaticResumePath.reset();
              automaticResumePatchPath.reset();
              sessionSettings.lastGamePath.reset();
              sessionSettings.lastPatchPath.reset();
              const auto cleared = sessionSettingsStore.save(sessionSettings);
              if (!cleared) {
                qWarning().noquote() << QString::fromStdString(cleared.message);
              }
              window.setSessionSettings(sessionSettings);
            }
          }
          const bool shouldResumeAutomatically =
            automaticResumeCandidate && stateActivated.ok();
          if (shouldResumeAutomatically) {
            pendingAutomaticResume = PendingAutomaticResume{
              .gameGeneration = gameGeneration,
              .operationId = 0U,
              .phase = AutomaticResumePhase::waitingForStateSession,
            };
            window.setSessionResumeBusy(true);
          }
          const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
          if (!completedLoad.target.isPhysicalMedia()) {
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
                qWarning().noquote()
                  << QString::fromStdString(recorded.message);
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
          }
          if (!shouldResumeAutomatically) {
            startLoadedGame();
          }
          const auto cheatMetadataId = ++cheatMetadataOperationId;
          pendingCheatMetadata = PendingCheatMetadata{
            .operationId = cheatMetadataId,
            .path = runtimePath,
            .system = cheatSystem(event->hardware),
          };
          const auto cheatMetadataSubmitted = metadataService.request(
            cheatMetadataId, runtimePath);
          if (!cheatMetadataSubmitted) {
            pendingCheatMetadata.reset();
            window.showCheatError(
              "Cheat metadata could not be read: " +
              cheatMetadataSubmitted.message);
          }
        } else {
          qWarning().noquote() << "Game load failed:"
                               << QString::fromStdString(event->message);
          if (automaticResumePath && *automaticResumePath == loadedPath) {
            automaticResumePath.reset();
            automaticResumePatchPath.reset();
            pendingAutomaticResume.reset();
            sessionSettings.lastGamePath.reset();
            sessionSettings.lastPatchPath.reset();
            const auto cleared = sessionSettingsStore.save(sessionSettings);
            if (!cleared) {
              qWarning().noquote() << QString::fromStdString(cleared.message);
            }
            window.setSessionSettings(sessionSettings);
          }
          const bool previousGameRemainsLoaded =
            event->coreError == genplusgx::CoreError::persistenceFailed &&
            (event->workerState == genplusgx::EmulationWorkerState::paused ||
             event->workerState == genplusgx::EmulationWorkerState::running);
          const bool candidateIsPreviousSnapshot =
            previousGameRemainsLoaded &&
            completedLoad.target.isPhysicalMedia() &&
            completedLoad.previousTarget.isPhysicalMedia() &&
            completedLoad.target.physicalMedia->storageDirectory ==
              completedLoad.previousTarget.physicalMedia->storageDirectory;
          if (completedLoad.target.isPhysicalMedia() &&
              !candidateIsPreviousSnapshot) {
            const auto released = releasePhysicalTarget(completedLoad.target);
            if (!released) {
              qWarning().noquote()
                << "Rejected physical-media snapshot cleanup failed:"
                << QString::fromStdString(released.message);
            }
          }
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
          pendingAutomaticResume.reset();
          automaticResumePath.reset();
          automaticResumePatchPath.reset();
          window.setSessionResumeBusy(false);
          if (sessionSettings.lastGamePath) {
            auto clearedSession = sessionSettings;
            clearedSession.lastGamePath.reset();
            clearedSession.lastPatchPath.reset();
            const auto cleared = sessionSettingsStore.save(clearedSession);
            if (cleared) {
              sessionSettings = std::move(clearedSession);
              window.setSessionSettings(sessionSettings);
            } else {
              qWarning().noquote() << QString::fromStdString(cleared.message);
              window.showRecentGamesError(
                "The automatic session marker could not be cleared: " +
                cleared.message);
            }
          }
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
          const auto released = releasePhysicalTarget(closingGameTarget);
          if (!released) {
            qWarning().noquote()
              << "Physical-media snapshot cleanup after unload failed:"
              << QString::fromStdString(released.message);
          }
          closingGameTarget = {};
          if (cloudSettings.enabled &&
              (cloudSettings.syncOnGameClose || startupCloudSyncDeferred)) {
            startupCloudSyncDeferred = false;
            requestCloudSync({}, true);
          }
        } else {
          window.setGameLoaded(closingGameTarget);
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
           *event->command == genplusgx::EmulationCommandType::restoreState ||
           *event->command == genplusgx::EmulationCommandType::debugRequest);
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
        event->speedPercent == 100U && !event->rewinding;
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
        if (pendingAutomaticResume &&
            pendingAutomaticResume->gameGeneration == gameGeneration &&
            pendingAutomaticResume->phase ==
              AutomaticResumePhase::waitingForStateSession) {
          const auto operationId = ++stateOperationId;
          const auto submitted = stateStorage.submit(
            genplusgx::StateStorageCommand::simple(
              genplusgx::StateStorageCommandType::loadResume,
              operationId,
              gameGeneration));
          if (submitted) {
            pendingAutomaticResume->operationId = operationId;
            pendingAutomaticResume->phase =
              AutomaticResumePhase::loadingCheckpoint;
          } else {
            pendingAutomaticResume.reset();
            automaticResumePath.reset();
            automaticResumePatchPath.reset();
            window.setSessionResumeBusy(false);
            sessionSettings.lastGamePath.reset();
            sessionSettings.lastPatchPath.reset();
            const auto cleared = sessionSettingsStore.save(sessionSettings);
            if (!cleared) {
              qWarning().noquote() << QString::fromStdString(cleared.message);
            }
            window.setSessionSettings(sessionSettings);
            window.showStateOperationError(
              genplusgx::ui::StateUiOperation::load,
              "The automatic checkpoint request could not be queued. Normal "
              "emulation has started instead: " + submitted.message);
            startLoadedGame();
          }
        }
        continue;
      }
      if (event->type == genplusgx::StateStorageEventType::operationFailed) {
        if (pendingAutomaticResume &&
            event->operationId == pendingAutomaticResume->operationId &&
            event->gameGeneration == pendingAutomaticResume->gameGeneration) {
          const auto detail = event->message;
          qWarning().noquote()
            << "Automatic session resume failed safely:"
            << QString::fromStdString(detail);
          pendingAutomaticResume.reset();
          automaticResumePath.reset();
          automaticResumePatchPath.reset();
          window.setSessionResumeBusy(false);
          sessionSettings.lastGamePath.reset();
          sessionSettings.lastPatchPath.reset();
          const auto cleared = sessionSettingsStore.save(sessionSettings);
          if (!cleared) {
            qWarning().noquote() << QString::fromStdString(cleared.message);
          }
          window.setSessionSettings(sessionSettings);
          window.showStateOperationError(
            genplusgx::ui::StateUiOperation::load,
            "The automatic session checkpoint is missing or invalid. Normal "
            "emulation has started instead: " + detail);
          startLoadedGame();
        } else if (stateActivationOperation &&
            event->operationId == *stateActivationOperation &&
            event->gameGeneration == gameGeneration) {
          stateActivationOperation.reset();
          stateSessionAvailable = false;
          window.setStateSessionReady(false);
          window.showStateOperationError(
            genplusgx::ui::StateUiOperation::load,
            "Save states are unavailable: " + event->message);
          if (pendingAutomaticResume &&
              pendingAutomaticResume->phase ==
                AutomaticResumePhase::waitingForStateSession) {
            pendingAutomaticResume.reset();
            automaticResumePath.reset();
            automaticResumePatchPath.reset();
            window.setSessionResumeBusy(false);
            sessionSettings.lastGamePath.reset();
            sessionSettings.lastPatchPath.reset();
            const auto cleared = sessionSettingsStore.save(sessionSettings);
            if (!cleared) {
              qWarning().noquote() << QString::fromStdString(cleared.message);
            }
            window.setSessionSettings(sessionSettings);
            startLoadedGame();
          }
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
      if (pendingAutomaticResume &&
          event->type == genplusgx::StateStorageEventType::resumeLoaded &&
          event->operationId == pendingAutomaticResume->operationId &&
          event->gameGeneration == pendingAutomaticResume->gameGeneration &&
          event->gameGeneration == gameGeneration &&
          pendingAutomaticResume->phase ==
            AutomaticResumePhase::loadingCheckpoint) {
        const auto restored = worker.submit(genplusgx::EmulationCommand::restore(
          event->operationId, event->rawPayload,
          event->achievementProgress));
        if (restored) {
          pendingAutomaticResume->phase =
            AutomaticResumePhase::restoringCheckpoint;
        } else {
          pendingAutomaticResume.reset();
          automaticResumePath.reset();
          automaticResumePatchPath.reset();
          window.setSessionResumeBusy(false);
          sessionSettings.lastGamePath.reset();
          sessionSettings.lastPatchPath.reset();
          const auto cleared = sessionSettingsStore.save(sessionSettings);
          if (!cleared) {
            qWarning().noquote() << QString::fromStdString(cleared.message);
          }
          window.setSessionSettings(sessionSettings);
          window.showStateOperationError(
            genplusgx::ui::StateUiOperation::load,
            "The automatic checkpoint could not reach the emulation worker. "
            "Normal emulation has started instead: " + restored.message);
          startLoadedGame();
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
          event->operationId, event->rawPayload,
          event->achievementProgress));
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
      } else if ((event->type ==
                    genplusgx::StateStorageEventType::slotImported &&
                   pendingState->phase == PendingStatePhase::importing) ||
                 (event->type ==
                    genplusgx::StateStorageEventType::slotExported &&
                   pendingState->phase == PendingStatePhase::exporting) ||
                 (event->type ==
                    genplusgx::StateStorageEventType::slotRenamed &&
                   pendingState->phase == PendingStatePhase::renaming)) {
        const auto operation = pendingState->operation;
        const auto slot = pendingState->slot;
        window.setStateSlotViews(stateSlotViews(event->slotSummaries));
        pendingState.reset();
        window.setStateOperationBusy(false);
        window.showStateOperationSuccess(operation, slot);
        qInfo().noquote() << "Save-state management operation completed: slot"
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
    while (auto event = recordingService->pollEvent()) {
      using EventType = genplusgx::capture::RecordingEventType;
      switch (event->type) {
        case EventType::serviceStarted:
          qInfo() << "Lossless recording service started.";
          break;
        case EventType::recordingStarted:
          qInfo().noquote() << "Lossless recording started:"
                            << genplusgx::ui::pathToQString(event->path);
          window.setRecordingState(
            genplusgx::ui::RecordingUiState::recording, event->path);
          break;
        case EventType::recordingFinished:
          qInfo().noquote()
            << "Lossless recording saved:"
            << genplusgx::ui::pathToQString(event->path)
            << "frames" << static_cast<qulonglong>(event->metrics.writtenFrames)
            << "dropped" << static_cast<qulonglong>(event->metrics.droppedFrames);
          window.setRecordingState(genplusgx::ui::RecordingUiState::idle,
            event->path, event->metrics.writtenFrames,
            event->metrics.droppedFrames);
          break;
        case EventType::recordingFailed:
          qWarning().noquote() << "Lossless recording failed:"
                               << QString::fromStdString(event->status.message)
                               << genplusgx::ui::pathToQString(event->path);
          window.showRecordingError(event->status.message);
          break;
        case EventType::serviceStopped:
          if (!event->status) {
            qWarning().noquote() << "Lossless recording service stopped:"
                                 << QString::fromStdString(event->status.message);
            window.showRecordingError(event->status.message);
          }
          break;
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
          const auto path = pendingLoad->target.sourcePath;
          const auto previous = pendingLoad->previousEffective;
          const auto abandonedTarget = pendingLoad->target;
          pendingLoad.reset();
          const auto restored = applyEffectiveSettings(previous);
          const auto released = releasePhysicalTarget(abandonedTarget);
          if (!released) {
            qWarning().noquote() << QString::fromStdString(released.message);
          }
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
          event->path == pendingLoad->target.runtimePath) {
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
          const auto path = pendingLoad->target.sourcePath;
          const auto previous = pendingLoad->previousEffective;
          const auto abandonedTarget = pendingLoad->target;
          pendingLoad.reset();
          static_cast<void>(applyEffectiveSettings(previous));
          const auto released = releasePhysicalTarget(abandonedTarget);
          if (!released) {
            qWarning().noquote() << QString::fromStdString(released.message);
          }
          window.showGameLoadError(path, applied.message, false);
          continue;
        }
        pendingLoad->operationId = ++lifecycleOperationId;
        pendingLoad->phase = PendingLoadPhase::coreLoad;
        const auto submitted = worker.submit(genplusgx::EmulationCommand::load(
          pendingLoad->operationId, pendingLoad->target.runtimePath));
        if (!submitted) {
          const auto path = pendingLoad->target.sourcePath;
          const auto previous = pendingLoad->previousEffective;
          const auto abandonedTarget = pendingLoad->target;
          pendingLoad.reset();
          static_cast<void>(applyEffectiveSettings(previous));
          const auto released = releasePhysicalTarget(abandonedTarget);
          if (!released) {
            qWarning().noquote() << QString::fromStdString(released.message);
          }
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
          window.loadedRuntimePath() == requestedPath;
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
        window.loadedRuntimePath() == event->path;
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
          if (refreshed && onlineMetadataSettings.enabled &&
              onlineMetadataSettings.automaticLookup) {
            const auto games = gameLibrary.games();
            if (games.status) {
              for (const auto& game : games.games) {
                if (game.directoryId == event->directoryId &&
                    !game.onlineMetadata) {
                  if (!automaticMetadataQueue.tryPush(game.id)) {
                    qWarning() << "Automatic online metadata queue reached its "
                                  "256-game capacity; skipping library game"
                               << game.id;
                  }
                }
              }
              startNextAutomaticMetadata();
            }
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
    if (automatedQuitWhenGameReady && automatedQuitMilliseconds &&
        !automatedReadyQuitScheduled && window.isGameLoaded() &&
        stateSessionAvailable && !pendingAutomaticResume &&
        worker.state() == genplusgx::EmulationWorkerState::running) {
      automatedReadyQuitScheduled = true;
      QTimer::singleShot(
        *automatedQuitMilliseconds,
        QCoreApplication::instance(),
        &QCoreApplication::quit);
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
  std::optional<std::filesystem::path> startupGame;
  std::optional<std::filesystem::path> startupPatch;
  if (commandLine.gamePath) {
    startupGame = genplusgx::ui::pathFromQString(*commandLine.gamePath);
    if (commandLine.patchPath) {
      startupPatch = genplusgx::ui::pathFromQString(*commandLine.patchPath);
    }
  } else if (automaticResumePath) {
    startupGame = *automaticResumePath;
    startupPatch = automaticResumePatchPath;
  }
  if (startupGame) {
    const bool automatic = automaticResumePath.has_value();
    QTimer::singleShot(0, &window,
      [&automaticResumePatchPath, &automaticResumePath, &sessionSettings,
       &sessionSettingsStore, &window, automatic, startupGame = *startupGame,
       startupPatch] {
        if (window.requestGameLoad(startupGame, startupPatch) || !automatic) {
          return;
        }
        automaticResumePath.reset();
        automaticResumePatchPath.reset();
        sessionSettings.lastGamePath.reset();
        sessionSettings.lastPatchPath.reset();
        const auto cleared = sessionSettingsStore.save(sessionSettings);
        if (!cleared) {
          qWarning().noquote() << QString::fromStdString(cleared.message);
        }
        window.setSessionSettings(sessionSettings);
      });
  }
  if (cloudSettings.enabled && cloudSettings.syncOnStartup) {
    if (!startupGame) {
      QTimer::singleShot(0, &window,
        [&requestCloudSync] { requestCloudSync({}, true); });
    } else {
      startupCloudSyncDeferred = true;
      qInfo() << "Startup cloud synchronization deferred until the active game closes.";
    }
  }
  if (automatedQuitMilliseconds && !automatedQuitWhenGameReady) {
    qInfo().noquote() << "Automated startup smoke test entered the event loop.";
    QTimer::singleShot(
      *automatedQuitMilliseconds, &application, &QCoreApplication::quit);
  }
  const int result = application.exec();
  netplaySession.disconnectFromPeer("Application shutdown.");
  eventPump.stop();
  genplusgx::app::ShutdownReport shutdownReport{result};
  if (result == 0 && sessionSettings.resumeOnLaunch &&
      window.isGameLoaded() && pendingAutomaticResume) {
    shutdownReport.addFailure(
      "Automatic session resume",
      "Shutdown interrupted restoration of the previous checkpoint; the "
      "earlier checkpoint was preserved.");
  }
  const bool resumeCheckpointRequested = result == 0 &&
    sessionSettings.resumeOnLaunch && window.isGameLoaded() &&
    !window.loadedGameTarget().isPhysicalMedia() && !pendingAutomaticResume &&
    !worker.metrics().achievementsHardcore;
  if (resumeCheckpointRequested && gameGeneration != 0U &&
      stateSessionAvailable &&
      (worker.state() == genplusgx::EmulationWorkerState::paused ||
       worker.state() == genplusgx::EmulationWorkerState::running)) {
    const auto waitForWorkerOperation = [&worker](std::uint64_t operationId) {
      const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds{3};
      while (std::chrono::steady_clock::now() < deadline) {
        auto event = worker.waitForEvent(std::chrono::milliseconds{100});
        if (event && event->operationId == operationId) {
          return event;
        }
      }
      return std::optional<genplusgx::EmulationEvent>{};
    };
    const auto waitForStateOperation = [&stateStorage](
                                       std::uint64_t operationId) {
      const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds{3};
      while (std::chrono::steady_clock::now() < deadline) {
        auto event = stateStorage.waitForEvent(std::chrono::milliseconds{100});
        if (event && event->operationId == operationId) {
          return event;
        }
      }
      return std::optional<genplusgx::StateStorageEvent>{};
    };

    bool checkpointReady = true;
    if (worker.state() == genplusgx::EmulationWorkerState::running) {
      const auto pauseId = ++stateOperationId;
      const auto submitted = worker.submit(genplusgx::EmulationCommand::simple(
        genplusgx::EmulationCommandType::pause, pauseId));
      const auto paused = submitted ? waitForWorkerOperation(pauseId)
                                    : std::nullopt;
      if (!submitted || !paused || !paused->succeeded()) {
        checkpointReady = false;
        shutdownReport.addFailure(
          "Automatic session resume",
          submitted
            ? "The emulation worker did not pause for the shutdown checkpoint."
            : submitted.message);
      }
    }
    if (checkpointReady) {
      const auto captureId = ++stateOperationId;
      const auto submitted = worker.submit(genplusgx::EmulationCommand::simple(
        genplusgx::EmulationCommandType::captureState, captureId));
      auto captured = submitted ? waitForWorkerOperation(captureId)
                                : std::nullopt;
      if (!submitted || !captured || !captured->succeeded()) {
        checkpointReady = false;
        shutdownReport.addFailure(
          "Automatic session resume",
          submitted && captured
            ? captured->message
            : (submitted
                ? "The emulation worker did not return a shutdown checkpoint."
                : submitted.message));
      } else {
        const auto stored = stateStorage.submit(
          genplusgx::StateStorageCommand::saveResumeState(
            captureId,
            gameGeneration,
            captured->frameNumber,
            std::move(captured->rawState),
            std::move(captured->achievementProgress)));
        const auto stateEvent = stored ? waitForStateOperation(captureId)
                                      : std::nullopt;
        if (!stored || !stateEvent || !stateEvent->succeeded() ||
            stateEvent->type != genplusgx::StateStorageEventType::resumeSaved) {
          checkpointReady = false;
          shutdownReport.addFailure(
            "Automatic session resume",
            stored && stateEvent
              ? stateEvent->message
              : (stored
                  ? "The state service did not confirm the shutdown checkpoint."
                  : stored.message));
        }
      }
    }
    if (checkpointReady) {
      std::error_code absolutePathError;
      auto checkpointGamePath = std::filesystem::absolute(
        window.loadedGamePath(), absolutePathError);
      if (absolutePathError || checkpointGamePath.empty()) {
        shutdownReport.addFailure(
          "Automatic session resume",
          "The running game's absolute path could not be recorded.");
      } else {
        checkpointGamePath = checkpointGamePath.lexically_normal();
        auto savedSession = sessionSettings;
        savedSession.lastGamePath = std::move(checkpointGamePath);
        savedSession.lastPatchPath.reset();
        if (window.loadedGameTarget().isPatched()) {
          std::error_code patchPathError;
          auto checkpointPatchPath = std::filesystem::absolute(
            window.loadedGameTarget().patchPath, patchPathError);
          if (patchPathError || checkpointPatchPath.empty()) {
            checkpointReady = false;
            shutdownReport.addFailure(
              "Automatic session resume",
              "The running game's patch path could not be recorded.");
          } else {
            savedSession.lastPatchPath = checkpointPatchPath.lexically_normal();
          }
        }
        if (checkpointReady) {
          const auto saved = sessionSettingsStore.save(savedSession);
          if (saved) {
            sessionSettings = std::move(savedSession);
            qInfo().noquote() << "Automatic session checkpoint saved for"
                              << genplusgx::ui::pathToQString(
                                   *sessionSettings.lastGamePath);
          } else {
            shutdownReport.addFailure("Automatic session resume", saved.message);
          }
        }
      }
    }
  } else if (resumeCheckpointRequested) {
    shutdownReport.addFailure(
      "Automatic session resume",
      "The active game did not have a ready core/state session at shutdown.");
  }
  window.displayWidget()->setRendererFailureSink({});
  keyboardInput.setSnapshotSink({});
  keyboardInput.detach();
  controllerInput.setSnapshotSink({});
  controllerInput.setConnectionSink({});
  controllerInput.setCaptureSink({});
  window.setInputConfigurationSink({});
  window.setControllerAssignmentSink({});
  window.setGameLoadSink({});
  window.setPhysicalMediaActions({});
  window.setGameCloseSink({});
  window.setClearRecentGamesSink({});
  window.setStateOperationSink({});
  window.setGameInformationRequestSink({});
  window.setGameLibraryActions({});
  window.setScreenshotSink({});
  window.setRecordingSink({});
  window.setScreenshotSettingsSink({});
  window.setCheatConfigurationSink({});
  window.setPerGameSettingsSink({});
  window.setAppearanceSettingsSink({});
  window.setSessionSettingsSink({});
  window.setRunAheadSettingsSink({});
  window.setDiagnosticsSnapshotProvider({});
  window.setDebugRequestSink({});
  window.setNetplayRequestSink({});
  window.setAchievementSettingsSink({});
  window.setAchievementLoginSink({});
  window.setAchievementLogoutSink({});
  window.setCloudSettingsSink({});
  window.setCloudPasswordSink({});
  window.setCloudForgetSink({});
  window.setCloudSyncSink({});
  window.setOnlineMetadataSettingsSink({});
  window.setVideoSettingsSink({});
  window.setAudioSettingsSink({});
  window.setSystemSettingsSink({});
  window.setBiosConfigurationSink({});
  inputAggregator.setSnapshotSink({});
  const auto recordCleanup = [&shutdownReport](
                               const char* service, const auto& status) {
    if (status) {
      return;
    }
    qWarning().noquote() << service << '-'
                         << QString::fromStdString(status.message);
    shutdownReport.addFailure(service, status.message);
  };
  const auto cloudSyncServiceStopped = cloudSyncService.stop();
  recordCleanup("Cloud synchronization service", cloudSyncServiceStopped);
  const auto onlineMetadataServiceStopped = onlineMetadataService.stop();
  recordCleanup("Online metadata service", onlineMetadataServiceStopped);
  const auto workerStopped = worker.stop();
  recordCleanup("Emulation worker", workerStopped);
  const auto physicalMediaServiceStopped = physicalMediaService.stop();
  recordCleanup("Physical-media service", physicalMediaServiceStopped);
  while (auto event = physicalMediaService.pollEvent()) {
    if (event->type ==
          genplusgx::platform::PhysicalMediaEventType::importReady &&
        event->snapshot.valid()) {
      recordCleanup("Physical-media pending snapshot",
        genplusgx::platform::releasePhysicalMediaSnapshot(
          physicalMediaCache, event->snapshot));
    }
  }
  recordCleanup(
    "Physical-media active snapshot",
    releasePhysicalTarget(window.loadedGameTarget()));
  if (pendingLoad &&
      (!window.loadedGameTarget().isPhysicalMedia() ||
       pendingLoad->target.physicalMedia !=
         window.loadedGameTarget().physicalMedia)) {
    recordCleanup(
      "Physical-media pending game snapshot",
      releasePhysicalTarget(pendingLoad->target));
  }
  const auto recordingServiceStopped = recordingService->stop();
  recordCleanup("Lossless recording service", recordingServiceStopped);
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
