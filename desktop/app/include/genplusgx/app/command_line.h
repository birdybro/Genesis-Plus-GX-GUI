#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace genplusgx::app {

struct CommandLineOptions final {
  bool valid{true};
  bool showHelp{false};
  bool showVersion{false};
  bool fullscreen{false};
  std::optional<QString> gamePath;
  std::optional<QString> patchPath;
  QString error;
};

[[nodiscard]] CommandLineOptions parseCommandLine(const QStringList& arguments);
[[nodiscard]] QString commandLineHelp();

} // namespace genplusgx::app
