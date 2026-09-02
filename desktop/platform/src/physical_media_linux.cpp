#include "genplusgx/platform/physical_media.h"

#include <linux/cdrom.h>
#include <sys/ioctl.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <memory>
#include <set>
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
  FileDescriptor& operator=(FileDescriptor&& other) noexcept
  {
    if (this != &other) {
      if (value_ >= 0) {
        static_cast<void>(::close(value_));
      }
      value_ = std::exchange(other.value_, -1);
    }
    return *this;
  }
  [[nodiscard]] int get() const noexcept { return value_; }

private:
  int value_{-1};
};

std::uint32_t sectorFromMsf(const cdrom_msf0& address) noexcept
{
  const auto absolute =
    static_cast<std::uint32_t>(address.minute) * 60U * 75U +
    static_cast<std::uint32_t>(address.second) * 75U + address.frame;
  return absolute >= CD_MSF_OFFSET ? absolute - CD_MSF_OFFSET : 0U;
}

cdrom_msf0 msfFromSector(std::uint32_t sector) noexcept
{
  const auto absolute = sector + CD_MSF_OFFSET;
  return {
    .minute = static_cast<unsigned char>(absolute / (60U * 75U)),
    .second = static_cast<unsigned char>((absolute / 75U) % 60U),
    .frame = static_cast<unsigned char>(absolute % 75U),
  };
}

class LinuxDiscReader final : public PhysicalDiscReader {
public:
  LinuxDiscReader(FileDescriptor descriptor, PhysicalDisc disc)
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
        .message = "The Linux raw-sector read request is out of range."};
    }
    const auto track = std::upper_bound(disc_.tracks.begin(), disc_.tracks.end(),
      firstSector, [](std::uint32_t value, const PhysicalTrack& candidate) {
        return value < candidate.startSector;
      });
    const auto trackIndex = track == disc_.tracks.begin()
      ? 0U
      : static_cast<std::size_t>(
          std::distance(disc_.tracks.begin(), track) - 1);
    const auto trackEnd = trackIndex + 1U < disc_.tracks.size()
      ? disc_.tracks[trackIndex + 1U].startSector
      : disc_.leadOutSector;
    if (sectorCount > trackEnd - firstSector) {
      return {.error = PhysicalMediaError::invalidRequest,
        .message = "A raw-sector read may not cross physical tracks."};
    }
    if (disc_.tracks[trackIndex].type == PhysicalTrackType::audio) {
      cdrom_read_audio request{};
      request.addr.lba = static_cast<int>(firstSector);
      request.addr_format = CDROM_LBA;
      request.nframes = static_cast<int>(sectorCount);
      request.buf = destination.data();
      if (::ioctl(descriptor_.get(), CDROMREADAUDIO, &request) != 0) {
        return systemFailure(PhysicalMediaError::readFailed,
          "The Linux optical drive could not read CDDA sectors");
      }
      return {};
    }
    union RawSectorRequest {
      cdrom_msf msf;
      std::array<unsigned char, CD_FRAMESIZE_RAW> bytes;
    } request{};
    for (std::uint32_t index = 0U; index < sectorCount; ++index) {
      request = {};
      request.msf.cdmsf_min0 = msfFromSector(firstSector + index).minute;
      request.msf.cdmsf_sec0 = msfFromSector(firstSector + index).second;
      request.msf.cdmsf_frame0 = msfFromSector(firstSector + index).frame;
      if (::ioctl(descriptor_.get(), CDROMREADRAW, request.bytes.data()) != 0) {
        return systemFailure(PhysicalMediaError::readFailed,
          "The Linux optical drive could not read a raw data sector");
      }
      std::ranges::copy(request.bytes,
        destination.begin() + static_cast<std::ptrdiff_t>(
          static_cast<std::size_t>(index) * compactDiscRawSectorBytes));
    }
    return {};
  }

private:
  FileDescriptor descriptor_;
  PhysicalDisc disc_;
};

