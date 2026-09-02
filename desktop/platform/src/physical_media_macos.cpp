#include "genplusgx/platform/physical_media.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOCDMedia.h>
#include <IOKit/storage/IOCDMediaBSDClient.h>
#include <IOKit/storage/IOCDTypes.h>
#include <IOKit/storage/IOMedia.h>
#include <sys/ioctl.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string>
#include <unistd.h>
#include <utility>

namespace genplusgx::platform {
namespace {

PhysicalMediaStatus systemFailure(
  PhysicalMediaError error, std::string message, int systemError = errno)
{
  if (systemError != 0) {
    message += ": ";
    message += std::strerror(systemError);
  }
  return {.error = error, .message = std::move(message)};
}

class FileDescriptor final {
public:
  explicit FileDescriptor(int value = -1) noexcept : value_(value) {}
  ~FileDescriptor()
  {
    if (value_ >= 0) {
      static_cast<void>(::close(value_));
    }
  }
  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;
  FileDescriptor(FileDescriptor&& other) noexcept
    : value_(std::exchange(other.value_, -1))
  {
  }
  [[nodiscard]] int get() const noexcept { return value_; }

private:
  int value_{-1};
};

std::uint32_t sectorFromMsf(const CDMSF& address) noexcept
{
  const auto absolute = static_cast<std::uint32_t>(address.minute) * 60U * 75U +
    static_cast<std::uint32_t>(address.second) * 75U + address.frame;
  return absolute >= 150U ? absolute - 150U : 0U;
}

class MacDiscReader final : public PhysicalDiscReader {
public:
  MacDiscReader(FileDescriptor descriptor, PhysicalDisc disc)
    : descriptor_(std::move(descriptor)), disc_(std::move(disc))
  {
  }

  [[nodiscard]] const PhysicalDisc& disc() const noexcept override
  {
    return disc_;
  }

  [[nodiscard]] PhysicalMediaStatus readRawSectors(
    std::uint32_t firstSector,
    std::uint32_t sectorCount,
    std::span<std::uint8_t> destination) override
  {
    const auto required = static_cast<std::size_t>(sectorCount) *
      compactDiscRawSectorBytes;
    if (sectorCount == 0U || destination.size() != required ||
        firstSector >= disc_.leadOutSector ||
        sectorCount > disc_.leadOutSector - firstSector) {
      return {.error = PhysicalMediaError::invalidRequest,
        .message = "The macOS raw-sector read request is out of range."};
    }
    const auto next = std::upper_bound(disc_.tracks.begin(), disc_.tracks.end(),
      firstSector, [](std::uint32_t value, const PhysicalTrack& track) {
        return value < track.startSector;
      });
    const auto index = next == disc_.tracks.begin()
      ? 0U
      : static_cast<std::size_t>(
          std::distance(disc_.tracks.begin(), next) - 1);
    const auto trackEnd = index + 1U < disc_.tracks.size()
      ? disc_.tracks[index + 1U].startSector
      : disc_.leadOutSector;
    if (sectorCount > trackEnd - firstSector) {
      return {.error = PhysicalMediaError::invalidRequest,
        .message = "A raw-sector read may not cross physical tracks."};
    }
    dk_cd_read_t request{};
    request.buffer = destination.data();
    request.bufferLength = static_cast<std::uint32_t>(required);
    if (disc_.tracks[index].type == PhysicalTrackType::audio) {
      request.offset = static_cast<std::uint64_t>(firstSector) *
        kCDSectorSizeCDDA;
      request.sectorArea = kCDSectorAreaUser;
      request.sectorType = kCDSectorTypeCDDA;
    } else {
      request.offset = static_cast<std::uint64_t>(firstSector) *
        kCDSectorSizeMode1;
      request.sectorArea = kCDSectorAreaSync | kCDSectorAreaHeader |
        kCDSectorAreaUser | kCDSectorAreaAuxiliary;
      request.sectorType = kCDSectorTypeMode1;
    }
    if (::ioctl(descriptor_.get(), DKIOCCDREAD, &request) != 0 ||
        request.bufferLength != static_cast<std::uint32_t>(required)) {
      return systemFailure(PhysicalMediaError::readFailed,
        "The macOS optical drive could not read raw sectors");
    }
    return {};
  }

private:
  FileDescriptor descriptor_;
  PhysicalDisc disc_;
};

class MacPhysicalMediaBackend final : public PhysicalMediaBackend {
public:
  [[nodiscard]] PhysicalMediaStatus discover(
    std::vector<PhysicalDrive>& drives) override
  {
    drives.clear();
    io_iterator_t iterator = IO_OBJECT_NULL;
    const auto result = IOServiceGetMatchingServices(kIOMainPortDefault,
      IOServiceMatching(kIOCDMediaClass), &iterator);
    if (result != KERN_SUCCESS) {
      return {.error = PhysicalMediaError::discoveryFailed,
        .message = "macOS could not enumerate optical media."};
    }
    while (const auto service = IOIteratorNext(iterator)) {
      const auto property = IORegistryEntryCreateCFProperty(service,
        CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0U);
      if (property != nullptr && CFGetTypeID(property) == CFStringGetTypeID()) {
        std::array<char, 128U> name{};
        if (CFStringGetCString(static_cast<CFStringRef>(property), name.data(),
              static_cast<CFIndex>(name.size()), kCFStringEncodingUTF8)) {
          const std::string id = "/dev/r" + std::string{name.data()};
          drives.push_back({
            .id = id,
            .displayName = "Optical Drive (" + id + ')',
          });
        }
      }
      if (property != nullptr) {
        CFRelease(property);
      }
      IOObjectRelease(service);
    }
    IOObjectRelease(iterator);
    return {};
  }

