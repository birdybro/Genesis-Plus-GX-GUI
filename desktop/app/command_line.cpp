#include "genplusgx/app/command_line.h"

#include "genplusgx/version.h"

#include <QCoreApplication>

namespace genplusgx::app {

CommandLineOptions parseCommandLine(const QStringList& arguments)
{
  CommandLineOptions options;
  bool positionalOnly = false;
  for (const auto& argument : arguments) {
    if (!positionalOnly && argument == QStringLiteral("--")) {
      positionalOnly = true;
      continue;
    }
    if (!positionalOnly &&
        (argument == QStringLiteral("--help") || argument == QStringLiteral("-h"))) {
      options.showHelp = true;
      continue;
    }
    if (!positionalOnly &&
        (argument == QStringLiteral("--version") || argument == QStringLiteral("-v"))) {
      options.showVersion = true;
      continue;
    }
    if (!positionalOnly &&
        (argument == QStringLiteral("--fullscreen") || argument == QStringLiteral("-f"))) {
      options.fullscreen = true;
      continue;
    }
    if (!positionalOnly && argument.startsWith(u'-')) {
      options.valid = false;
      options.error = QCoreApplication::translate(
        "CommandLine", "Unknown option: %1").arg(argument);
      return options;
    }
    if (options.gamePath) {
      options.valid = false;
      options.error = QCoreApplication::translate(
        "CommandLine", "Only one game file may be opened at startup.");
      return options;
    }
    options.gamePath = argument;
  }
  return options;
}

QString commandLineHelp()
{
  return QCoreApplication::translate(
    "CommandLine",
    "%1 %2\n"
    "Native desktop frontend for Genesis Plus GX.\n\n"
    "Usage: genesis-plus-gx-gui [options] [game]\n\n"
    "Options:\n"
    "  -h, --help        Show this help text.\n"
    "  -v, --version     Show application version.\n"
    "  -f, --fullscreen  Start in fullscreen mode.\n\n"
    "Arguments:\n"
    "  game              Game cartridge or disc image to open.")
    .arg(QString::fromLatin1(GENPLUSGX_APP_NAME),
      QString::fromLatin1(GENPLUSGX_VERSION));
}

} // namespace genplusgx::app
