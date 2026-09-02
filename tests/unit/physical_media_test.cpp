#include "genplusgx/game_file.h"
#include "genplusgx/platform/physical_media.h"

#include "physical_media_fixture.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace std::chrono_literals;

namespace {

bool check(bool condition, std::string_view message)
{
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

std::filesystem::path pathIn(const QTemporaryDir& directory)
{
  return std::filesystem::path{directory.path().toStdString()} /
    "physical-media";
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path)
{
  std::ifstream stream{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{stream},
    std::istreambuf_iterator<char>{}};
}

std::optional<genplusgx::platform::PhysicalMediaEvent> waitFor(
  genplusgx::platform::PhysicalMediaService& service,
  std::uint64_t operationId,
  std::initializer_list<genplusgx::platform::PhysicalMediaEventType> types)
{
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = service.waitForEvent(100ms);
    if (event && event->operationId == operationId &&
        std::ranges::find(types, event->type) != types.end()) {
      return event;
    }
  }
  return std::nullopt;
}

} // namespace

int main(int argc, char** argv)
{
  QCoreApplication application{argc, argv};
  using namespace genplusgx::platform;
  bool passed = true;

  const auto nativeBackend = createNativePhysicalMediaBackend();
  passed &= check(static_cast<bool>(nativeBackend),
    "The platform physical-media backend factory returned no backend");
  if (nativeBackend) {
    std::vector<PhysicalDrive> nativeDrives;
    const auto nativeDiscovery = nativeBackend->discover(nativeDrives);
    passed &= check(nativeDiscovery ||
        nativeDiscovery.error == PhysicalMediaError::unsupportedPlatform ||
        nativeDiscovery.error == PhysicalMediaError::discoveryFailed,
      "Native optical-drive discovery returned an invalid platform status");
    passed &= check(std::ranges::all_of(nativeDrives,
        [](const PhysicalDrive& drive) { return drive.valid(); }),
      "Native optical-drive discovery returned an incomplete identity");
    passed &= check(nativePhysicalMediaSupported() ||
        nativeDiscovery.error == PhysicalMediaError::unsupportedPlatform,
      "An unsupported platform backend did not fail closed");
    const auto invalidNativeOpen = nativeBackend->open({});
    passed &= check(!invalidNativeOpen.reader &&
        (invalidNativeOpen.status.error == PhysicalMediaError::invalidRequest ||
         invalidNativeOpen.status.error ==
           PhysicalMediaError::unsupportedPlatform),
      "The native backend accepted an empty optical-drive identifier");
  }

  genplusgx::test::SyntheticPhysicalMediaBackend backend;
  passed &= check(validatePhysicalDisc(backend.disc),
    "The legal mixed-mode synthetic Sega CD layout was rejected");
  const auto cue = physicalDiscCueSheet(backend.disc);
  passed &= check(cue ==
      "FILE \"disc.bin\" BINARY\n"
      "  TRACK 01 MODE1/2352\n"
      "    INDEX 01 00:00:00\n"
      "  TRACK 02 AUDIO\n"
      "    INDEX 01 00:02:00\n",
    "The mixed-mode CUE sheet did not preserve track types and offsets");

  auto invalid = backend.disc;
  invalid.tracks.front().type = PhysicalTrackType::audio;
  passed &= check(validatePhysicalDisc(invalid).error ==
      PhysicalMediaError::unsupportedLayout,
    "An audio-first disc was accepted as Sega CD media");
  invalid = backend.disc;
  invalid.tracks[1U].startSector = invalid.tracks[0U].startSector;
  passed &= check(validatePhysicalDisc(invalid).error ==
      PhysicalMediaError::invalidTableOfContents,
    "Overlapping physical tracks were accepted");
  invalid = backend.disc;
  invalid.leadOutSector = maximumPhysicalDiscSectors + 1U;
  passed &= check(validatePhysicalDisc(invalid).error ==
      PhysicalMediaError::unsupportedLayout,
    "An unbounded physical-disc size was accepted");

  QTemporaryDir directory;
  passed &= check(directory.isValid(), "The snapshot temporary root is invalid");
  std::vector<std::uint32_t> progress;
  const auto cache = pathIn(directory);
  auto snapshot = snapshotPhysicalDisc(backend, backend.disc.drive.id, cache,
    [&progress](std::uint32_t completed, std::uint32_t) {
      progress.push_back(completed);
    });
  const auto expectedBytes = static_cast<std::uint64_t>(
    backend.disc.leadOutSector) * compactDiscRawSectorBytes;
  passed &= check(snapshot.status && snapshot.snapshot.valid() &&
      snapshot.snapshot.byteSize == expectedBytes &&
      std::filesystem::file_size(
        snapshot.snapshot.storageDirectory / "disc.bin") == expectedBytes &&
      genplusgx::validateCueSheetFile(snapshot.snapshot.cuePath),
    "A legal physical Sega CD did not produce a validated atomic BIN/CUE snapshot");
  passed &= check(!progress.empty() && progress.front() == 0U &&
      progress.back() == backend.disc.leadOutSector &&
      std::ranges::is_sorted(progress),
    "Physical-disc snapshot progress was not monotonic and complete");
  const auto firstRaw = readBytes(
    snapshot.snapshot.storageDirectory / "disc.bin");
  constexpr std::string_view signature{"SEGADISCSYSTEM"};
  passed &= check(firstRaw.size() == expectedBytes &&
      std::equal(signature.begin(), signature.end(), firstRaw.begin() + 16),
    "The raw snapshot did not preserve the Sega CD data-sector payload");

  auto duplicate = snapshotPhysicalDisc(
    backend, backend.disc.drive.id, cache);
  passed &= check(duplicate.status &&
      duplicate.snapshot.storageDirectory == snapshot.snapshot.storageDirectory &&
      duplicate.snapshot.sha256 == snapshot.snapshot.sha256,
    "Content-addressed physical-disc snapshot reuse was not deterministic");
  {
    std::fstream tamper{duplicate.snapshot.storageDirectory / "disc.bin",
      std::ios::binary | std::ios::in | std::ios::out};
    char byte = 0;
    tamper.read(&byte, 1);
    byte = static_cast<char>(byte ^ 0x5a);
    tamper.seekp(0);
    tamper.write(&byte, 1);
  }
  const auto rejectedCache = snapshotPhysicalDisc(
    backend, backend.disc.drive.id, cache);
  passed &= check(rejectedCache.status.error ==
      PhysicalMediaError::commitFailed,
    "A same-size tampered physical-media cache entry was trusted");
  passed &= check(releasePhysicalMediaSnapshot(cache, duplicate.snapshot) &&
      !std::filesystem::exists(duplicate.snapshot.storageDirectory),
    "The managed physical-disc snapshot was not released cleanly");
  auto escaped = snapshot.snapshot;
  escaped.storageDirectory = directory.path().toStdString();
  passed &= check(releasePhysicalMediaSnapshot(cache, escaped).error ==
      PhysicalMediaError::invalidRequest,
    "Snapshot cleanup accepted a directory outside the managed cache");

  QTemporaryDir failureDirectory;
  backend.failAtSector = 32U;
  const auto failed = snapshotPhysicalDisc(
    backend, backend.disc.drive.id, pathIn(failureDirectory));
  passed &= check(failed.status.error == PhysicalMediaError::readFailed,
    "An injected optical read failure was not surfaced");
  std::error_code iteratorError;
  const auto failedCache = pathIn(failureDirectory);
  const bool failedCacheEmpty = !std::filesystem::exists(failedCache) ||
    std::filesystem::is_empty(failedCache, iteratorError);
  passed &= check(failedCacheEmpty && !iteratorError,
    "A failed physical import left a partial cache entry");

  backend.failAtSector = 0xffffffffU;
  QTemporaryDir cancellationDirectory;
  std::uint32_t completedSectors = 0U;
  const auto cancelled = snapshotPhysicalDisc(backend, backend.disc.drive.id,
    pathIn(cancellationDirectory),
    [&completedSectors](std::uint32_t completed, std::uint32_t) {
      completedSectors = completed;
    },
    [&completedSectors] { return completedSectors >= 16U; });
  passed &= check(cancelled.status.error == PhysicalMediaError::cancelled,
    "Physical-disc snapshot cancellation was not honored between bounded reads");

  QTemporaryDir serviceDirectory;
  auto serviceBackend =
    std::make_shared<genplusgx::test::SyntheticPhysicalMediaBackend>();
  PhysicalMediaService service{serviceBackend, pathIn(serviceDirectory), 2U, 64U};
  passed &= check(service.start(), "The physical-media service did not start");
  passed &= check(service.discover(101U), "Drive discovery could not be queued");
  const auto discovery = waitFor(
    service, 101U, {PhysicalMediaEventType::discoveryReady,
      PhysicalMediaEventType::operationFailed});
  passed &= check(discovery && discovery->succeeded() &&
      discovery->drives == std::vector<PhysicalDrive>{serviceBackend->disc.drive},
    "The worker did not return deterministic optical-drive discovery");
  passed &= check(service.importDisc(102U, serviceBackend->disc.drive.id),
    "Physical-disc import could not be queued");
  const auto imported = waitFor(service, 102U,
    {PhysicalMediaEventType::importReady,
      PhysicalMediaEventType::operationFailed});
  passed &= check(imported && imported->succeeded() && imported->snapshot.valid(),
    "The worker did not complete a physical-disc import");
  if (imported && imported->snapshot.valid()) {
    passed &= check(releasePhysicalMediaSnapshot(
        pathIn(serviceDirectory), imported->snapshot),
      "The service-produced snapshot could not be released");
  }

  serviceBackend->readDelay = 2ms;
  passed &= check(service.importDisc(103U, serviceBackend->disc.drive.id),
    "The leading queued import could not be submitted");
  const auto leadingStarted = waitFor(
    service, 103U, {PhysicalMediaEventType::importStarted});
  passed &= check(leadingStarted.has_value() &&
      service.importDisc(104U, serviceBackend->disc.drive.id) &&
      service.cancel(104U),
    "A queued physical-disc import could not be cancelled");
  const auto leadingImport = waitFor(service, 103U,
    {PhysicalMediaEventType::importReady,
      PhysicalMediaEventType::operationFailed});
  const auto queuedCancellation = waitFor(service, 104U,
    {PhysicalMediaEventType::operationCancelled,
      PhysicalMediaEventType::importReady,
      PhysicalMediaEventType::operationFailed});
  passed &= check(leadingImport && leadingImport->succeeded() &&
      queuedCancellation && queuedCancellation->type ==
        PhysicalMediaEventType::operationCancelled,
    "Cancellation was lost while a physical-disc import was queued");
  if (leadingImport && leadingImport->snapshot.valid()) {
    passed &= check(releasePhysicalMediaSnapshot(
        pathIn(serviceDirectory), leadingImport->snapshot),
      "The leading queued-import snapshot could not be released");
  }

  passed &= check(service.importDisc(105U, serviceBackend->disc.drive.id),
    "The cancellable import could not be queued");
  const auto started = waitFor(
    service, 105U, {PhysicalMediaEventType::importStarted});
  passed &= check(started.has_value() && service.cancel(105U),
    "The active physical-disc import could not be cancelled");
  const auto cancelledEvent = waitFor(service, 105U,
    {PhysicalMediaEventType::operationCancelled,
      PhysicalMediaEventType::importReady,
      PhysicalMediaEventType::operationFailed});
  passed &= check(cancelledEvent &&
      cancelledEvent->type == PhysicalMediaEventType::operationCancelled,
    "The worker did not publish a terminal cancellation event");
  passed &= check(service.stop(), "The physical-media service did not stop cleanly");
  passed &= check(service.discover(106U).error == PhysicalMediaError::notRunning,
    "The stopped physical-media service accepted new work");

  return passed ? 0 : 1;
}
