#include "genplusgx/ui/main_window.h"
#include "genplusgx/version.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>

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

  genplusgx::ui::MainWindow window;
  window.show();
  return application.exec();
}
