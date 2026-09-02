#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace genplusgx::diagnostics {

enum class LogLevel : std::uint8_t {
  debug,
  information,
  warning,
  critical,
  fatal,
};

enum class LoggerError : std::uint8_t {
  none,
  invalidConfiguration,
  directoryCreationFailed,
  fileOpenFailed,
};

struct LoggerStatus final {
  LoggerError error{LoggerError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept { return error == LoggerError::none; }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

struct LoggerMetrics final {
  std::uint64_t writtenMessages{0};
  std::uint64_t droppedMessages{0};
  std::uint64_t redactedMessages{0};
  std::uint64_t rotations{0};
  std::uintmax_t currentBytes{0};
};

[[nodiscard]] std::string redactSensitiveText(
  std::string_view text, std::string_view homeDirectory = {});

class FrontendLogger final {
public:
  static constexpr std::uintmax_t defaultMaximumFileBytes = 1U * 1024U * 1024U;
  static constexpr std::size_t defaultBackupCount = 3U;

  FrontendLogger();
  ~FrontendLogger();

  FrontendLogger(const FrontendLogger&) = delete;
  FrontendLogger& operator=(const FrontendLogger&) = delete;
  FrontendLogger(FrontendLogger&&) = delete;
  FrontendLogger& operator=(FrontendLogger&&) = delete;

  [[nodiscard]] LoggerStatus initialize(std::filesystem::path path,
    std::uintmax_t maximumFileBytes = defaultMaximumFileBytes,
    std::size_t backupCount = defaultBackupCount);
  [[nodiscard]] bool install();
  void shutdown() noexcept;
  void write(LogLevel level, std::string_view category, std::string_view message);

  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] bool installed() const noexcept;
  [[nodiscard]] LoggerMetrics metrics() const noexcept;

private:
  class Private;
  std::unique_ptr<Private> private_;
};

struct BiosDiagnostic final {
  std::string name;
  std::string status;
  std::string sha256Prefix;
};

struct DiagnosticsSnapshot final {
  std::string applicationName;
  std::string version;
  std::string gitCommit;
  std::string qtVersion;
  std::string sdlVersion;
  std::string operatingSystem;
  std::string architecture;
  std::string applicationDataMode;
  std::string requestedInterfaceLanguage;
  std::string effectiveInterfaceLanguage;
  bool interfaceLanguageFallback{false};
  std::string renderer;
  std::string presentationSync;
  std::string presentationBuffering;
  std::string videoArtwork;
  std::string videoArtworkFormat;
  std::uintmax_t videoArtworkBytes{0U};
  std::uint32_t videoArtworkWidth{0U};
  std::uint32_t videoArtworkHeight{0U};
  std::string audioDevice;
  std::string loadedGame;
  std::string loadedSystem;
  std::string loadedRegion;
  std::size_t controllerCount{0};
  std::uint64_t videoPublishedFrames{0U};
  std::uint64_t videoCopiedFrames{0U};
  std::uint64_t videoSkippedFrames{0U};
  std::uint64_t videoProducerDrops{0U};
  std::uint64_t videoRenderedFrames{0U};
  std::uint64_t videoSwappedFrames{0U};
  std::uint64_t videoCoalescedFrames{0U};
  std::uint64_t videoDuplicateRenders{0U};
  std::size_t videoPendingFrames{0U};
  std::size_t videoMaximumPendingFrames{0U};
  double measuredPresentationFramesPerSecond{0.0};
  std::uint64_t averageSwapIntervalMicroseconds{0U};
  std::uint64_t maximumSwapIntervalMicroseconds{0U};
  std::uint64_t audioUnderruns{0};
  std::uint64_t audioOverruns{0};
  std::size_t audioBufferedFrames{0};
  std::size_t audioCapacityFrames{0};
  std::uint32_t normalSpeedPercent{100U};
  std::uint32_t slowMotionSpeedPercent{50U};
  std::uint32_t fastForwardSpeedPercent{400U};
  std::uint32_t activeSpeedPercent{100U};
  bool fastForwarding{false};
  bool slowMotion{false};
  bool rewindEnabled{false};
  bool rewinding{false};
  std::size_t rewindSnapshots{0};
  std::size_t rewindPayloadBytes{0};
  std::size_t rewindMemoryLimitBytes{0};
  bool runAheadEnabled{false};
  bool runAheadSupported{false};
  bool runAheadActive{false};
  bool runAheadVerified{false};
  std::uint32_t runAheadFrames{1U};
  std::uint64_t runAheadSpeculativeFrames{0U};
  std::uint64_t runAheadRollbacks{0U};
  std::uint64_t runAheadDeterminismFailures{0U};
  std::size_t runAheadStateBytes{0U};
  std::size_t runAheadStateCapacityBytes{0U};
  std::string netplayState;
  std::uint64_t netplaySentPackets{0U};
  std::uint64_t netplayReceivedPackets{0U};
  std::uint64_t netplaySentBytes{0U};
  std::uint64_t netplayReceivedBytes{0U};
  std::uint64_t netplayAuthenticationFailures{0U};
  std::uint64_t netplayProtocolFailures{0U};
  std::size_t netplayQueuedFrames{0U};
  std::size_t netplayQueueCapacity{0U};
  std::uint64_t netplayPredictedFrames{0U};
  std::uint64_t netplayRollbackRequests{0U};
  std::uint64_t netplayRollbacks{0U};
  std::size_t netplayHistoryFrames{0U};
  std::size_t netplayHistoryBytes{0U};
  bool recordingActive{false};
  std::size_t recordingQueuedFrames{0};
  std::size_t recordingQueueCapacity{0};
  std::uint64_t recordingWrittenFrames{0};
  std::uint64_t recordingDroppedFrames{0};
  std::uint64_t recordingOutputBytes{0};
  bool loggerActive{false};
  LoggerMetrics logger;
  std::vector<BiosDiagnostic> bios;
};

[[nodiscard]] DiagnosticsSnapshot staticDiagnosticsSnapshot();
[[nodiscard]] std::string formatDiagnostics(
  const DiagnosticsSnapshot& snapshot, std::string_view homeDirectory = {});

} // namespace genplusgx::diagnostics