class LinuxPhysicalMediaBackend final : public PhysicalMediaBackend {
public:
  [[nodiscard]] PhysicalMediaStatus discover(
    std::vector<PhysicalDrive>& drives) override
  {
    drives.clear();
    std::vector<std::filesystem::path> candidates{
      "/dev/cdrom", "/dev/cdrw", "/dev/dvd", "/dev/dvdrw"};
    for (unsigned index = 0U; index < 32U; ++index) {
      candidates.emplace_back("/dev/sr" + std::to_string(index));
      candidates.emplace_back("/dev/scd" + std::to_string(index));
    }
    std::set<std::filesystem::path> known;
    for (const auto& candidate : candidates) {
      std::error_code error;
      if (!std::filesystem::exists(candidate, error) || error) {
        continue;
      }
      auto resolved = std::filesystem::weakly_canonical(candidate, error);
      if (error) {
        resolved = candidate.lexically_normal();
      }
      if (!known.insert(resolved).second) {
        continue;
      }
      drives.push_back({
        .id = resolved.string(),
        .displayName = "Optical Drive (" + resolved.string() + ')',
      });
    }
    return {};
  }

  [[nodiscard]] PhysicalDiscOpenResult open(
    const std::string& driveId) override
  {
    if (driveId.empty() || !driveId.starts_with("/dev/") ||
        driveId.size() > 255U) {
      return {.status = {.error = PhysicalMediaError::invalidRequest,
        .message = "The Linux optical-drive path is invalid."}, .reader = {}};
    }
    FileDescriptor descriptor{
      ::open(driveId.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC)};
    if (descriptor.get() < 0) {
      const auto error = errno;
      return {.status = systemFailure(
        error == EACCES || error == EPERM
          ? PhysicalMediaError::permissionDenied
          : PhysicalMediaError::driveUnavailable,
        error == EACCES || error == EPERM
          ? "Permission to read the Linux optical drive was denied"
          : "The Linux optical drive could not be opened",
        error), .reader = {}};
    }
    const int driveStatus = ::ioctl(
      descriptor.get(), CDROM_DRIVE_STATUS, CDSL_CURRENT);
    if (driveStatus == CDS_NO_DISC || driveStatus == CDS_TRAY_OPEN ||
        driveStatus == CDS_DRIVE_NOT_READY) {
      return {.status = {.error = PhysicalMediaError::noMedia,
        .message = "The selected optical drive has no ready disc."},
        .reader = {}};
    }
    cdrom_tochdr header{};
    if (::ioctl(descriptor.get(), CDROMREADTOCHDR, &header) != 0) {
      return {.status = systemFailure(PhysicalMediaError::readFailed,
        "The Linux optical drive table of contents could not be read"),
        .reader = {}};
    }
    PhysicalDisc disc{
      .drive = {.id = driveId,
        .displayName = "Optical Drive (" + driveId + ')'},
      .tracks = {},
      .leadOutSector = 0U,
    };
    for (unsigned number = header.cdth_trk0; number <= header.cdth_trk1;
         ++number) {
      cdrom_tocentry entry{};
      entry.cdte_track = static_cast<unsigned char>(number);
      entry.cdte_format = CDROM_MSF;
      if (::ioctl(descriptor.get(), CDROMREADTOCENTRY, &entry) != 0) {
        return {.status = systemFailure(PhysicalMediaError::readFailed,
          "A Linux optical-disc track entry could not be read"), .reader = {}};
      }
      disc.tracks.push_back({
        .number = static_cast<std::uint8_t>(number),
        .type = (entry.cdte_ctrl & CDROM_DATA_TRACK) != 0
          ? PhysicalTrackType::mode1Data
          : PhysicalTrackType::audio,
        .startSector = sectorFromMsf(entry.cdte_addr.msf),
      });
    }
    cdrom_tocentry leadOut{};
    leadOut.cdte_track = CDROM_LEADOUT;
    leadOut.cdte_format = CDROM_MSF;
    if (::ioctl(descriptor.get(), CDROMREADTOCENTRY, &leadOut) != 0) {
      return {.status = systemFailure(PhysicalMediaError::readFailed,
        "The Linux optical-disc lead-out could not be read"), .reader = {}};
    }
    disc.leadOutSector = sectorFromMsf(leadOut.cdte_addr.msf);
    if (const auto valid = validatePhysicalDisc(disc); !valid) {
      return {.status = valid, .reader = {}};
    }
    return {
      .status = {},
      .reader = std::make_unique<LinuxDiscReader>(
        std::move(descriptor), std::move(disc)),
    };
  }
};

} // namespace

bool nativePhysicalMediaSupported() noexcept { return true; }

std::shared_ptr<PhysicalMediaBackend> createNativePhysicalMediaBackend()
{
  return std::make_shared<LinuxPhysicalMediaBackend>();
}

} // namespace genplusgx::platform
