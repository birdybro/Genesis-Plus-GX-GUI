#include "genplusgx/screenshots/screenshot_service.h"
#include "genplusgx/settings/screenshot_settings.h"

#include <QCoreApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::filesystem::path temporaryPath(const QTemporaryDir& directory)
{
#if defined(Q_OS_WIN)
  return std::filesystem::path{directory.path().toStdWString()};
#else
  return std::filesystem::path{directory.path().toUtf8().constData()};
#endif
}

QString pathToQString(const std::filesystem::path& path)
{
#if defined(Q_OS_WIN)
  return QString::fromStdWString(path.wstring());
#else
  return QString::fromUtf8(path.string());
#endif
}

bool writeText(const std::filesystem::path& path, std::string_view text)
{
  return genplusgx::writeFileAtomically(path,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(text.data()), text.size()},
    genplusgx::settings::ScreenshotSettingsStore::maximumFileBytes);
}

} // namespace

int main(int argc, char* argv[])
{
  QCoreApplication application{argc, argv};
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "Temporary directory was unavailable")) {
    return 1;
  }
  const auto root = temporaryPath(temporary);
  const auto output = root / "native-captures";
  const genplusgx::CoreVideoFrameInfo frame{
    .format = genplusgx::CorePixelFormat::rgb565,
    .width = 2U,
    .height = 2U,
    .frameNumber = 42U,
  };
  constexpr std::array<std::uint16_t, 4> pixels{0xf800U, 0x07e0U, 0x001fU, 0xffffU};
  const auto timestamp = std::chrono::system_clock::time_point{
    std::chrono::milliseconds{1'700'000'000'123LL}};
  const auto first = genplusgx::screenshots::writeNativeScreenshot(
    output, "Sonic: The / Hedgehog?", frame, pixels, timestamp);
  if (!check(first.status && std::filesystem::is_regular_file(first.path) &&
               first.path.extension() == ".png" &&
               first.path.filename().string().find("f42") != std::string::npos,
        "A deterministic native PNG was not written")) {
    return 2;
  }
  const QImage saved{pathToQString(first.path)};
  if (!check(!saved.isNull() && saved.size() == QSize{2, 2} &&
               saved.pixelColor(0, 0) == QColor{255, 0, 0} &&
               saved.pixelColor(1, 0) == QColor{0, 255, 0} &&
               saved.pixelColor(0, 1) == QColor{0, 0, 255} &&
               saved.pixelColor(1, 1) == QColor{255, 255, 255},
        "RGB565 pixels did not survive PNG conversion exactly")) {
    return 3;
  }
  const auto second = genplusgx::screenshots::writeNativeScreenshot(
    output, "Sonic: The / Hedgehog?", frame, pixels, timestamp);
  if (!check(second.status && second.path != first.path &&
               std::filesystem::is_regular_file(second.path) &&
               second.path.stem().string().ends_with("-1"),
        "Screenshot filename collisions were not resolved safely")) {
    return 4;
  }
  std::size_t regularFiles = 0U;
  for (const auto& entry : std::filesystem::directory_iterator{output}) {
    regularFiles += entry.is_regular_file() ? 1U : 0U;
    if (!check(entry.path().extension() == ".png",
          "A temporary screenshot file remained after commit")) {
      return 5;
    }
  }
  if (!check(regularFiles == 2U, "Screenshot output contained unexpected files")) {
    return 6;
  }

  auto invalidFrame = frame;
  invalidFrame.width = 0U;
  if (!check(
        genplusgx::screenshots::writeNativeScreenshot(
          output, "invalid", invalidFrame, pixels, timestamp)
              .status.error == genplusgx::screenshots::ScreenshotError::invalidFrame &&
          genplusgx::screenshots::writeNativeScreenshot(
            "relative", "invalid", frame, pixels, timestamp)
              .status.error ==
            genplusgx::screenshots::ScreenshotError::invalidDirectory,
        "Invalid screenshot inputs were accepted")) {
    return 7;
  }

  genplusgx::screenshots::ScreenshotService service{2U, 4U};
  if (!check(
        service
              .request(1U, output, "not-running", frame, {pixels.begin(), pixels.end()})
              .error == genplusgx::screenshots::ScreenshotError::notRunning &&
          service.start() &&
          service.start().error ==
            genplusgx::screenshots::ScreenshotError::alreadyRunning,
        "Screenshot worker lifecycle validation failed")) {
    return 8;
  }
  const auto started = service.waitForEvent(std::chrono::seconds{2});
  if (!check(started && started->type ==
                          genplusgx::screenshots::ScreenshotEventType::serviceStarted,
        "Screenshot worker did not announce startup")) {
    return 9;
  }
  if (!check(service.request(
               71U, output, "worker capture", frame, {pixels.begin(), pixels.end()}),
        "Screenshot worker rejected a valid request")) {
    return 10;
  }
  const auto completed = service.waitForEvent(std::chrono::seconds{5});
  if (!check(completed && completed->operationId == 71U && completed->succeeded() &&
               std::filesystem::is_regular_file(completed->path),
        "Screenshot worker did not complete its bounded request")) {
    return 11;
  }
  if (!check(service.stop(), "Screenshot worker did not stop cleanly")) {
    return 12;
  }
  const auto stopped = service.pollEvent();
  if (!check(stopped &&
               stopped->type ==
                 genplusgx::screenshots::ScreenshotEventType::serviceStopped &&
               service.start() && service.stop(),
        "Screenshot worker did not report stop or support restart")) {
    return 13;
  }

  const auto defaultDirectory = root / "default-screenshots";
  genplusgx::settings::ScreenshotSettingsStore settingsStore{
    root / "config" / "screenshot-settings.json", defaultDirectory};
  const auto missing = settingsStore.load();
  if (!check(missing.status && !missing.migrated &&
               missing.settings.directory == defaultDirectory,
        "Missing screenshot settings did not use the platform default")) {
    return 14;
  }
  const genplusgx::settings::ScreenshotSettings custom{
    .directory = root / "custom-captures"};
  if (!check(settingsStore.save(custom) && settingsStore.load().settings == custom &&
               !settingsStore.save({.directory = "relative"}),
        "Screenshot settings did not validate and round-trip")) {
    return 15;
  }
  const auto legacyDirectory = root / "legacy-captures";
  const auto legacyData = QJsonDocument{
    QJsonObject{
      {QStringLiteral("schemaVersion"), 0},
      {QStringLiteral("screenshotDirectory"), pathToQString(legacyDirectory)},
    }}.toJson(QJsonDocument::Compact);
  if (!check(writeText(settingsStore.path(),
               std::string_view{
                 legacyData.constData(), static_cast<std::size_t>(legacyData.size())}),
        "Legacy screenshot settings could not be staged")) {
    return 16;
  }
  const auto migrated = settingsStore.load();
  if (!check(migrated.status && migrated.migrated &&
               migrated.settings.directory == legacyDirectory,
        "Schema-zero screenshot settings did not migrate")) {
    return 17;
  }
  if (!check(writeText(settingsStore.path(), "{broken") &&
               !settingsStore.load().status &&
               settingsStore.load().settings.directory == defaultDirectory &&
               writeText(settingsStore.path(), "{\"schemaVersion\":999}") &&
               !settingsStore.load().status,
        "Corrupt or future screenshot settings did not fail closed")) {
    return 18;
  }
  return 0;
}
