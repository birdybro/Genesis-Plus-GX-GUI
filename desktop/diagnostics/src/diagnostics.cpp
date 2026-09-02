#include "genplusgx/diagnostics/diagnostics.h"

#include "genplusgx/version.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QSysInfo>
#include <QtGlobal>

#include <SDL3/SDL_version.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <system_error>
#include <utility>

namespace genplusgx::diagnostics {
namespace {

std::atomic<FrontendLogger*> activeLogger{nullptr};
std::atomic<QtMessageHandler> previousMessageHandler{nullptr};

QString fromUtf8(std::string_view value)
{
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::string toUtf8(const QString& value)
{
  const auto utf8 = value.toUtf8();
  return {utf8.constData(), static_cast<std::size_t>(utf8.size())};
}

std::string_view levelName(LogLevel level)
{
  switch (level) {
    case LogLevel::debug:
      return "debug";
    case LogLevel::information:
      return "information";
    case LogLevel::warning:
      return "warning";
    case LogLevel::critical:
      return "critical";
    case LogLevel::fatal:
      return "fatal";
  }
  return "unknown";
}

LogLevel logLevel(QtMsgType type)
{
  switch (type) {
    case QtDebugMsg:
      return LogLevel::debug;
    case QtInfoMsg:
      return LogLevel::information;
    case QtWarningMsg:
      return LogLevel::warning;
    case QtCriticalMsg:
      return LogLevel::critical;
    case QtFatalMsg:
      return LogLevel::fatal;
  }
  return LogLevel::warning;
}

std::filesystem::path backupPath(const std::filesystem::path& path, std::size_t index)
{
  auto result = path;
  result += "." + std::to_string(index);
  return result;
}

std::string safeLine(std::string_view value, std::string_view homeDirectory)
{
  auto result = redactSensitiveText(value, homeDirectory);
  for (auto& character : result) {
    if (character == '\r' || character == '\n' || character == '\t') {
      character = ' ';
    }
  }
  return result.empty() ? "Not available" : result;
}

void qtMessageHandler(
  QtMsgType type, const QMessageLogContext& context, const QString& message)
{
  if (auto* logger = activeLogger.load(std::memory_order_acquire)) {
    const auto category = context.category != nullptr
                            ? std::string_view{context.category}
                            : std::string_view{"default"};
    logger->write(logLevel(type), category, toUtf8(message));
  }

  if (const auto previous = previousMessageHandler.load(std::memory_order_acquire)) {
    previous(type, context, message);
  } else {
    const auto local = message.toLocal8Bit();
    std::fprintf(stderr, "%s\n", local.constData());
  }
}

} // namespace

class FrontendLogger::Private final {
public:
  LoggerStatus initialize(std::filesystem::path path,
    std::uintmax_t maximumFileBytes,
    std::size_t backupCount)
  {
    std::scoped_lock lock{mutex};
    if (path.empty() || !path.is_absolute() || maximumFileBytes < 1'024U ||
        maximumFileBytes > 64U * 1024U * 1024U || backupCount > 10U) {
      return {
        .error = LoggerError::invalidConfiguration,
        .message = "The frontend logger configuration is invalid.",
      };
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
      return {
        .error = LoggerError::directoryCreationFailed,
        .message = "The frontend log directory could not be created.",
      };
    }
    path_ = std::move(path);
    maximumFileBytes_ = maximumFileBytes;
    backupCount_ = backupCount;
    const auto existingBytes = std::filesystem::file_size(path_, error);
    if (!error && existingBytes >= maximumFileBytes_ && !rotateLocked()) {
      return {
        .error = LoggerError::fileOpenFailed,
        .message = "The full frontend log could not be rotated.",
      };
    }
    error.clear();
    if (!stream_.is_open()) {
      stream_.clear();
      stream_.open(path_, std::ios::binary | std::ios::app);
    }
    if (!stream_) {
      return {
        .error = LoggerError::fileOpenFailed,
        .message = "The frontend log file could not be opened.",
      };
    }
    currentBytes_ = std::filesystem::file_size(path_, error);
    if (error) {
      currentBytes_ = 0U;
    }
    ready_ = true;
    return {};
  }

