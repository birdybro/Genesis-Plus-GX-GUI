#include "genplusgx/diagnostics/diagnostics.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::string readText(const std::filesystem::path& path)
{
  std::ifstream stream{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

bool validateJsonLines(const std::string& text)
{
  std::size_t start = 0U;
  while (start < text.size()) {
    const auto end = text.find('\n', start);
    const auto length = (end == std::string::npos ? text.size() : end) - start;
    if (length != 0U) {
      const auto document = QJsonDocument::fromJson(
        QByteArray{text.data() + static_cast<std::ptrdiff_t>(start),
          static_cast<qsizetype>(length)});
      if (!document.isObject()) {
        return false;
      }
      const auto object = document.object();
      if (!object.value(QStringLiteral("timestamp")).isString() ||
          !object.value(QStringLiteral("level")).isString() ||
          !object.value(QStringLiteral("category")).isString() ||
          !object.value(QStringLiteral("message")).isString()) {
        return false;
      }
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1U;
  }
  return true;
}

} // namespace

int main()
{
  using namespace genplusgx::diagnostics;
  const auto redacted = redactSensitiveText(
    "/users/alex/game.bin token=abc123 password:letmein", "/users/alex");
  if (!check(redacted.find("/users/alex") == std::string::npos &&
               redacted.find("abc123") == std::string::npos &&
               redacted.find("letmein") == std::string::npos &&
               redacted.find("<home>") != std::string::npos &&
               redacted.find("<redacted>") != std::string::npos,
        "Sensitive diagnostic text was not redacted")) {
    return 1;
  }

  auto snapshot = staticDiagnosticsSnapshot();
  snapshot.renderer = "OpenGL";
  snapshot.audioDevice = "Default";
  snapshot.loadedGame = "/users/alex/private.bin";
  snapshot.loadedSystem = "Genesis";
  snapshot.loadedRegion = "USA password=hunter2";
  snapshot.rewindEnabled = true;
  snapshot.rewinding = true;
  snapshot.rewindSnapshots = 12U;
  snapshot.rewindPayloadBytes = 12U * 1024U * 1024U;
  snapshot.rewindMemoryLimitBytes = 128U * 1024U * 1024U;
  snapshot.loggerActive = true;
  snapshot.bios.push_back({
    .name = "Sega CD USA",
    .status = "Valid",
    .sha256Prefix = "0123456789ab",
  });
  const auto report = formatDiagnostics(snapshot, "/users/alex");
  if (!check(!snapshot.applicationName.empty() && !snapshot.version.empty() &&
               !snapshot.qtVersion.empty() && !snapshot.sdlVersion.empty() &&
               report.find("OpenGL") != std::string::npos &&
               report.find("Sega CD USA: Valid") != std::string::npos &&
               report.find("Rewind: Enabled (active)") != std::string::npos &&
               report.find("Rewind snapshots: 12") != std::string::npos &&
               report.find("/users/alex") == std::string::npos &&
               report.find("hunter2") == std::string::npos &&
               report.find("Privacy:") != std::string::npos,
        "The diagnostics report was incomplete or exposed private data")) {
    return 2;
  }

  QTemporaryDir directory;
  if (!check(directory.isValid(), "Temporary log directory was unavailable")) {
    return 3;
  }
  const std::filesystem::path root{directory.path().toStdString()};
  FrontendLogger invalid;
  if (!check(!invalid.initialize("relative.log"),
        "A relative frontend log path was accepted")) {
    return 4;
  }

  const auto logPath = root / "frontend.jsonl";
  FrontendLogger logger;
  const auto initialized = logger.initialize(logPath, 1'024U, 2U);
  if (!check(initialized && logger.ready() && logger.install(),
        "The frontend logger could not initialize and install")) {
    return 5;
  }
  qInfo().noquote() << "handler token=super-secret";
  for (int index = 0; index < 80; ++index) {
    logger.write(LogLevel::information,
      "unit.diagnostics",
      "bounded message password=hidden-value sequence=" + std::to_string(index));
  }
  qInfo().noquote() << "handler-final authorization=never-expose";
  const auto metrics = logger.metrics();
  logger.shutdown();
  if (!check(metrics.writtenMessages > 0U && metrics.rotations > 0U &&
               metrics.redactedMessages > 0U && metrics.currentBytes <= 1'024U,
        "Logger metrics did not report bounded writes, redaction, and rotation")) {
    return 6;
  }

  std::vector<std::filesystem::path> paths{
    logPath, root / "frontend.jsonl.1", root / "frontend.jsonl.2"};
  bool sawContent = false;
  bool sawHandlerMessage = false;
  for (const auto& path : paths) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
      continue;
    }
    if (!check(std::filesystem::file_size(path, error) <= 1'024U,
          "A rotated frontend log exceeded its configured bound")) {
      return 7;
    }
    const auto text = readText(path);
    sawContent = sawContent || !text.empty();
    sawHandlerMessage =
      sawHandlerMessage || text.find("handler-final") != std::string::npos;
    if (!check(validateJsonLines(text),
          "A frontend log did not contain valid structured JSON lines") ||
        !check(text.find("super-secret") == std::string::npos &&
                 text.find("hidden-value") == std::string::npos &&
                 text.find("never-expose") == std::string::npos,
          "A rotated frontend log exposed a credential-like value")) {
      return 8;
    }
  }
  return check(sawContent && sawHandlerMessage,
           "The installed Qt handler did not write bounded frontend log content")
           ? 0
           : 9;
}
