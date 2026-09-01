#include "genplusgx/app/command_line.h"
#include "genplusgx/app/platform_bootstrap.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

bool check(bool condition, std::string_view message)
{
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

} // namespace

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  bool passed = true;

  const auto empty = genplusgx::app::parseCommandLine({});
  passed &= check(empty.valid && !empty.gamePath && !empty.fullscreen &&
      !empty.portable,
    "an empty command line uses normal windowed startup");

  const auto startup = genplusgx::app::parseCommandLine(
    {QStringLiteral("--fullscreen"), QStringLiteral("game.md")});
  passed &= check(startup.valid && startup.fullscreen && startup.gamePath &&
      *startup.gamePath == QStringLiteral("game.md"),
    "fullscreen and a positional game parse together");

  const auto portable = genplusgx::app::parseCommandLine(
    {QStringLiteral("--portable"), QStringLiteral("game.md")});
  passed &= check(portable.valid && portable.portable && portable.gamePath &&
      *portable.gamePath == QStringLiteral("game.md"),
    "portable mode is an explicit option and retains the startup game");
  const auto repeatedPortable = genplusgx::app::parseCommandLine(
    {QStringLiteral("--portable"), QStringLiteral("--portable")});
  passed &= check(repeatedPortable.valid && repeatedPortable.portable,
    "repeating the stateless portable option is harmless");

  const auto patched = genplusgx::app::parseCommandLine(
    {QStringLiteral("--patch"), QStringLiteral("translation.bps"),
      QStringLiteral("game.md")});
  passed &= check(patched.valid && patched.patchPath &&
      *patched.patchPath == QStringLiteral("translation.bps") &&
      patched.gamePath && *patched.gamePath == QStringLiteral("game.md"),
    "an explicit soft patch parses with its startup game");
  const auto equalsPatch = genplusgx::app::parseCommandLine(
    {QStringLiteral("--patch=fix.ips"), QStringLiteral("game.md")});
  passed &= check(equalsPatch.valid && equalsPatch.patchPath &&
      *equalsPatch.patchPath == QStringLiteral("fix.ips"),
    "the equals form of the patch option parses");
  const auto orphanPatch = genplusgx::app::parseCommandLine(
    {QStringLiteral("--patch"), QStringLiteral("fix.ups")});
  passed &= check(!orphanPatch.valid &&
      orphanPatch.error.contains(QStringLiteral("startup game")),
    "a patch without a game is rejected");
  const auto missingPatchValue = genplusgx::app::parseCommandLine(
    {QStringLiteral("--patch")});
  passed &= check(!missingPatchValue.valid &&
      missingPatchValue.error.contains(QStringLiteral("requires")),
    "a missing patch option value is rejected");
  const auto duplicatePatch = genplusgx::app::parseCommandLine(
    {QStringLiteral("--patch=one.ips"), QStringLiteral("--patch=two.bps"),
      QStringLiteral("game.md")});
  passed &= check(!duplicatePatch.valid &&
      duplicatePatch.error.contains(QStringLiteral("Only one")),
    "multiple startup patches are rejected explicitly");

  const auto shortOptions = genplusgx::app::parseCommandLine(
    {QStringLiteral("-f"), QStringLiteral("-h")});
  passed &= check(shortOptions.valid && shortOptions.fullscreen && shortOptions.showHelp,
    "documented short options parse");

  const auto dashedPath = genplusgx::app::parseCommandLine(
    {QStringLiteral("--"), QStringLiteral("-game.md")});
  passed &= check(dashedPath.valid && dashedPath.gamePath &&
      *dashedPath.gamePath == QStringLiteral("-game.md"),
    "the option terminator permits a dashed filename");

  const auto unknown = genplusgx::app::parseCommandLine(
    {QStringLiteral("--turbo-mode")});
  passed &= check(!unknown.valid && unknown.error.contains(QStringLiteral("Unknown")),
    "unknown options produce a useful diagnostic");

  const auto multiple = genplusgx::app::parseCommandLine(
    {QStringLiteral("one.md"), QStringLiteral("two.md")});
  passed &= check(!multiple.valid && multiple.error.contains(QStringLiteral("one game")),
    "multiple startup files are rejected explicitly");

  const auto help = genplusgx::app::commandLineHelp();
  passed &= check(help.contains(QStringLiteral("--fullscreen")) &&
      help.contains(QStringLiteral("--portable")) &&
      help.contains(QStringLiteral("--patch FILE")) &&
      help.contains(QStringLiteral("[game]")),
    "help documents startup behavior");

  QTemporaryDir packageRoot;
  passed &= check(packageRoot.isValid(),
    "a temporary package root is available");
  const auto binaryDirectory = packageRoot.path() + QStringLiteral("/bin");
  const auto platformDirectory =
    packageRoot.path() + QStringLiteral("/lib/qt6/plugins/platforms");
  passed &= check(QDir{}.mkpath(binaryDirectory) &&
      QDir{}.mkpath(platformDirectory),
    "the temporary Linux package layout can be created");
  QFile qtConfiguration{binaryDirectory + QStringLiteral("/qt.conf")};
  QFile xcbPlugin{platformDirectory + QStringLiteral("/libqxcb.so")};
  passed &= check(qtConfiguration.open(QIODevice::WriteOnly) &&
      qtConfiguration.write("[Paths]\n") > 0 && qtConfiguration.flush() &&
      xcbPlugin.open(QIODevice::WriteOnly) && xcbPlugin.flush(),
    "the temporary Linux package markers can be written");
  qtConfiguration.close();
  xcbPlugin.close();

  const auto previousPlatform = qgetenv("QT_QPA_PLATFORM");
  const bool platformWasSet = qEnvironmentVariableIsSet("QT_QPA_PLATFORM");
  qunsetenv("QT_QPA_PLATFORM");
  const auto executablePath = std::filesystem::path{
    binaryDirectory.toStdString()} / "genesis-plus-gx-gui";
#if defined(Q_OS_LINUX)
  passed &= check(
    genplusgx::app::configureBundledLinuxQtPlatform(executablePath) &&
      qgetenv("QT_QPA_PLATFORM") == QByteArrayLiteral("xcb"),
    "an XCB-only relocatable Linux package selects its bundled backend");
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  passed &= check(
    !genplusgx::app::configureBundledLinuxQtPlatform(executablePath) &&
      qgetenv("QT_QPA_PLATFORM") == QByteArrayLiteral("offscreen"),
    "an explicit Qt platform selection is preserved");
  qunsetenv("QT_QPA_PLATFORM");
  QFile waylandPlugin{
    platformDirectory + QStringLiteral("/libqwayland-egl.so")};
  passed &= check(waylandPlugin.open(QIODevice::WriteOnly) &&
      waylandPlugin.flush(),
    "the temporary Wayland plugin marker can be written");
  waylandPlugin.close();
  passed &= check(
    !genplusgx::app::configureBundledLinuxQtPlatform(executablePath) &&
      qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"),
    "a package with a native Wayland backend keeps Qt auto-selection");
#else
  passed &= check(
    !genplusgx::app::configureBundledLinuxQtPlatform(executablePath),
    "Linux package selection is inert on other operating systems");
#endif
  if (platformWasSet) {
    qputenv("QT_QPA_PLATFORM", previousPlatform);
  } else {
    qunsetenv("QT_QPA_PLATFORM");
  }
  return passed ? 0 : 1;
}