  void write(LogLevel level, std::string_view category, std::string_view message)
  {
    const auto home = toUtf8(QDir::homePath());
    const auto redactedCategory = redactSensitiveText(category, home);
    const auto redactedMessage = redactSensitiveText(message, home);
    const bool redacted = redactedCategory != category || redactedMessage != message;
    const auto timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    auto line = QJsonDocument{
      QJsonObject{
        {QStringLiteral("timestamp"), timestamp},
        {QStringLiteral("level"), fromUtf8(levelName(level))},
        {QStringLiteral("category"), fromUtf8(redactedCategory)},
        {QStringLiteral("message"), fromUtf8(redactedMessage)},
      }}.toJson(QJsonDocument::Compact);
    line.append('\n');

    std::scoped_lock lock{mutex};
    if (!ready_) {
      ++droppedMessages_;
      return;
    }
    const auto lineBytes = static_cast<std::uintmax_t>(line.size());
    if (lineBytes > maximumFileBytes_ ||
        (currentBytes_ + lineBytes > maximumFileBytes_ && !rotateLocked())) {
      ++droppedMessages_;
      return;
    }
    stream_.write(line.constData(), static_cast<std::streamsize>(line.size()));
    stream_.flush();
    if (!stream_) {
      stream_.clear();
      ++droppedMessages_;
      return;
    }
    currentBytes_ += lineBytes;
    ++writtenMessages_;
    if (redacted) {
      ++redactedMessages_;
    }
  }

  bool rotateLocked()
  {
    stream_.close();
    std::error_code error;
    if (backupCount_ == 0U) {
      std::filesystem::remove(path_, error);
      if (error) {
        return false;
      }
    } else {
      std::filesystem::remove(backupPath(path_, backupCount_), error);
      error.clear();
      for (std::size_t index = backupCount_; index > 1U; --index) {
        const auto source = backupPath(path_, index - 1U);
        if (!std::filesystem::exists(source, error)) {
          error.clear();
          continue;
        }
        std::filesystem::rename(source, backupPath(path_, index), error);
        if (error) {
          return false;
        }
      }
      if (std::filesystem::exists(path_, error)) {
        error.clear();
        std::filesystem::rename(path_, backupPath(path_, 1U), error);
        if (error) {
          return false;
        }
      }
    }
    stream_.open(path_, std::ios::binary | std::ios::trunc);
    if (!stream_) {
      return false;
    }
    currentBytes_ = 0U;
    ++rotations_;
    return true;
  }

