#include "genplusgx/ui/main_window.h"
#include "genplusgx/app/command_line.h"
#include "genplusgx/version.h"
#include "genplusgx/audio_output.h"
#include "genplusgx/backup_store.h"
#include "genplusgx/emulation_worker.h"
#include "genplusgx/input/controller_input.h"
#include "genplusgx/input/input_aggregator.h"
#include "genplusgx/input/input_profile.h"
#include "genplusgx/input/keyboard_input.h"
#include "genplusgx/persistence.h"
#include "genplusgx/platform/bios_manager.h"
#include "genplusgx/recent_games.h"
#include "genplusgx/state_storage_service.h"
#include "genplusgx/settings/video_settings.h"
#include "genplusgx/settings/audio_settings.h"
#include "genplusgx/settings/system_settings.h"
#include "genplusgx/ui/dialog_service.h"
#include "genplusgx/video/display_widget.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QTextStream>
#include <QTimer>

#include <chrono>
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>

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
    .segaCdUsa = validPath(genplusgx::platform::BiosSlot::segaCdUsa),
    .segaCdEurope = validPath(genplusgx::platform::BiosSlot::segaCdEurope),
    .segaCdJapan = validPath(genplusgx::platform::BiosSlot::segaCdJapan),
  };
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

} // namespace

