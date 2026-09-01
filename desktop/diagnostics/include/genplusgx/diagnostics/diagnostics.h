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
  std::string renderer;
  std::string audioDevice;
  std::string loadedGame;
  std::string loadedSystem;
  std::string loadedRegion;
  std::size_t controllerCount{0};
  std::uint64_t audioUnderruns{0};
  std::uint64_t audioOverruns{0};
  std::size_t audioBufferedFrames{0};
  std::size_t audioCapacityFrames{0};
  bool rewindEnabled{false};
  bool rewinding{false};
  std::size_t rewindSnapshots{0};
  std::size_t rewindPayloadBytes{0};
  std::size_t rewindMemoryLimitBytes{0};
  bool loggerActive{false};
  LoggerMetrics logger;
  std::vector<BiosDiagnostic> bios;
};

[[nodiscard]] DiagnosticsSnapshot staticDiagnosticsSnapshot();
[[nodiscard]] std::string formatDiagnostics(
  const DiagnosticsSnapshot& snapshot, std::string_view homeDirectory = {});

} // namespace genplusgx::diagnostics
