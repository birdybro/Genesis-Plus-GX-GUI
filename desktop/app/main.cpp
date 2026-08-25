#include "genplusgx/ui/main_window.h"
#include "genplusgx/app/command_line.h"
#include "genplusgx/version.h"
#include "genplusgx/audio_output.h"
#include "genplusgx/emulation_worker.h"
#include "genplusgx/input/controller_input.h"
#include "genplusgx/input/input_aggregator.h"
#include "genplusgx/input/input_profile.h"
#include "genplusgx/input/keyboard_input.h"
#include "genplusgx/persistence.h"
#include "genplusgx/ui/dialog_service.h"
#include "genplusgx/video/display_widget.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QTextStream>
#include <QTimer>

#include <cstdint>
#include <memory>
#include <optional>

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

  genplusgx::AudioOutput audioOutput;
  const auto audioInitialized = audioOutput.initialize();
  if (!audioInitialized) {
    qWarning().noquote() << QString::fromStdString(audioInitialized.message);
  } else {
    qInfo().noquote() << "Audio output:"
                      << QString::fromStdString(audioOutput.deviceName());
  }

  auto videoFrames = std::make_shared<genplusgx::VideoFrameExchange>();
  genplusgx::EmulationWorker worker{
    64U,
    64U,
    audioOutput.config().sampleRate,
    videoFrames,
    audioOutput.ringBuffer()};
  const auto workerStarted = worker.start();
  if (!workerStarted) {
    qCritical().noquote() << QString::fromStdString(workerStarted.message);
    static_cast<void>(audioOutput.shutdown());
    return 1;
  }

  genplusgx::ui::MainWindow window;
  window.displayWidget()->setFrameExchange(videoFrames);
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
  std::optional<PendingLoad> pendingLoad;
  std::optional<std::uint64_t> pendingUnload;
  std::filesystem::path closingGamePath;
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
        qWarning().noquote() << QString::fromStdString(submitted.message);
      }
    });
  QTimer eventPump;
  eventPump.setInterval(8);
  QObject::connect(
    &eventPump,
    &QTimer::timeout,
    &window,
    [&audioOutput, &controllerInput, &inputAggregator, &inputOperationId,
     &lifecycleOperationId, &pendingLoad, &pendingUnload, &closingGamePath,
     &worker, &window] {
    static_cast<void>(controllerInput.pollEvents());
    while (const auto event = worker.pollEvent()) {
      if (event->type == genplusgx::EmulationEventType::frameCompleted) {
        static_cast<void>(window.displayWidget()->presentLatestFrame());
        continue;
      }
      if (pendingLoad && event->operationId == pendingLoad->operationId &&
          event->command == genplusgx::EmulationCommandType::loadGame) {
        const auto loadedPath = pendingLoad->path;
        pendingLoad.reset();
        if (event->succeeded()) {
          window.setGameLoaded(loadedPath);
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
          window.showGameLoadError(loadedPath, event->message);
        }
      } else if (pendingUnload && event->operationId == *pendingUnload &&
                 event->command == genplusgx::EmulationCommandType::unloadGame) {
        pendingUnload.reset();
        if (event->succeeded()) {
          window.setNoGameLoaded();
          closingGamePath.clear();
        } else {
          window.setGameLoaded(closingGamePath);
          qWarning().noquote() << QString::fromStdString(event->message);
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
  inputAggregator.setSnapshotSink({});
  static_cast<void>(worker.stop());
  static_cast<void>(audioOutput.shutdown());
  return result;
}