int main(int argc, char* argv[])
{
  QApplication application(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("Genesis Plus GX GUI"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("genesisplusgx.org"));
  QCoreApplication::setApplicationName(QString::fromLatin1(GENPLUSGX_APP_NAME));
  QCoreApplication::setApplicationVersion(QString::fromLatin1(GENPLUSGX_VERSION));
  QApplication::setDesktopFileName(QString::fromLatin1(GENPLUSGX_APP_ID));

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

  const auto applicationPaths = genplusgx::ApplicationPaths::fromPlatform();
  const auto pathsInitialized = applicationPaths.initialize();
  if (!pathsInitialized) {
    qWarning().noquote() << QString::fromStdString(pathsInitialized.message);
  }
  genplusgx::input::InputProfileStore inputProfileStore{
    applicationPaths.configDirectory() / "input-profiles.json"};
  auto loadedInputProfiles = inputProfileStore.load();
  if (!loadedInputProfiles.status) {
    qWarning().noquote() << QString::fromStdString(loadedInputProfiles.status.message);
  }
  auto inputConfiguration = std::move(loadedInputProfiles.configuration);
  if (loadedInputProfiles.migrated) {
    const auto migrated = inputProfileStore.save(inputConfiguration);
    if (!migrated) {
      qWarning().noquote() << QString::fromStdString(migrated.message);
    }
  }
  genplusgx::RecentGamesStore recentGamesStore{
    applicationPaths.configDirectory() / "recent-games.json"};
  auto loadedRecentGames = recentGamesStore.load();
  if (!loadedRecentGames.status) {
    qWarning().noquote() << QString::fromStdString(loadedRecentGames.status.message);
  }
  auto recentGames = std::move(loadedRecentGames.model);
  if (loadedRecentGames.migrated) {
    const auto migrated = recentGamesStore.save(recentGames);
    if (!migrated) {
      qWarning().noquote() << QString::fromStdString(migrated.message);
    }
  }
  genplusgx::settings::VideoSettingsStore videoSettingsStore{
    applicationPaths.configDirectory() / "video-settings.json"};
  auto loadedVideoSettings = videoSettingsStore.load();
  if (!loadedVideoSettings.status) {
    qWarning().noquote() << QString::fromStdString(
      loadedVideoSettings.status.message);
  }
  auto videoSettings = loadedVideoSettings.settings;
  if (loadedVideoSettings.migrated) {
    const auto migrated = videoSettingsStore.save(videoSettings);
    if (!migrated) {
      qWarning().noquote() << QString::fromStdString(migrated.message);
    }
  }

  genplusgx::settings::AudioSettingsStore audioSettingsStore{
    applicationPaths.configDirectory() / "audio-settings.json"};
  auto loadedAudioSettings = audioSettingsStore.load();
  if (!loadedAudioSettings.status) {
    qWarning().noquote() << QString::fromStdString(
      loadedAudioSettings.status.message);
  }
  auto audioSettings = loadedAudioSettings.settings;
  if (loadedAudioSettings.migrated) {
    const auto migrated = audioSettingsStore.save(audioSettings);
    if (!migrated) {
      qWarning().noquote() << QString::fromStdString(migrated.message);
    }
  }
  genplusgx::settings::SystemSettingsStore systemSettingsStore{
    applicationPaths.configDirectory() / "system-settings.json"};
  auto loadedSystemSettings = systemSettingsStore.load();
  if (!loadedSystemSettings.status) {
    qWarning().noquote() << QString::fromStdString(
      loadedSystemSettings.status.message);
  }
  auto systemSettings = loadedSystemSettings.settings;
  if (loadedSystemSettings.migrated) {
    const auto migrated = systemSettingsStore.save(systemSettings);
    if (!migrated) {
      qWarning().noquote() << QString::fromStdString(migrated.message);
    }
  }
  genplusgx::platform::BiosManager biosManager{
    genplusgx::platform::BiosConfigurationStore{
      applicationPaths.configDirectory() / "bios.json"}};
  const auto biosLoaded = biosManager.load();
  if (!biosLoaded) {
    qWarning().noquote() << QString::fromStdString(biosLoaded.message);
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
    }
  }

  genplusgx::AudioOutput audioOutput{audioOutputConfig};
  const auto audioInitialized = audioOutput.initialize();
  if (!audioInitialized) {
    qWarning().noquote() << QString::fromStdString(audioInitialized.message);
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
    static_cast<void>(stateStorage.stop());
    static_cast<void>(audioOutput.shutdown());
    return 1;
  }

  genplusgx::ui::MainWindow window;
  window.displayWidget()->setFrameExchange(videoFrames);
  window.setVideoSettings(videoSettings);
  window.setAudioSettings(audioSettings);
  window.setSystemSettings(systemSettings);
  window.setBiosSnapshot(biosManager.snapshot());
  std::uint64_t firmwareSettingsOperationId = 800'000U;
  const auto initialFirmwareSettings = worker.submit(
    genplusgx::EmulationCommand::updateFirmwareSettings(
      ++firmwareSettingsOperationId,
      coreFirmwareSettings(biosManager.snapshot())));
  if (!initialFirmwareSettings) {
    qWarning().noquote() << QString::fromStdString(
      initialFirmwareSettings.message);
  }
  window.setBiosConfigurationSink(
    [&biosManager, &firmwareSettingsOperationId, &window, &worker](
      const genplusgx::platform::BiosConfiguration& configuration) {
      const auto saved = biosManager.apply(configuration);
      if (!saved) {
        return saved;
      }
      window.setBiosSnapshot(biosManager.snapshot());
      const auto submitted = worker.submit(
        genplusgx::EmulationCommand::updateFirmwareSettings(
          ++firmwareSettingsOperationId,
          coreFirmwareSettings(biosManager.snapshot())));
      if (!submitted) {
        return genplusgx::PersistenceStatus{
          .error = genplusgx::PersistenceError::invalidData,
          .message = "BIOS paths were saved, but the emulation worker could not "
                     "accept the update: " + submitted.message,
        };
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
  }
  window.setVideoSettingsSink(
    [&videoSettings, &videoSettingsOperationId, &videoSettingsStore, &worker](
      const genplusgx::settings::VideoSettings& settings) {
      const auto submitted = worker.submit(
        genplusgx::EmulationCommand::updateVideoSettings(
          ++videoSettingsOperationId, settings.core));
      if (!submitted) {
        qWarning().noquote() << QString::fromStdString(submitted.message);
        return;
      }
      videoSettings = settings;
      const auto saved = videoSettingsStore.save(settings);
      if (!saved) {
        qWarning().noquote() << QString::fromStdString(saved.message);
      }
    });
  std::uint64_t audioSettingsOperationId = 600'000U;
  const auto initialAudioSettings = worker.submit(
    genplusgx::EmulationCommand::updateAudioSettings(
      ++audioSettingsOperationId, audioSettings.core));
  if (!initialAudioSettings) {
    qWarning().noquote() << QString::fromStdString(initialAudioSettings.message);
  }
  window.setAudioSettingsSink(
    [&audioOutput, &audioSettings, &audioSettingsOperationId,
     &audioSettingsStore, &worker](
      const genplusgx::settings::AudioSettings& settings) {
      const auto submitted = worker.submit(
        genplusgx::EmulationCommand::updateAudioSettings(
          ++audioSettingsOperationId, settings.core));
      if (!submitted) {
        qWarning().noquote() << QString::fromStdString(submitted.message);
        return;
      }
      const auto volume = audioOutput.setVolumePercent(
        settings.masterVolumePercent);
      if (!volume) {
        qWarning().noquote() << QString::fromStdString(volume.message);
        return;
      }
      audioOutput.setMuted(settings.muted);
      audioSettings = settings;
      const auto saved = audioSettingsStore.save(settings);
      if (!saved) {
        qWarning().noquote() << QString::fromStdString(saved.message);
      }
    });
  std::uint64_t systemSettingsOperationId = 700'000U;
  const auto initialSystemSettings = worker.submit(
    genplusgx::EmulationCommand::updateSystemSettings(
      ++systemSettingsOperationId, systemSettings));
  if (!initialSystemSettings) {
    qWarning().noquote() << QString::fromStdString(initialSystemSettings.message);
  }
  window.setSystemSettingsSink(
    [&systemSettings, &systemSettingsOperationId, &systemSettingsStore, &worker](
      const genplusgx::CoreSystemSettings& settings) {
      const auto submitted = worker.submit(
        genplusgx::EmulationCommand::updateSystemSettings(
          ++systemSettingsOperationId, settings));
      if (!submitted) {
        qWarning().noquote() << QString::fromStdString(submitted.message);
        return;
      }
      systemSettings = settings;
      const auto saved = systemSettingsStore.save(settings);
      if (!saved) {
        qWarning().noquote() << QString::fromStdString(saved.message);
      }
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
      recentGames.clear();
      refreshRecentGamesMenu();
      const auto saved = recentGamesStore.save(recentGames);
      if (!saved) {
        qWarning().noquote() << QString::fromStdString(saved.message);
      }
    });
  genplusgx::input::InputAggregator inputAggregator;
  genplusgx::input::KeyboardInput keyboardInput{&window};
  genplusgx::input::ControllerInput controllerInput;
  window.setInputConfiguration(inputConfiguration);
  const auto applyActiveInputProfile =
    [&inputConfiguration, &keyboardInput, &controllerInput] {
      const auto* profile = inputConfiguration.active();
      if (profile == nullptr) {
        return;
      }
      if (!keyboardInput.setBindings(profile->keyboardBindings)) {
        qWarning() << "The active keyboard profile was rejected by the runtime.";
      }
      if (!controllerInput.setBindings(profile->controllerBindings)) {
        qWarning() << "The active controller profile was rejected by the runtime.";
      }
      if (!controllerInput.setAxisBindings(profile->controllerAxisBindings)) {
        qWarning() << "The active controller axis profile was rejected by the runtime.";
      }
      controllerInput.setDeadzone(profile->deadzone);
    };
  applyActiveInputProfile();
  window.setInputConfigurationSink(
    [&inputConfiguration, &inputProfileStore, &applyActiveInputProfile](
      const genplusgx::input::InputConfiguration& configuration) {
      inputConfiguration = configuration;
      applyActiveInputProfile();
      const auto saved = inputProfileStore.save(configuration);
      if (!saved) {
        qWarning().noquote() << QString::fromStdString(saved.message);
      }
    });
  window.setControllerAssignmentSink(
    [&controllerInput, &window](std::uint32_t instanceId, std::size_t player) {
      if (!controllerInput.assignPlayer(instanceId, player)) {
        qWarning() << "A controller player assignment was rejected.";
      }
      window.setConnectedControllers(controllerInput.controllers());
    });
  keyboardInput.attach(*window.displayWidget());
  std::uint64_t inputOperationId = 1'000'000U;
  inputAggregator.setSnapshotSink(
    [&inputOperationId, &worker](const genplusgx::InputSnapshot& snapshot) {
      const auto state = worker.state();
      if (state != genplusgx::EmulationWorkerState::paused &&
          state != genplusgx::EmulationWorkerState::running) {
        return;
      }
      const auto submitted = worker.submit(
        genplusgx::EmulationCommand::updateInput(++inputOperationId, snapshot));
      if (!submitted) {
        qWarning().noquote() << QString::fromStdString(submitted.message);
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
  }

  struct PendingLoad final {
    std::uint64_t operationId{0};
    std::filesystem::path path;
  };
  std::uint64_t lifecycleOperationId = 2'000'000U;
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
  window.setGameLoadSink(
    [&lifecycleOperationId, &pendingLoad, &window, &worker](
      const std::filesystem::path& path) {
      const auto operationId = ++lifecycleOperationId;
      pendingLoad = PendingLoad{.operationId = operationId, .path = path};
      const auto submitted = worker.submit(
        genplusgx::EmulationCommand::load(operationId, path));
      if (!submitted) {
        pendingLoad.reset();
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
  QTimer eventPump;
  eventPump.setInterval(8);
  QObject::connect(
    &eventPump,
    &QTimer::timeout,
    &window,
    [&audioOutput, &controllerInput, &gameGeneration, &inputAggregator,
     &inputOperationId, &lifecycleOperationId, &pendingDisc, &pendingLoad,
     &pendingState, &pendingUnload, &closingGamePath, &recentGames, &recentGamesStore,
     &refreshRecentGamesMenu, &stateActivationOperation, &stateOperationId,
     &stateSessionAvailable, &stateStorage, &worker, &window] {
    static_cast<void>(controllerInput.pollEvents());
    while (auto event = worker.pollEvent()) {
      if (event->type == genplusgx::EmulationEventType::frameCompleted) {
        static_cast<void>(window.displayWidget()->presentLatestFrame());
        continue;
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
          } else {
            window.showStateOperationError(operation, event->message);
          }
        }
      }
      if (pendingLoad && event->operationId == pendingLoad->operationId &&
          event->command == genplusgx::EmulationCommandType::loadGame) {
        const auto loadedPath = pendingLoad->path;
        pendingLoad.reset();
        if (event->succeeded()) {
          window.setGameLoaded(loadedPath);
          window.setSegaCdSession(
            event->disc.segaCd,
            discRegionName(event->disc.region),
            event->disc.path,
            event->disc.trayOpen,
            event->disc.discPresent);
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
          if (recentGames.add(loadedPath, now)) {
            refreshRecentGamesMenu();
            const auto saved = recentGamesStore.save(recentGames);
            if (!saved) {
              qWarning().noquote() << QString::fromStdString(saved.message);
            }
          }
          const auto inputSubmitted = worker.submit(
            genplusgx::EmulationCommand::updateInput(
              ++inputOperationId, inputAggregator.snapshot()));
          if (!inputSubmitted) {
            qWarning().noquote() << QString::fromStdString(inputSubmitted.message);
          }
          const auto started = worker.submit(genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::start, ++lifecycleOperationId));
          if (!started) {
            qWarning().noquote() << QString::fromStdString(started.message);
          }
        } else {
          const bool previousGameRemainsLoaded =
            event->coreError == genplusgx::CoreError::persistenceFailed &&
            (event->workerState == genplusgx::EmulationWorkerState::paused ||
             event->workerState == genplusgx::EmulationWorkerState::running);
          window.showGameLoadError(
            loadedPath, event->message, !previousGameRemainsLoaded);
          if (previousGameRemainsLoaded) {
            window.setStateSessionReady(stateSessionAvailable);
            window.setSegaCdSession(
              event->disc.segaCd,
              discRegionName(event->disc.region),
              event->disc.path,
              event->disc.trayOpen,
              event->disc.discPresent);
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
          window.setNoGameLoaded();
          closingGamePath.clear();
        } else {
          window.setGameLoaded(closingGamePath);
          window.setSegaCdSession(
            event->disc.segaCd,
            discRegionName(event->disc.region),
            event->disc.path,
            event->disc.trayOpen,
            event->disc.discPresent);
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
        }
      } else if (!audioShouldRun && !audioOutput.isPaused()) {
        const auto paused = audioOutput.pause();
        if (!paused) {
          qWarning().noquote() << QString::fromStdString(paused.message);
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
      } else if (event->type == genplusgx::StateStorageEventType::slotDeleted &&
                 pendingState->phase == PendingStatePhase::deleting) {
        const auto operation = pendingState->operation;
        const auto slot = pendingState->slot;
        window.setStateSlotViews(stateSlotViews(event->slotSummaries));
        pendingState.reset();
        window.setStateOperationBusy(false);
        window.showStateOperationSuccess(operation, slot);
      }
    }
  });
  eventPump.start();
  window.show();
  if (commandLine.fullscreen) {
    window.setFullscreen(true);
  }
  if (commandLine.gamePath) {
    const auto startupGame = genplusgx::ui::pathFromQString(*commandLine.gamePath);
    QTimer::singleShot(0, &window, [&window, startupGame] {
      static_cast<void>(window.requestGameLoad(startupGame));
    });
  }
  const int result = application.exec();
  eventPump.stop();
  keyboardInput.setSnapshotSink({});
  keyboardInput.detach();
  controllerInput.setSnapshotSink({});
  controllerInput.setConnectionSink({});
  controllerInput.setCaptureSink({});
  static_cast<void>(controllerInput.shutdown());
  window.setInputConfigurationSink({});
  window.setControllerAssignmentSink({});
  window.setGameLoadSink({});
  window.setGameCloseSink({});
  window.setClearRecentGamesSink({});
  window.setStateOperationSink({});
  window.setVideoSettingsSink({});
  inputAggregator.setSnapshotSink({});
  const auto workerStopped = worker.stop();
  if (!workerStopped) {
    qWarning().noquote() << QString::fromStdString(workerStopped.message);
  }
  const auto stateStorageStopped = stateStorage.stop();
  if (!stateStorageStopped) {
    qWarning().noquote() << QString::fromStdString(stateStorageStopped.message);
  }
  static_cast<void>(audioOutput.shutdown());
  return result;
}