  [[nodiscard]] PhysicalDiscOpenResult open(
    const std::string& driveId) override
  {
    if (!driveId.starts_with("/dev/r") || driveId.size() > 127U) {
      return {.status = {.error = PhysicalMediaError::invalidRequest,
        .message = "The macOS optical-drive path is invalid."}, .reader = {}};
    }
    FileDescriptor descriptor{
      ::open(driveId.c_str(), O_RDONLY | O_NONBLOCK)};
    if (descriptor.get() < 0) {
      const auto error = errno;
      return {.status = systemFailure(
        error == EACCES || error == EPERM
          ? PhysicalMediaError::permissionDenied
          : PhysicalMediaError::driveUnavailable,
        error == EACCES || error == EPERM
          ? "Permission to read the macOS optical drive was denied"
          : "The macOS optical drive could not be opened",
        error), .reader = {}};
    }
    std::array<std::uint8_t,
      sizeof(CDTOC) + 100U * sizeof(CDTOCDescriptor)> buffer{};
    dk_cd_read_toc_t request{};
    request.format = kCDTOCFormatTOC;
    request.formatAsTime = 1U;
    request.buffer = buffer.data();
    request.bufferLength = static_cast<std::uint16_t>(buffer.size());
    if (::ioctl(descriptor.get(), DKIOCCDREADTOC, &request) != 0) {
      const auto error = errno;
      return {.status = systemFailure(
        error == ENXIO || error == EBUSY
          ? PhysicalMediaError::noMedia
          : PhysicalMediaError::readFailed,
        error == ENXIO || error == EBUSY
          ? "The selected macOS optical drive has no ready disc"
          : "The macOS optical drive table of contents could not be read",
        error), .reader = {}};
    }
    if (request.bufferLength < sizeof(CDTOC)) {
      return {.status = {.error = PhysicalMediaError::invalidTableOfContents,
        .message = "The macOS optical-disc table of contents is incomplete."},
        .reader = {}};
    }
    auto* toc = reinterpret_cast<CDTOC*>(buffer.data());
    const auto descriptorCount = CDTOCGetDescriptorCount(toc);
    if (descriptorCount <= 0 || descriptorCount > 100) {
      return {.status = {.error = PhysicalMediaError::invalidTableOfContents,
        .message = "The macOS optical-disc descriptor count is invalid."},
        .reader = {}};
    }
    PhysicalDisc disc{
      .drive = {.id = driveId,
        .displayName = "Optical Drive (" + driveId + ')'},
      .tracks = {},
      .leadOutSector = 0U,
    };
    for (UInt32 index = 0U; index < descriptorCount; ++index) {
      const auto& entry = toc->descriptors[index];
      if (entry.adr != 1U) {
        continue;
      }
      if (entry.point == 0xA2U) {
        disc.leadOutSector = sectorFromMsf(entry.p);
      } else if (entry.point >= 1U && entry.point <= 99U) {
        disc.tracks.push_back({
          .number = entry.point,
          .type = (entry.control & 0x04U) != 0U
            ? PhysicalTrackType::mode1Data
            : PhysicalTrackType::audio,
          .startSector = sectorFromMsf(entry.p),
        });
      }
    }
    std::ranges::sort(disc.tracks,
      [](const PhysicalTrack& left, const PhysicalTrack& right) {
        return left.number < right.number;
      });
    if (const auto valid = validatePhysicalDisc(disc); !valid) {
      return {.status = valid, .reader = {}};
    }
    return {
      .status = {},
      .reader = std::make_unique<MacDiscReader>(
        std::move(descriptor), std::move(disc)),
    };
  }
};

} // namespace

bool nativePhysicalMediaSupported() noexcept { return true; }

std::shared_ptr<PhysicalMediaBackend> createNativePhysicalMediaBackend()
{
  return std::make_shared<MacPhysicalMediaBackend>();
}

} // namespace genplusgx::platform
