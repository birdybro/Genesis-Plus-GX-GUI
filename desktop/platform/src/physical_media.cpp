#include "genplusgx/platform/physical_media.h"

#include "genplusgx/bounded_queue.h"

#include <QCryptographicHash>
#include <QByteArrayView>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <mutex>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>

namespace genplusgx::platform {
namespace {

constexpr std::uint32_t minimumSegaCdSectors = 150U;
constexpr std::uint32_t sectorsPerRead = 16U;
constexpr std::size_t maximumTracks = 99U;

PhysicalMediaStatus failure(PhysicalMediaError error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

QString pathString(const std::filesystem::path& path)
{
#if defined(Q_OS_WIN)
  return QString::fromStdWString(path.wstring());
#else
  return QString::fromUtf8(path.string());
#endif
}

std::filesystem::path filesystemPath(const QString& path)
{
#if defined(Q_OS_WIN)
  return std::filesystem::path{path.toStdWString()};
#else
  return std::filesystem::path{path.toUtf8().constData()};
#endif
}

std::string cueTimestamp(std::uint32_t sector)
{
  const auto minutes = sector / (60U * 75U);
  const auto seconds = (sector / 75U) % 60U;
  const auto frames = sector % 75U;
  std::ostringstream output;
  output << std::setfill('0') << std::setw(2) << minutes << ':'
         << std::setw(2) << seconds << ':' << std::setw(2) << frames;
  return output.str();
}

bool segaCdSignature(std::span<const std::uint8_t> rawSector)
{
  constexpr std::string_view signature{"SEGADISCSYSTEM"};
  for (const std::size_t offset : {std::size_t{16U}, std::size_t{24U}}) {
    if (offset + signature.size() <= rawSector.size() &&
        std::equal(signature.begin(), signature.end(),
          rawSector.begin() + static_cast<std::ptrdiff_t>(offset))) {
      return true;
    }
  }
  return false;
}

std::size_t trackIndexForSector(
  const PhysicalDisc& disc, std::uint32_t sector) noexcept
{
  const auto next = std::upper_bound(disc.tracks.begin(), disc.tracks.end(),
    sector, [](std::uint32_t value, const PhysicalTrack& track) {
      return value < track.startSector;
    });
  return next == disc.tracks.begin()
    ? 0U
    : static_cast<std::size_t>(std::distance(disc.tracks.begin(), next) - 1);
}

std::uint32_t trackEndSector(
  const PhysicalDisc& disc, std::size_t trackIndex) noexcept
{
  return trackIndex + 1U < disc.tracks.size()
    ? disc.tracks[trackIndex + 1U].startSector
    : disc.leadOutSector;
}

bool safeSnapshotDirectory(
  const std::filesystem::path& cacheDirectory,
  const std::filesystem::path& snapshotDirectory)
{
  if (cacheDirectory.empty() || snapshotDirectory.empty() ||
      !cacheDirectory.is_absolute() || !snapshotDirectory.is_absolute()) {
    return false;
  }
  const auto cache = cacheDirectory.lexically_normal();
  const auto candidate = snapshotDirectory.lexically_normal();
  return candidate.parent_path() == cache &&
    candidate.filename().string().starts_with("disc-");
}

bool cachedSnapshotMatches(
  const std::filesystem::path& binaryPath,
  const std::filesystem::path& cuePath,
  std::string_view expectedCue,
  std::string_view expectedDigest)
{
  std::ifstream cueFile{cuePath, std::ios::binary};
  const std::string storedCue{
    std::istreambuf_iterator<char>{cueFile},
    std::istreambuf_iterator<char>{}};
  if (cueFile.bad() || storedCue != expectedCue) {
    return false;
  }
  std::ifstream binary{binaryPath, std::ios::binary};
  if (!binary) {
    return false;
  }
  QCryptographicHash hash{QCryptographicHash::Sha256};
  hash.addData(QByteArrayView{expectedCue.data(),
    static_cast<qsizetype>(expectedCue.size())});
  std::array<char, 256U * 1024U> buffer{};
  while (binary) {
    binary.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = binary.gcount();
    if (count > 0) {
      hash.addData(QByteArrayView{buffer.data(),
        static_cast<qsizetype>(count)});
    }
  }
  return binary.eof() && hash.result().toHex().toStdString() == expectedDigest;
}

enum class PhysicalMediaCommandType : std::uint8_t {
  discover,
  importDisc,
};

struct PhysicalMediaCommand final {
  PhysicalMediaCommandType type{PhysicalMediaCommandType::discover};
  std::uint64_t operationId{0U};
  std::string driveId;
};

} // namespace

PhysicalMediaStatus validatePhysicalDisc(const PhysicalDisc& disc)
{
  if (!disc.drive.valid()) {
    return failure(PhysicalMediaError::invalidTableOfContents,
      "The optical drive identity is incomplete.");
  }
  if (disc.tracks.empty() || disc.tracks.size() > maximumTracks) {
    return failure(PhysicalMediaError::invalidTableOfContents,
      "The disc table of contents must contain between 1 and 99 tracks.");
  }
  if (disc.tracks.front().type == PhysicalTrackType::audio) {
    return failure(PhysicalMediaError::unsupportedLayout,
      "Sega CD media must begin with a data track.");
  }
  if (disc.tracks.front().startSector > 150U) {
    return failure(PhysicalMediaError::unsupportedLayout,
      "The first data track begins beyond the supported lead-in range.");
  }
  for (std::size_t index = 0U; index < disc.tracks.size(); ++index) {
    const auto& track = disc.tracks[index];
    const auto expectedNumber =
      static_cast<std::uint8_t>(disc.tracks.front().number + index);
    if (track.number == 0U || track.number != expectedNumber ||
        track.startSector >= disc.leadOutSector ||
        (index != 0U &&
         track.startSector <= disc.tracks[index - 1U].startSector)) {
      return failure(PhysicalMediaError::invalidTableOfContents,
        "The disc tracks are not ordered, sequential, and non-empty.");
    }
    if (index != 0U && track.type != PhysicalTrackType::audio) {
      return failure(PhysicalMediaError::unsupportedLayout,
        "Only one leading data track followed by audio tracks is supported.");
    }
  }
  const auto totalSectors =
    disc.leadOutSector - disc.tracks.front().startSector;
  if (totalSectors < minimumSegaCdSectors ||
      totalSectors > maximumPhysicalDiscSectors) {
    return failure(PhysicalMediaError::unsupportedLayout,
      "The physical disc size is outside the supported 2-to-100-minute range.");
  }
  return {};
}

std::string physicalDiscCueSheet(const PhysicalDisc& disc)
{
  if (!validatePhysicalDisc(disc)) {
    return {};
  }
  const auto baseSector = disc.tracks.front().startSector;
  std::ostringstream cue;
  cue << "FILE \"disc.bin\" BINARY\n";
  for (const auto& track : disc.tracks) {
    cue << "  TRACK " << std::setfill('0') << std::setw(2)
        << static_cast<unsigned>(track.number) << ' ';
    switch (track.type) {
      case PhysicalTrackType::mode1Data:
        cue << "MODE1/2352\n";
        break;
      case PhysicalTrackType::mode2Data:
        cue << "MODE2/2352\n";
        break;
      case PhysicalTrackType::audio:
        cue << "AUDIO\n";
        break;
    }
    cue << "    INDEX 01 "
        << cueTimestamp(track.startSector - baseSector) << '\n';
  }
  return cue.str();
}

PhysicalMediaSnapshotResult snapshotPhysicalDisc(
  PhysicalMediaBackend& backend,
  const std::string& driveId,
  const std::filesystem::path& cacheDirectory,
  const PhysicalMediaProgress& progress,
  const PhysicalMediaCancellation& cancellationRequested)
{
  if (driveId.empty()) {
    return {.status = failure(PhysicalMediaError::invalidRequest,
      "A physical optical drive must be selected."), .snapshot = {}};
  }
  if (cacheDirectory.empty() || !cacheDirectory.is_absolute() ||
      cacheDirectory.native().size() > 4'096U) {
    return {.status = failure(PhysicalMediaError::invalidCache,
      "The physical-media cache must be a bounded absolute path."),
      .snapshot = {}};
  }
  if (cancellationRequested && cancellationRequested()) {
    return {.status = failure(
      PhysicalMediaError::cancelled, "Physical-disc import was cancelled."),
      .snapshot = {}};
  }
  auto opened = backend.open(driveId);
  if (!opened.status || !opened.reader) {
    if (opened.status) {
      opened.status = failure(PhysicalMediaError::driveUnavailable,
        "The selected optical drive did not provide a readable disc.");
    }
    return {.status = std::move(opened.status), .snapshot = {}};
  }
  const auto disc = opened.reader->disc();
  if (const auto valid = validatePhysicalDisc(disc); !valid) {
    return {.status = valid, .snapshot = {}};
  }

  std::error_code error;
  std::filesystem::create_directories(cacheDirectory, error);
  if (error || !std::filesystem::is_directory(cacheDirectory, error) || error) {
    return {.status = failure(PhysicalMediaError::invalidCache,
      "The physical-media cache directory could not be created."),
      .snapshot = {}};
  }
  QTemporaryDir temporary{pathString(
    cacheDirectory / ".physical-import-XXXXXX")};
  temporary.setAutoRemove(true);
  if (!temporary.isValid()) {
    return {.status = failure(PhysicalMediaError::invalidCache,
      "A private temporary directory could not be created in the cache."),
      .snapshot = {}};
  }
  const auto temporaryPath = filesystemPath(temporary.path());
  const auto binaryPath = temporaryPath / "disc.bin";
  std::ofstream binary{binaryPath, std::ios::binary | std::ios::trunc};
  if (!binary) {
    return {.status = failure(PhysicalMediaError::writeFailed,
      "The physical-disc snapshot could not be opened for writing."),
      .snapshot = {}};
  }

  QCryptographicHash hash{QCryptographicHash::Sha256};
  const auto cue = physicalDiscCueSheet(disc);
  hash.addData(QByteArrayView{
    cue.data(), static_cast<qsizetype>(cue.size())});
  const auto firstSector = disc.tracks.front().startSector;
  const auto totalSectors = disc.leadOutSector - firstSector;
  std::vector<std::uint8_t> buffer(
    static_cast<std::size_t>(sectorsPerRead) * compactDiscRawSectorBytes);
  bool signatureChecked = false;
  if (progress) {
    progress(0U, totalSectors);
  }
  std::uint32_t completed = 0U;
  while (completed < totalSectors) {
    if (cancellationRequested && cancellationRequested()) {
      binary.close();
      return {.status = failure(
        PhysicalMediaError::cancelled, "Physical-disc import was cancelled."),
        .snapshot = {}};
    }
    const auto sector = firstSector + completed;
    const auto trackIndex = trackIndexForSector(disc, sector);
    const auto untilTrackEnd = trackEndSector(disc, trackIndex) - sector;
    const auto count = std::min({
      sectorsPerRead, totalSectors - completed, untilTrackEnd});
    const auto byteCount =
      static_cast<std::size_t>(count) * compactDiscRawSectorBytes;
    auto bytes = std::span<std::uint8_t>{buffer}.first(byteCount);
    const auto read = opened.reader->readRawSectors(sector, count, bytes);
    if (!read) {
      binary.close();
      return {.status = read, .snapshot = {}};
    }
    if (!signatureChecked) {
      signatureChecked = true;
      if (!segaCdSignature(bytes.first(compactDiscRawSectorBytes))) {
        binary.close();
        return {.status = failure(PhysicalMediaError::notSegaCd,
          "The selected disc does not contain a Sega CD system signature."),
          .snapshot = {}};
      }
    }
    binary.write(reinterpret_cast<const char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
    if (!binary) {
      binary.close();
      return {.status = failure(PhysicalMediaError::writeFailed,
        "The physical-disc snapshot could not be written completely."),
        .snapshot = {}};
    }
    hash.addData(QByteArrayView{reinterpret_cast<const char*>(bytes.data()),
      static_cast<qsizetype>(bytes.size())});
    completed += count;
    if (progress) {
      progress(completed, totalSectors);
    }
  }
  binary.flush();
  if (!binary) {
    binary.close();
    return {.status = failure(PhysicalMediaError::writeFailed,
      "The physical-disc snapshot could not be flushed to storage."),
      .snapshot = {}};
  }
  binary.close();

  const auto cuePath = temporaryPath / "disc.cue";
  std::ofstream cueFile{cuePath, std::ios::binary | std::ios::trunc};
  cueFile.write(cue.data(), static_cast<std::streamsize>(cue.size()));
  cueFile.flush();
  if (!cueFile) {
    cueFile.close();
    return {.status = failure(PhysicalMediaError::writeFailed,
      "The generated physical-disc CUE sheet could not be written."),
      .snapshot = {}};
  }
  cueFile.close();

  const auto digest = hash.result().toHex().toStdString();
  const auto destination = cacheDirectory / ("disc-" + digest);
  const auto destinationBinary = destination / "disc.bin";
  const auto destinationCue = destination / "disc.cue";
  const auto byteSize = static_cast<std::uint64_t>(totalSectors) *
    compactDiscRawSectorBytes;
  if (std::filesystem::exists(destination, error) && !error) {
    const auto directoryStatus = std::filesystem::symlink_status(destination, error);
    const auto binaryStatus = std::filesystem::symlink_status(destinationBinary, error);
    const auto cueStatus = std::filesystem::symlink_status(destinationCue, error);
    const auto existingSize = std::filesystem::file_size(destinationBinary, error);
    if (error || directoryStatus.type() != std::filesystem::file_type::directory ||
        binaryStatus.type() != std::filesystem::file_type::regular ||
        cueStatus.type() != std::filesystem::file_type::regular ||
        existingSize != byteSize ||
        !cachedSnapshotMatches(
          destinationBinary, destinationCue, cue, digest)) {
      return {.status = failure(PhysicalMediaError::commitFailed,
        "An unsafe or incomplete physical-disc cache entry blocks the import."),
        .snapshot = {}};
    }
    return {
      .status = {},
      .snapshot = {
        .disc = disc,
        .cuePath = destinationCue,
        .storageDirectory = destination,
        .sha256 = digest,
        .byteSize = byteSize,
      },
    };
  }
  error.clear();
  std::filesystem::rename(temporaryPath, destination, error);
  if (error) {
    return {.status = failure(PhysicalMediaError::commitFailed,
      "The completed physical-disc snapshot could not be committed atomically."),
      .snapshot = {}};
  }
  temporary.setAutoRemove(false);
  if (pathString(destinationCue).toUtf8().size() > 255) {
    PhysicalMediaSnapshot oversized{
      .disc = disc,
      .cuePath = destinationCue,
      .storageDirectory = destination,
      .sha256 = digest,
      .byteSize = byteSize,
    };
    static_cast<void>(releasePhysicalMediaSnapshot(cacheDirectory, oversized));
    return {.status = failure(PhysicalMediaError::invalidCache,
      "The physical-media cache path is too long for the emulator core."),
      .snapshot = {}};
  }
  return {
    .status = {},
    .snapshot = {
      .disc = disc,
      .cuePath = destinationCue,
      .storageDirectory = destination,
      .sha256 = digest,
      .byteSize = byteSize,
    },
  };
}

PhysicalMediaStatus releasePhysicalMediaSnapshot(
  const std::filesystem::path& cacheDirectory,
  const PhysicalMediaSnapshot& snapshot)
{
  if (!snapshot.valid() ||
      !safeSnapshotDirectory(cacheDirectory, snapshot.storageDirectory) ||
      snapshot.cuePath.lexically_normal().parent_path() !=
        snapshot.storageDirectory.lexically_normal()) {
    return failure(PhysicalMediaError::invalidRequest,
      "The requested physical-media snapshot is outside the managed cache.");
  }
  std::error_code error;
  const auto status = std::filesystem::symlink_status(
    snapshot.storageDirectory, error);
  if (error == std::errc::no_such_file_or_directory ||
      status.type() == std::filesystem::file_type::not_found) {
    return {};
  }
  if (error || status.type() != std::filesystem::file_type::directory) {
    return failure(PhysicalMediaError::invalidCache,
      "The managed physical-media snapshot is not a safe directory.");
  }
  std::filesystem::remove_all(snapshot.storageDirectory, error);
  if (error) {
    return failure(PhysicalMediaError::writeFailed,
      "The temporary physical-media snapshot could not be removed.");
  }
  return {};
}

class PhysicalMediaService::Private final {
public:
  Private(std::shared_ptr<PhysicalMediaBackend> backend,
    std::filesystem::path cacheDirectory,
    std::size_t commandCapacity,
    std::size_t eventCapacity)
    : backend_(std::move(backend)),
      cacheDirectory_(std::move(cacheDirectory)),
      commands_(commandCapacity),
      events_(eventCapacity)
  {
  }

  PhysicalMediaStatus start()
  {
    std::scoped_lock lock{mutex_};
    if (thread_.joinable() || accepting_) {
      return failure(PhysicalMediaError::alreadyRunning,
        "The physical-media service is already running.");
    }
    if (!backend_ || cacheDirectory_.empty() ||
        !cacheDirectory_.is_absolute()) {
      return failure(PhysicalMediaError::invalidRequest,
        "The physical-media service configuration is incomplete.");
    }
    commands_.clear();
    events_.clear();
    stopRequested_.store(false, std::memory_order_release);
    cancelledOperation_.store(0U, std::memory_order_release);
    accepting_ = true;
    shutdownStatus_ = {};
    try {
      thread_ = std::thread{&Private::threadMain, this};
    } catch (const std::system_error& error) {
      accepting_ = false;
      return failure(PhysicalMediaError::threadFailure,
        "The physical-media worker could not start: " +
          std::string{error.what()});
    }
    return {};
  }

  PhysicalMediaStatus submit(PhysicalMediaCommand command)
  {
    if (command.operationId == 0U ||
        (command.type == PhysicalMediaCommandType::importDisc &&
         command.driveId.empty())) {
      return failure(PhysicalMediaError::invalidRequest,
        "Physical-media requests require a nonzero ID and selected drive.");
    }
    std::scoped_lock lock{mutex_};
    if (!accepting_ || stopRequested_.load(std::memory_order_acquire)) {
      return failure(PhysicalMediaError::notRunning,
        "The physical-media service is not accepting requests.");
    }
    if (!commands_.tryPush(std::move(command))) {
      return failure(PhysicalMediaError::queueFull,
        "The bounded physical-media request queue is full.");
    }
    wake_.notify_one();
    return {};
  }

  PhysicalMediaStatus cancel(std::uint64_t operationId)
  {
    if (operationId == 0U) {
      return failure(PhysicalMediaError::invalidRequest,
        "A nonzero physical-media operation ID is required.");
    }
    std::scoped_lock lock{mutex_};
    if (!accepting_ || stopRequested_.load(std::memory_order_acquire)) {
      return failure(PhysicalMediaError::notRunning,
        "The physical-media service is not accepting cancellation requests.");
    }
    cancelledOperation_.store(operationId, std::memory_order_release);
    return {};
  }

  std::optional<PhysicalMediaEvent> pollEvent()
  {
    std::scoped_lock lock{mutex_};
    return events_.pop();
  }

  std::optional<PhysicalMediaEvent> waitForEvent(
    std::chrono::milliseconds timeout)
  {
    std::unique_lock lock{mutex_};
    eventReady_.wait_for(lock, timeout, [this] { return !events_.empty(); });
    return events_.pop();
  }

  PhysicalMediaStatus stop()
  {
    {
      std::scoped_lock lock{mutex_};
      if (!thread_.joinable()) {
        accepting_ = false;
        return shutdownStatus_;
      }
      accepting_ = false;
      stopRequested_.store(true, std::memory_order_release);
      commands_.clear();
      wake_.notify_all();
      eventReady_.notify_all();
    }
    thread_.join();
    std::scoped_lock lock{mutex_};
    return shutdownStatus_;
  }

private:
  bool cancelled(std::uint64_t operationId) const noexcept
  {
    return stopRequested_.load(std::memory_order_acquire) ||
      cancelledOperation_.load(std::memory_order_acquire) == operationId;
  }

  void threadMain()
  {
    publish({
      .type = PhysicalMediaEventType::serviceStarted,
      .operationId = 0U,
      .status = {},
      .drives = {},
      .snapshot = {},
      .completedSectors = 0U,
      .totalSectors = 0U,
    });
    while (true) {
      std::optional<PhysicalMediaCommand> command;
      {
        std::unique_lock lock{mutex_};
        wake_.wait(lock, [this] {
          return stopRequested_.load(std::memory_order_acquire) ||
            !commands_.empty();
        });
        if (stopRequested_.load(std::memory_order_acquire)) {
          break;
        }
        command = commands_.pop();
      }
      if (!command) {
        continue;
      }
      if (command->type == PhysicalMediaCommandType::discover) {
        std::vector<PhysicalDrive> drives;
        auto status = cancelled(command->operationId)
          ? failure(PhysicalMediaError::cancelled,
              "Physical-drive discovery was cancelled.")
          : backend_->discover(drives);
        publish({
          .type = status ? PhysicalMediaEventType::discoveryReady
                         : (status.error == PhysicalMediaError::cancelled
                             ? PhysicalMediaEventType::operationCancelled
                             : PhysicalMediaEventType::operationFailed),
          .operationId = command->operationId,
          .status = std::move(status),
          .drives = std::move(drives),
          .snapshot = {},
          .completedSectors = 0U,
          .totalSectors = 0U,
        });
        auto cancelledOperation = command->operationId;
        static_cast<void>(cancelledOperation_.compare_exchange_strong(
          cancelledOperation, 0U, std::memory_order_acq_rel));
        continue;
      }
      publish({
        .type = PhysicalMediaEventType::importStarted,
        .operationId = command->operationId,
        .status = {},
        .drives = {},
        .snapshot = {},
        .completedSectors = 0U,
        .totalSectors = 0U,
      });
      auto result = snapshotPhysicalDisc(*backend_, command->driveId,
        cacheDirectory_,
        [this, operationId = command->operationId](
          std::uint32_t completed, std::uint32_t total) {
          publish({
            .type = PhysicalMediaEventType::importProgress,
            .operationId = operationId,
            .status = {},
            .drives = {},
            .snapshot = {},
            .completedSectors = completed,
            .totalSectors = total,
          });
        },
        [this, operationId = command->operationId] {
          return cancelled(operationId);
        });
      const auto type = result.status
        ? PhysicalMediaEventType::importReady
        : (result.status.error == PhysicalMediaError::cancelled
            ? PhysicalMediaEventType::operationCancelled
            : PhysicalMediaEventType::operationFailed);
      publish({
        .type = type,
        .operationId = command->operationId,
        .status = std::move(result.status),
        .drives = {},
        .snapshot = std::move(result.snapshot),
        .completedSectors = 0U,
        .totalSectors = 0U,
      });
      auto cancelledOperation = command->operationId;
      static_cast<void>(cancelledOperation_.compare_exchange_strong(
        cancelledOperation, 0U, std::memory_order_acq_rel));
    }
    {
      std::scoped_lock lock{mutex_};
      accepting_ = false;
      shutdownStatus_ = {};
    }
    publish({
      .type = PhysicalMediaEventType::serviceStopped,
      .operationId = 0U,
      .status = {},
      .drives = {},
      .snapshot = {},
      .completedSectors = 0U,
      .totalSectors = 0U,
    });
  }

  void publish(PhysicalMediaEvent event)
  {
    std::scoped_lock lock{mutex_};
    static_cast<void>(events_.dropOldestAndPush(std::move(event)));
    eventReady_.notify_all();
  }

  std::shared_ptr<PhysicalMediaBackend> backend_;
  std::filesystem::path cacheDirectory_;
  BoundedQueue<PhysicalMediaCommand> commands_;
  BoundedQueue<PhysicalMediaEvent> events_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable eventReady_;
  std::thread thread_;
  PhysicalMediaStatus shutdownStatus_;
  bool accepting_{false};
  std::atomic_bool stopRequested_{false};
  std::atomic_uint64_t cancelledOperation_{0U};
};

PhysicalMediaService::PhysicalMediaService(
  std::shared_ptr<PhysicalMediaBackend> backend,
  std::filesystem::path cacheDirectory,
  std::size_t commandCapacity,
  std::size_t eventCapacity)
  : private_(std::make_unique<Private>(std::move(backend),
      std::move(cacheDirectory), commandCapacity, eventCapacity))
{
}

PhysicalMediaService::~PhysicalMediaService()
{
  static_cast<void>(private_->stop());
}

PhysicalMediaStatus PhysicalMediaService::start()
{
  return private_->start();
}

PhysicalMediaStatus PhysicalMediaService::discover(std::uint64_t operationId)
{
  return private_->submit({
    .type = PhysicalMediaCommandType::discover,
    .operationId = operationId,
    .driveId = {},
  });
}

PhysicalMediaStatus PhysicalMediaService::importDisc(
  std::uint64_t operationId, std::string driveId)
{
  return private_->submit({
    .type = PhysicalMediaCommandType::importDisc,
    .operationId = operationId,
    .driveId = std::move(driveId),
  });
}

PhysicalMediaStatus PhysicalMediaService::cancel(std::uint64_t operationId)
{
  return private_->cancel(operationId);
}

std::optional<PhysicalMediaEvent> PhysicalMediaService::pollEvent()
{
  return private_->pollEvent();
}

std::optional<PhysicalMediaEvent> PhysicalMediaService::waitForEvent(
  std::chrono::milliseconds timeout)
{
  return private_->waitForEvent(timeout);
}

PhysicalMediaStatus PhysicalMediaService::stop()
{
  return private_->stop();
}

} // namespace genplusgx::platform
