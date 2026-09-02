#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace genplusgx::platform {

inline constexpr std::size_t compactDiscRawSectorBytes = 2'352U;
inline constexpr std::uint32_t maximumPhysicalDiscSectors = 100U * 60U * 75U;

enum class PhysicalMediaError : std::uint8_t {
  none,
  unsupportedPlatform,
  invalidRequest,
  alreadyRunning,
  notRunning,
  queueFull,
  threadFailure,
  discoveryFailed,
  driveUnavailable,
  noMedia,
  permissionDenied,
  readFailed,
  invalidTableOfContents,
  unsupportedLayout,
  notSegaCd,
  invalidCache,
  writeFailed,
  commitFailed,
  cancelled,
};

struct PhysicalMediaStatus final {
  PhysicalMediaError error{PhysicalMediaError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == PhysicalMediaError::none;
  }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

enum class PhysicalTrackType : std::uint8_t {
  mode1Data,
  mode2Data,
  audio,
};

struct PhysicalTrack final {
  std::uint8_t number{0U};
  PhysicalTrackType type{PhysicalTrackType::mode1Data};
  std::uint32_t startSector{0U};

  [[nodiscard]] bool operator==(const PhysicalTrack&) const = default;
};

struct PhysicalDrive final {
  std::string id;
  std::string displayName;

  [[nodiscard]] bool valid() const noexcept
  {
    return !id.empty() && !displayName.empty();
  }
  [[nodiscard]] bool operator==(const PhysicalDrive&) const = default;
};

struct PhysicalDisc final {
  PhysicalDrive drive;
  std::vector<PhysicalTrack> tracks;
  std::uint32_t leadOutSector{0U};

  [[nodiscard]] bool operator==(const PhysicalDisc&) const = default;
};

class PhysicalDiscReader {
public:
  virtual ~PhysicalDiscReader() = default;

  [[nodiscard]] virtual const PhysicalDisc& disc() const noexcept = 0;
  [[nodiscard]] virtual PhysicalMediaStatus readRawSectors(
    std::uint32_t firstSector,
    std::uint32_t sectorCount,
    std::span<std::uint8_t> destination) = 0;
};

struct PhysicalDiscOpenResult final {
  PhysicalMediaStatus status;
  std::unique_ptr<PhysicalDiscReader> reader;
};

class PhysicalMediaBackend {
public:
  virtual ~PhysicalMediaBackend() = default;

  [[nodiscard]] virtual PhysicalMediaStatus discover(
    std::vector<PhysicalDrive>& drives) = 0;
  [[nodiscard]] virtual PhysicalDiscOpenResult open(
    const std::string& driveId) = 0;
};

[[nodiscard]] bool nativePhysicalMediaSupported() noexcept;
[[nodiscard]] std::shared_ptr<PhysicalMediaBackend>
createNativePhysicalMediaBackend();

[[nodiscard]] PhysicalMediaStatus validatePhysicalDisc(
  const PhysicalDisc& disc);
[[nodiscard]] std::string physicalDiscCueSheet(const PhysicalDisc& disc);

struct PhysicalMediaSnapshot final {
  PhysicalDisc disc;
  std::filesystem::path cuePath;
  std::filesystem::path storageDirectory;
  std::string sha256;
  std::uint64_t byteSize{0U};

  [[nodiscard]] bool valid() const noexcept
  {
    return disc.drive.valid() && !cuePath.empty() &&
      !storageDirectory.empty() && sha256.size() == 64U && byteSize != 0U;
  }
};

struct PhysicalMediaSnapshotResult final {
  PhysicalMediaStatus status;
  PhysicalMediaSnapshot snapshot;
};

using PhysicalMediaProgress =
  std::function<void(std::uint32_t completedSectors,
    std::uint32_t totalSectors)>;
using PhysicalMediaCancellation = std::function<bool()>;

[[nodiscard]] PhysicalMediaSnapshotResult snapshotPhysicalDisc(
  PhysicalMediaBackend& backend,
  const std::string& driveId,
  const std::filesystem::path& cacheDirectory,
  const PhysicalMediaProgress& progress = {},
  const PhysicalMediaCancellation& cancellationRequested = {});
[[nodiscard]] PhysicalMediaStatus releasePhysicalMediaSnapshot(
  const std::filesystem::path& cacheDirectory,
  const PhysicalMediaSnapshot& snapshot);

enum class PhysicalMediaEventType : std::uint8_t {
  serviceStarted,
  discoveryReady,
  importStarted,
  importProgress,
  importReady,
  operationFailed,
  operationCancelled,
  serviceStopped,
};

struct PhysicalMediaEvent final {
  PhysicalMediaEventType type{PhysicalMediaEventType::operationFailed};
  std::uint64_t operationId{0U};
  PhysicalMediaStatus status;
  std::vector<PhysicalDrive> drives;
  PhysicalMediaSnapshot snapshot;
  std::uint32_t completedSectors{0U};
  std::uint32_t totalSectors{0U};

  [[nodiscard]] bool succeeded() const noexcept
  {
    return (type == PhysicalMediaEventType::discoveryReady ||
            type == PhysicalMediaEventType::importReady) && status.ok();
  }
};

class PhysicalMediaService final {
public:
  explicit PhysicalMediaService(
    std::shared_ptr<PhysicalMediaBackend> backend,
    std::filesystem::path cacheDirectory,
    std::size_t commandCapacity = 4U,
    std::size_t eventCapacity = 32U);
  ~PhysicalMediaService();

  PhysicalMediaService(const PhysicalMediaService&) = delete;
  PhysicalMediaService& operator=(const PhysicalMediaService&) = delete;
  PhysicalMediaService(PhysicalMediaService&&) = delete;
  PhysicalMediaService& operator=(PhysicalMediaService&&) = delete;

  [[nodiscard]] PhysicalMediaStatus start();
  [[nodiscard]] PhysicalMediaStatus discover(std::uint64_t operationId);
  [[nodiscard]] PhysicalMediaStatus importDisc(
    std::uint64_t operationId, std::string driveId);
  [[nodiscard]] PhysicalMediaStatus cancel(std::uint64_t operationId);
  [[nodiscard]] std::optional<PhysicalMediaEvent> pollEvent();
  [[nodiscard]] std::optional<PhysicalMediaEvent> waitForEvent(
    std::chrono::milliseconds timeout);
  [[nodiscard]] PhysicalMediaStatus stop();

private:
  class Private;
  std::unique_ptr<Private> private_;
};

} // namespace genplusgx::platform
