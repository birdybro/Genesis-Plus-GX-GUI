#include "genplusgx/app/command_line.h"

#include "genplusgx/version.h"

#include <QCoreApplication>

namespace genplusgx::app {

CommandLineOptions parseCommandLine(const QStringList& arguments)
{
  CommandLineOptions options;
  bool positionalOnly = false;
  for (qsizetype index = 0; index < arguments.size(); ++index) {
    const auto& argument = arguments[index];
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
    if (!positionalOnly && argument == QStringLiteral("--patch")) {
      if (options.patchPath) {
        options.valid = false;
        options.error = QCoreApplication::translate(
          "CommandLine", "Only one soft patch may be selected at startup.");
        return options;
      }
      if (index + 1 >= arguments.size()) {
        options.valid = false;
        options.error = QCoreApplication::translate(
          "CommandLine", "The --patch option requires a patch file path.");
        return options;
      }
      options.patchPath = arguments[++index];
      continue;
    }
    if (!positionalOnly && argument.startsWith(QStringLiteral("--patch="))) {
      if (options.patchPath) {
        options.valid = false;
        options.error = QCoreApplication::translate(
          "CommandLine", "Only one soft patch may be selected at startup.");
        return options;
      }
      options.patchPath = argument.sliced(8);
      if (options.patchPath->isEmpty()) {
        options.valid = false;
        options.error = QCoreApplication::translate(
          "CommandLine", "The --patch option requires a patch file path.");
        return options;
      }
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
  if (options.patchPath && !options.gamePath) {
    options.valid = false;
    options.error = QCoreApplication::translate(
      "CommandLine", "The --patch option requires a startup game file.");
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
    "  -f, --fullscreen  Start in fullscreen mode.\n"
    "      --patch FILE  Apply an IPS, BPS, or UPS soft patch.\n\n"
    "Arguments:\n"
    "  game              Game cartridge or disc image to open.")
    .arg(QString::fromLatin1(GENPLUSGX_APP_NAME),
      QString::fromLatin1(GENPLUSGX_VERSION));
}

} // namespace genplusgx::app
