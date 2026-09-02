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
  snapshot.applicationDataMode = "Portable";
  snapshot.requestedInterfaceLanguage = "system";
  snapshot.effectiveInterfaceLanguage = "en";
  snapshot.interfaceLanguageFallback = true;
  snapshot.renderer = "OpenGL";
  snapshot.presentationSync =
    "Adaptive requested; effective swap interval -1";
  snapshot.presentationBuffering =
    "Double buffer requested; Double buffer effective";
  snapshot.videoArtwork = "Overlay active";
  snapshot.videoArtworkFormat = "png";
  snapshot.videoArtworkBytes = 4'096U;
  snapshot.videoArtworkWidth = 1'920U;
  snapshot.videoArtworkHeight = 1'080U;
  snapshot.videoPublishedFrames = 600U;
  snapshot.videoCopiedFrames = 590U;
  snapshot.videoSkippedFrames = 10U;
  snapshot.videoProducerDrops = 0U;
  snapshot.videoRenderedFrames = 580U;
  snapshot.videoSwappedFrames = 580U;
  snapshot.videoCoalescedFrames = 10U;
  snapshot.videoDuplicateRenders = 2U;
  snapshot.videoMaximumPendingFrames = 1U;
  snapshot.measuredPresentationFramesPerSecond = 59.923;
  snapshot.averageSwapIntervalMicroseconds = 16'688U;
  snapshot.maximumSwapIntervalMicroseconds = 18'000U;
  snapshot.audioDevice = "Default";
  snapshot.loadedGame = "/users/alex/private.bin";
  snapshot.loadedSystem = "Genesis";
  snapshot.loadedRegion = "USA password=hunter2";
  snapshot.rewindEnabled = true;
  snapshot.rewinding = true;
  snapshot.rewindSnapshots = 12U;
  snapshot.rewindPayloadBytes = 12U * 1024U * 1024U;
  snapshot.rewindMemoryLimitBytes = 128U * 1024U * 1024U;
  snapshot.runAheadEnabled = true;
  snapshot.runAheadSupported = true;
  snapshot.runAheadActive = true;
  snapshot.runAheadVerified = true;
  snapshot.runAheadFrames = 2U;
  snapshot.runAheadSpeculativeFrames = 288U;
  snapshot.runAheadRollbacks = 144U;
  snapshot.runAheadStateBytes = 384U * 1024U;
  snapshot.runAheadStateCapacityBytes = 512U * 1024U;
  snapshot.netplayState = "Authenticated peer connected";
  snapshot.netplaySentPackets = 360U;
  snapshot.netplayReceivedPackets = 359U;
  snapshot.netplaySentBytes = 21'600U;
  snapshot.netplayReceivedBytes = 21'540U;
  snapshot.netplayAuthenticationFailures = 1U;
  snapshot.netplayProtocolFailures = 2U;
  snapshot.netplayQueuedFrames = 3U;
  snapshot.netplayQueueCapacity = 256U;
  snapshot.netplayPredictedFrames = 41U;
  snapshot.netplayRollbackRequests = 4U;
  snapshot.netplayRollbacks = 4U;
  snapshot.netplayHistoryFrames = 9U;
  snapshot.netplayHistoryBytes = 786'432U;
  snapshot.achievementsState = "Authenticated; recognized game active";
  snapshot.achievementsHardcore = true;
  snapshot.achievementRequestQueueDepth = 1U;
  snapshot.achievementResponseQueueDepth = 2U;
  snapshot.achievementQueueCapacity = 32U;
  snapshot.rejectedAchievementRequests = 3U;
  snapshot.rejectedAchievementResponses = 4U;
  snapshot.activeAchievementRequests = 1U;
  snapshot.completedAchievementRequests = 8U;
  snapshot.failedAchievementRequests = 2U;
  snapshot.lastAchievementNetworkError = "token=must-not-appear";
  snapshot.cloudSyncState = "Synchronizing automatically";
  snapshot.cloudSyncSaves = true;
  snapshot.cloudSyncStates = true;
  snapshot.cloudSyncOnStartup = true;
  snapshot.cloudSyncOnGameClose = true;
  snapshot.signedUpdateState = "Available; idle";
  snapshot.automaticUpdateChecks = true;
  snapshot.updateSigningKeyId = "704e04b184a939a4";
  snapshot.highestVerifiedUpdate = "1.2.3";
  snapshot.lastUpdateCheckUtc = "2026-09-02T18:00:00.000Z";
  snapshot.recordingActive = true;
  snapshot.recordingQueuedFrames = 2U;
  snapshot.recordingQueueCapacity = 8U;
  snapshot.recordingWrittenFrames = 144U;
  snapshot.recordingDroppedFrames = 1U;
  snapshot.recordingOutputBytes = 4'096U;
  snapshot.normalSpeedPercent = 125U;
  snapshot.slowMotionSpeedPercent = 50U;
  snapshot.fastForwardSpeedPercent = 800U;
  snapshot.activeSpeedPercent = 50U;
  snapshot.slowMotion = true;
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
               report.find("Application data mode: Portable") != std::string::npos &&
               report.find("Requested interface language: system") != std::string::npos &&
               report.find("Effective interface language: en") != std::string::npos &&
               report.find("Interface language fallback: Yes") != std::string::npos &&
               report.find("Presentation sync: Adaptive") != std::string::npos &&
               report.find("Video artwork: Overlay active") != std::string::npos &&
               report.find("Video artwork dimensions: 1920 x 1080") != std::string::npos &&
               report.find("Video exchange: 590 copied / 600 published, 10 skipped, 0 producer drops") != std::string::npos &&
               report.find("Video presentation: 580 rendered, 580 swapped, 10 coalesced, 2 duplicate renders") != std::string::npos &&
               report.find("Video pending frames: 0 / 1 maximum") != std::string::npos &&
               report.find("Sega CD USA: Valid") != std::string::npos &&
               report.find("Rewind: Enabled (active)") != std::string::npos &&
               report.find("Rewind snapshots: 12") != std::string::npos &&
               report.find("Run-ahead: Enabled (active)") != std::string::npos &&
               report.find("Run-ahead determinism: Verified") != std::string::npos &&
               report.find("Run-ahead speculative frames: 288") != std::string::npos &&
               report.find("Run-ahead state bytes: 393216 / 524288") != std::string::npos &&
               report.find("Netplay: Authenticated peer connected") != std::string::npos &&
               report.find("Netplay packets: 360 sent / 359 received") != std::string::npos &&
               report.find("Netplay output queue: 3 / 256") != std::string::npos &&
               report.find("Netplay prediction/rollback: 41 predicted, 4 requested, 4 performed") != std::string::npos &&
               report.find("Netplay rollback history: 9 frames, 786432 bytes") != std::string::npos &&
               report.find("RetroAchievements: Authenticated; recognized game active (Hardcore active)") != std::string::npos &&
               report.find("RetroAchievements bridge: 1 requests / 2 responses / 32 capacity") != std::string::npos &&
               report.find("RetroAchievements HTTPS: 1 active, 8 completed, 2 failed") != std::string::npos &&
               report.find("Signed updates: Available; idle") != std::string::npos &&
               report.find("Update signing key: 704e04b184a939a4") != std::string::npos &&
               report.find("Last update attempt: 2026-09-02T18:00:00.000Z") != std::string::npos &&
               report.find("Cloud synchronization: Synchronizing automatically") != std::string::npos &&
               report.find("Cloud content: saves enabled, states enabled") != std::string::npos &&
               report.find("Cloud automatic sync: startup enabled, game close enabled") != std::string::npos &&
               report.find("Lossless recording: Active") != std::string::npos &&
               report.find("Recording queue: 2 / 8") != std::string::npos &&
               report.find("Recording frames: 144 written, 1 dropped") != std::string::npos &&
               report.find("Configured speeds: Normal 125%, slow motion 50%, fast forward 800%") != std::string::npos &&
               report.find("Active speed: 50% (slow motion)") != std::string::npos &&
               report.find("/users/alex") == std::string::npos &&
               report.find("hunter2") == std::string::npos &&
               report.find("must-not-appear") == std::string::npos &&
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
