#include "genplusgx/app/platform_bootstrap.h"

#include <QByteArray>
#include <QtGlobal>

#include <string>
#include <system_error>

namespace genplusgx::app {

bool configureBundledLinuxQtPlatform(
  const std::filesystem::path& executablePath)
{
#if defined(Q_OS_LINUX)
  if (!qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") ||
      executablePath.empty()) {
    return false;
  }

  std::error_code error;
  auto absolutePath = std::filesystem::absolute(executablePath, error);
  if (error) {
    return false;
  }
  const auto canonicalPath = std::filesystem::weakly_canonical(
    absolutePath, error);
  if (!error) {
    absolutePath = canonicalPath;
  }

  const auto binaryDirectory = absolutePath.parent_path();
  const auto platformDirectory = binaryDirectory.parent_path() /
    "lib" / "qt6" / "plugins" / "platforms";
  error.clear();
  if (!std::filesystem::is_regular_file(binaryDirectory / "qt.conf", error) ||
      error ||
      !std::filesystem::is_regular_file(
        platformDirectory / "libqxcb.so", error) || error) {
    return false;
  }

  std::filesystem::directory_iterator entry{platformDirectory, error};
  const std::filesystem::directory_iterator end;
  while (!error && entry != end) {
    const auto filename = entry->path().filename().string();
    if (filename.starts_with("libqwayland") &&
        entry->is_regular_file(error) && !error) {
      return false;
    }
    entry.increment(error);
  }
  if (error) {
    return false;
  }

  return qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("xcb"));
#else
  static_cast<void>(executablePath);
  return false;
#endif
}

} // namespace genplusgx::app
