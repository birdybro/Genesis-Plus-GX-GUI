#include "genplusgx/ui/main_window.h"
#include "genplusgx/version.h"
#include "genplusgx/audio_output.h"
#include "genplusgx/emulation_worker.h"
#include "genplusgx/video/display_widget.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

#include <memory>

int main(int argc, char* argv[])
{
  QApplication application(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("Genesis Plus GX GUI"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("genesisplusgx.org"));
  QCoreApplication::setApplicationName(QString::fromLatin1(GENPLUSGX_APP_NAME));
  QCoreApplication::setApplicationVersion(QString::fromLatin1(GENPLUSGX_VERSION));
  QApplication::setDesktopFileName(QString::fromLatin1(GENPLUSGX_APP_ID));

  QCommandLineParser parser;
  parser.setApplicationDescription(
    QCoreApplication::translate("main", "Native desktop frontend for Genesis Plus GX."));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.process(application);

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
  QTimer eventPump;
  eventPump.setInterval(8);
  QObject::connect(
    &eventPump,
    &QTimer::timeout,
    &window,
    [&audioOutput, &worker, &window] {
    while (const auto event = worker.pollEvent()) {
      if (event->type == genplusgx::EmulationEventType::frameCompleted) {
        static_cast<void>(window.displayWidget()->presentLatestFrame());
        continue;
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
  const int result = application.exec();
  eventPump.stop();
  static_cast<void>(worker.stop());
  static_cast<void>(audioOutput.shutdown());
  return result;
}