  mutable std::mutex mutex;
  std::filesystem::path path_;
  std::ofstream stream_;
  std::uintmax_t maximumFileBytes_{FrontendLogger::defaultMaximumFileBytes};
  std::size_t backupCount_{FrontendLogger::defaultBackupCount};
  std::uint64_t writtenMessages_{0};
  std::uint64_t droppedMessages_{0};
  std::uint64_t redactedMessages_{0};
  std::uint64_t rotations_{0};
  std::uintmax_t currentBytes_{0};
  bool ready_{false};
  bool installed_{false};
};

std::string redactSensitiveText(std::string_view text, std::string_view homeDirectory)
{
  auto result = fromUtf8(text);
  if (!homeDirectory.empty()) {
    result.replace(fromUtf8(homeDirectory), QStringLiteral("<home>"));
  }
  static const QRegularExpression credential{QStringLiteral(
    R"re((?i)\b(password|token|secret|authorization|api[_-]?key)\b\s*[:=]\s*[^\s,;]+)re")};
  result.replace(credential, QStringLiteral("\\1=<redacted>"));
  static const QRegularExpression absolutePath{
    QStringLiteral(R"re((?:(?:[A-Za-z]:[\\/])|/)[^\s,;]+)re")};
  result.replace(absolutePath, QStringLiteral("<path>"));
  return toUtf8(result);
}

FrontendLogger::FrontendLogger() : private_(std::make_unique<Private>()) {}

FrontendLogger::~FrontendLogger() { shutdown(); }

LoggerStatus FrontendLogger::initialize(
  std::filesystem::path path, std::uintmax_t maximumFileBytes, std::size_t backupCount)
{
  return private_->initialize(std::move(path), maximumFileBytes, backupCount);
}

bool FrontendLogger::install()
{
  std::scoped_lock lock{private_->mutex};
  if (!private_->ready_ || private_->installed_ ||
      activeLogger.load(std::memory_order_acquire) != nullptr) {
    return false;
  }
  activeLogger.store(this, std::memory_order_release);
  const auto previous = qInstallMessageHandler(qtMessageHandler);
  previousMessageHandler.store(previous, std::memory_order_release);
  private_->installed_ = true;
  return true;
}

void FrontendLogger::shutdown() noexcept
{
  std::scoped_lock lock{private_->mutex};
  if (private_->installed_) {
    activeLogger.store(nullptr, std::memory_order_release);
    qInstallMessageHandler(
      previousMessageHandler.exchange(nullptr, std::memory_order_acq_rel));
    private_->installed_ = false;
  }
  private_->stream_.flush();
  private_->stream_.close();
  private_->ready_ = false;
}

void FrontendLogger::write(
  LogLevel level, std::string_view category, std::string_view message)
{
  private_->write(level, category, message);
}

bool FrontendLogger::ready() const noexcept
{
  std::scoped_lock lock{private_->mutex};
  return private_->ready_;
}

bool FrontendLogger::installed() const noexcept
{
  std::scoped_lock lock{private_->mutex};
  return private_->installed_;
}

LoggerMetrics FrontendLogger::metrics() const noexcept
{
  std::scoped_lock lock{private_->mutex};
  return {
    .writtenMessages = private_->writtenMessages_,
    .droppedMessages = private_->droppedMessages_,
    .redactedMessages = private_->redactedMessages_,
    .rotations = private_->rotations_,
    .currentBytes = private_->currentBytes_,
  };
}

DiagnosticsSnapshot staticDiagnosticsSnapshot()
{
  DiagnosticsSnapshot snapshot;
  snapshot.applicationName = GENPLUSGX_APP_NAME;
  snapshot.version = GENPLUSGX_VERSION;
  snapshot.gitCommit = GENPLUSGX_GIT_COMMIT;
  snapshot.qtVersion = qVersion();
  snapshot.sdlVersion = std::to_string(SDL_MAJOR_VERSION) + "." +
    std::to_string(SDL_MINOR_VERSION) + "." +
    std::to_string(SDL_MICRO_VERSION);
  snapshot.operatingSystem = toUtf8(QSysInfo::prettyProductName());
  snapshot.architecture = toUtf8(QSysInfo::currentCpuArchitecture());
  return snapshot;
}

std::string formatDiagnostics(
  const DiagnosticsSnapshot& snapshot, std::string_view homeDirectory)
{
  const auto detectedHome =
    homeDirectory.empty() ? toUtf8(QDir::homePath()) : std::string{homeDirectory};
  std::ostringstream output;
  const auto field = [&output, &detectedHome](
                       std::string_view name, std::string_view value) {
    output << name << ": " << safeLine(value, detectedHome) << '\n';
  };
  output << "Genesis Plus GX GUI Diagnostics\n\n";
  field("Application", snapshot.applicationName);
  field("Version", snapshot.version);
  field("Git commit", snapshot.gitCommit);
  field("Qt", snapshot.qtVersion);
  field("SDL", snapshot.sdlVersion);
  field("Operating system", snapshot.operatingSystem);
  field("Architecture", snapshot.architecture);
  field("Application data mode", snapshot.applicationDataMode);
  field("Requested interface language", snapshot.requestedInterfaceLanguage);
  field("Effective interface language", snapshot.effectiveInterfaceLanguage);
  output << "Interface language fallback: "
         << (snapshot.interfaceLanguageFallback ? "Yes" : "No") << '\n';
  field("Renderer", snapshot.renderer);
  field("Presentation sync", snapshot.presentationSync);
  field("Presentation buffering", snapshot.presentationBuffering);
  field("Video artwork", snapshot.videoArtwork);
  field("Video artwork format", snapshot.videoArtworkFormat);
  output << "Video artwork dimensions: " << snapshot.videoArtworkWidth
         << " x " << snapshot.videoArtworkHeight << '\n'
         << "Video artwork bytes: " << snapshot.videoArtworkBytes << '\n';
  field("Audio device", snapshot.audioDevice);
  field("Loaded game", snapshot.loadedGame);
  field("Loaded system", snapshot.loadedSystem);
  field("Loaded region", snapshot.loadedRegion);
  output << "Controllers: " << snapshot.controllerCount << '\n'
         << "Video exchange: " << snapshot.videoCopiedFrames << " copied / "
         << snapshot.videoPublishedFrames << " published, "
         << snapshot.videoSkippedFrames << " skipped, "
         << snapshot.videoProducerDrops << " producer drops\n"
         << "Video presentation: " << snapshot.videoRenderedFrames
         << " rendered, " << snapshot.videoSwappedFrames << " swapped, "
         << snapshot.videoCoalescedFrames << " coalesced, "
         << snapshot.videoDuplicateRenders << " duplicate renders\n"
         << "Video pending frames: " << snapshot.videoPendingFrames << " / "
         << snapshot.videoMaximumPendingFrames << " maximum\n"
         << "Presentation cadence: "
         << snapshot.measuredPresentationFramesPerSecond << " FPS, "
         << snapshot.averageSwapIntervalMicroseconds << " us average, "
         << snapshot.maximumSwapIntervalMicroseconds << " us maximum\n"
         << "Audio buffered frames: " << snapshot.audioBufferedFrames << " / "
         << snapshot.audioCapacityFrames << '\n'
         << "Audio underruns: " << snapshot.audioUnderruns << '\n'
         << "Audio overruns: " << snapshot.audioOverruns << '\n'
         << "Active speed: " << snapshot.activeSpeedPercent << "%"
         << (snapshot.fastForwarding ? " (fast forward)"
             : snapshot.slowMotion ? " (slow motion)" : "") << '\n'
         << "Configured speeds: Normal " << snapshot.normalSpeedPercent
         << "%, slow motion " << snapshot.slowMotionSpeedPercent
         << "%, fast forward " << snapshot.fastForwardSpeedPercent << "%\n"
         << "Rewind: " << (snapshot.rewindEnabled ? "Enabled" : "Disabled")
         << (snapshot.rewinding ? " (active)" : "") << '\n'
         << "Rewind snapshots: " << snapshot.rewindSnapshots << '\n'
         << "Rewind payload bytes: " << snapshot.rewindPayloadBytes << " / "
         << snapshot.rewindMemoryLimitBytes << '\n'
         << "Run-ahead: " << (snapshot.runAheadEnabled ? "Enabled" : "Disabled")
         << (snapshot.runAheadActive ? " (active)" : "") << '\n'
         << "Run-ahead support: "
         << (snapshot.runAheadSupported ? "Available" : "Unavailable") << '\n'
         << "Run-ahead frames: " << snapshot.runAheadFrames << '\n'
         << "Run-ahead determinism: "
         << (snapshot.runAheadVerified ? "Verified" : "Not verified") << '\n'
         << "Run-ahead speculative frames: "
         << snapshot.runAheadSpeculativeFrames << '\n'
         << "Run-ahead rollbacks: " << snapshot.runAheadRollbacks << '\n'
         << "Run-ahead determinism failures: "
         << snapshot.runAheadDeterminismFailures << '\n'
         << "Run-ahead state bytes: " << snapshot.runAheadStateBytes << " / "
         << snapshot.runAheadStateCapacityBytes << '\n'
         << "Netplay: " << safeLine(snapshot.netplayState, detectedHome) << '\n'
         << "Netplay packets: " << snapshot.netplaySentPackets << " sent / "
         << snapshot.netplayReceivedPackets << " received\n"
         << "Netplay bytes: " << snapshot.netplaySentBytes << " sent / "
         << snapshot.netplayReceivedBytes << " received\n"
         << "Netplay authentication failures: "
         << snapshot.netplayAuthenticationFailures << '\n'
         << "Netplay protocol failures: " << snapshot.netplayProtocolFailures
         << '\n'
         << "Netplay output queue: " << snapshot.netplayQueuedFrames << " / "
         << snapshot.netplayQueueCapacity << '\n'
         << "Netplay prediction/rollback: " << snapshot.netplayPredictedFrames
         << " predicted, " << snapshot.netplayRollbackRequests
         << " requested, " << snapshot.netplayRollbacks << " performed\n"
         << "Netplay rollback history: " << snapshot.netplayHistoryFrames
         << " frames, " << snapshot.netplayHistoryBytes << " bytes\n"
         << "RetroAchievements: "
         << safeLine(snapshot.achievementsState, detectedHome)
         << (snapshot.achievementsHardcore ? " (Hardcore active)" : "") << '\n'
         << "RetroAchievements bridge: "
         << snapshot.achievementRequestQueueDepth << " requests / "
         << snapshot.achievementResponseQueueDepth << " responses / "
         << snapshot.achievementQueueCapacity << " capacity\n"
         << "RetroAchievements bridge rejections: "
         << snapshot.rejectedAchievementRequests << " requests / "
         << snapshot.rejectedAchievementResponses << " responses\n"
         << "RetroAchievements HTTPS: " << snapshot.activeAchievementRequests
         << " active, " << snapshot.completedAchievementRequests
         << " completed, " << snapshot.failedAchievementRequests << " failed\n"
         << "RetroAchievements last network error: "
         << safeLine(snapshot.lastAchievementNetworkError, detectedHome) << '\n'
         << "Cloud synchronization: "
         << safeLine(snapshot.cloudSyncState, detectedHome) << '\n'
         << "Cloud content: saves "
         << (snapshot.cloudSyncSaves ? "enabled" : "disabled")
         << ", states "
         << (snapshot.cloudSyncStates ? "enabled" : "disabled") << '\n'
         << "Cloud automatic sync: startup "
         << (snapshot.cloudSyncOnStartup ? "enabled" : "disabled")
         << ", game close "
         << (snapshot.cloudSyncOnGameClose ? "enabled" : "disabled") << '\n'
         << "Lossless recording: "
         << (snapshot.recordingActive ? "Active" : "Inactive") << '\n'
         << "Recording queue: " << snapshot.recordingQueuedFrames << " / "
         << snapshot.recordingQueueCapacity << '\n'
         << "Recording frames: " << snapshot.recordingWrittenFrames
         << " written, " << snapshot.recordingDroppedFrames << " dropped\n"
         << "Recording output bytes: " << snapshot.recordingOutputBytes << '\n'
         << "Structured logging: " << (snapshot.loggerActive ? "Active" : "Unavailable")
         << '\n'
         << "Log messages written: " << snapshot.logger.writtenMessages << '\n'
         << "Log messages dropped: " << snapshot.logger.droppedMessages << '\n'
         << "Log messages redacted: " << snapshot.logger.redactedMessages << '\n'
         << "Log rotations: " << snapshot.logger.rotations << "\n\n"
         << "BIOS status\n";
  if (snapshot.bios.empty()) {
    output << "- No BIOS slots reported\n";
  }
  for (const auto& bios : snapshot.bios) {
    output << "- " << safeLine(bios.name, detectedHome) << ": "
           << safeLine(bios.status, detectedHome);
    if (!bios.sha256Prefix.empty()) {
      output << " (SHA-256 " << safeLine(bios.sha256Prefix, detectedHome) << ')';
    }
    output << '\n';
  }
  output << "\nPrivacy: filesystem paths and credential-like values are omitted or "
            "redacted.\n";
  return output.str();
}

} // namespace genplusgx::diagnostics
