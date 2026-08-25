#include "genplusgx/ui/main_window.h"
#include "genplusgx/version.h"
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

  auto videoFrames = std::make_shared<genplusgx::VideoFrameExchange>();
  genplusgx::EmulationWorker worker{64U, 64U, 48'000, videoFrames};
  const auto workerStarted = worker.start();
  if (!workerStarted) {
    qCritical().noquote() << QString::fromStdString(workerStarted.message);
    return 1;
  }

  genplusgx::ui::MainWindow window;
  window.displayWidget()->setFrameExchange(videoFrames);
  QTimer eventPump;
  eventPump.setInterval(8);
  QObject::connect(&eventPump, &QTimer::timeout, &window, [&worker, &window] {
    while (const auto event = worker.pollEvent()) {
      if (event->type == genplusgx::EmulationEventType::frameCompleted) {
        static_cast<void>(window.displayWidget()->presentLatestFrame());
      }
    }
  });
  eventPump.start();
  window.show();
  const int result = application.exec();
  static_cast<void>(worker.stop());
  return result;
}
