#include "genplusgx/platform/physical_media.h"

#include <windows.h>
#include <winioctl.h>
#include <ntddcdrm.h>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace genplusgx::platform {
namespace {

std::string windowsMessage(DWORD code)
{
  LPWSTR buffer = nullptr;
  const auto length = FormatMessageW(
    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
      FORMAT_MESSAGE_IGNORE_INSERTS,
    nullptr, code, 0U, reinterpret_cast<LPWSTR>(&buffer), 0U, nullptr);
  std::string result;
  if (length != 0U && buffer != nullptr) {
    const auto required = WideCharToMultiByte(
      CP_UTF8, 0, buffer, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (required > 0) {
      result.resize(static_cast<std::size_t>(required));
      static_cast<void>(WideCharToMultiByte(CP_UTF8, 0, buffer,
        static_cast<int>(length), result.data(), required, nullptr, nullptr));
    }
    LocalFree(buffer);
  }
  while (!result.empty() &&
         (result.back() == '\r' || result.back() == '\n' || result.back() == ' ')) {
    result.pop_back();
  }
  return result.empty() ? "Windows error " + std::to_string(code) : result;
}

PhysicalMediaStatus systemFailure(
  PhysicalMediaError error, std::string message, DWORD code = GetLastError())
{
  return {.error = error,
    .message = std::move(message) + ": " + windowsMessage(code)};
}

class NativeHandle final {
public:
  explicit NativeHandle(HANDLE value = INVALID_HANDLE_VALUE) noexcept
    : value_(value)
  {
  }
  ~NativeHandle()
  {
    if (value_ != INVALID_HANDLE_VALUE) {
      CloseHandle(value_);
    }
  }
  NativeHandle(const NativeHandle&) = delete;
  NativeHandle& operator=(const NativeHandle&) = delete;
  NativeHandle(NativeHandle&& other) noexcept
    : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE))
  {
  }
  NativeHandle& operator=(NativeHandle&& other) noexcept
  {
    if (this != &other) {
      if (value_ != INVALID_HANDLE_VALUE) {
        CloseHandle(value_);
      }
      value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE);
    }
    return *this;
  }
  [[nodiscard]] HANDLE get() const noexcept { return value_; }

private:
  HANDLE value_{INVALID_HANDLE_VALUE};
};

std::uint32_t sectorFromAddress(const UCHAR (&address)[4]) noexcept
{
  const auto absolute = static_cast<std::uint32_t>(address[1]) * 60U * 75U +
    static_cast<std::uint32_t>(address[2]) * 75U + address[3];
  return absolute >= 150U ? absolute - 150U : 0U;
}

class WindowsDiscReader final : public PhysicalDiscReader {
public:
  WindowsDiscReader(NativeHandle handle, PhysicalDisc disc)
    : handle_(std::move(handle)), disc_(std::move(disc))
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
        required > (std::numeric_limits<DWORD>::max)() ||
        firstSector >= disc_.leadOutSector ||
        sectorCount > disc_.leadOutSector - firstSector) {
      return {.error = PhysicalMediaError::invalidRequest,
        .message = "The Windows raw-sector read request is out of range."};
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
    RAW_READ_INFO request{};
    request.DiskOffset.QuadPart =
      static_cast<LONGLONG>(firstSector) * 2'048LL;
    request.SectorCount = sectorCount;
    request.TrackMode = disc_.tracks[index].type == PhysicalTrackType::audio
      ? CDDA
      : YellowMode2;
    DWORD returned = 0U;
    if (!DeviceIoControl(handle_.get(), IOCTL_CDROM_RAW_READ,
          &request, sizeof(request), destination.data(),
          static_cast<DWORD>(required), &returned, nullptr) ||
        returned != required) {
      return systemFailure(PhysicalMediaError::readFailed,
        "The Windows optical drive could not read raw sectors");
    }
    return {};
  }

private:
  NativeHandle handle_;
  PhysicalDisc disc_;
};

class WindowsPhysicalMediaBackend final : public PhysicalMediaBackend {
public:
  [[nodiscard]] PhysicalMediaStatus discover(
    std::vector<PhysicalDrive>& drives) override
  {
    drives.clear();
    const DWORD mask = GetLogicalDrives();
    if (mask == 0U) {
      return systemFailure(PhysicalMediaError::discoveryFailed,
        "Windows could not enumerate logical drives");
    }
    for (unsigned index = 0U; index < 26U; ++index) {
      if ((mask & (DWORD{1U} << index)) == 0U) {
        continue;
      }
      const wchar_t letter = static_cast<wchar_t>(L'A' + index);
      std::wstring root{letter, L':', L'\\', L'\0'};
      if (GetDriveTypeW(root.c_str()) != DRIVE_CDROM) {
        continue;
      }
      const std::string id{static_cast<char>('A' + index), ':'};
      drives.push_back({
        .id = id,
        .displayName = "Optical Drive (" + id + ')',
      });
    }
    return {};
  }

  [[nodiscard]] PhysicalDiscOpenResult open(
    const std::string& driveId) override
  {
    if (driveId.size() != 2U || driveId[1] != ':' ||
        driveId[0] < 'A' || driveId[0] > 'Z') {
      return {.status = {.error = PhysicalMediaError::invalidRequest,
        .message = "The Windows optical-drive identifier is invalid."},
        .reader = {}};
    }
    const std::wstring device{L'\\', L'\\', L'.', L'\\',
      static_cast<wchar_t>(driveId[0]), L':', L'\0'};
    NativeHandle handle{CreateFileW(device.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (handle.get() == INVALID_HANDLE_VALUE) {
      const auto error = GetLastError();
      return {.status = systemFailure(
        error == ERROR_ACCESS_DENIED
          ? PhysicalMediaError::permissionDenied
          : PhysicalMediaError::driveUnavailable,
        error == ERROR_ACCESS_DENIED
          ? "Permission to read the Windows optical drive was denied"
          : "The Windows optical drive could not be opened",
        error), .reader = {}};
    }
    CDROM_TOC toc{};
    DWORD returned = 0U;
    if (!DeviceIoControl(handle.get(), IOCTL_CDROM_READ_TOC,
          nullptr, 0U, &toc, sizeof(toc), &returned, nullptr)) {
      const auto error = GetLastError();
      return {.status = systemFailure(
        error == ERROR_NOT_READY ? PhysicalMediaError::noMedia
                                 : PhysicalMediaError::readFailed,
        error == ERROR_NOT_READY
          ? "The selected Windows optical drive has no ready disc"
          : "The Windows optical drive table of contents could not be read",
        error), .reader = {}};
    }
    const auto trackCount = static_cast<unsigned>(
      toc.LastTrack - toc.FirstTrack + 1U);
    if (trackCount == 0U || trackCount > 99U ||
        returned < sizeof(toc.Length) + 2U +
          (trackCount + 1U) * sizeof(TRACK_DATA)) {
      return {.status = {.error = PhysicalMediaError::invalidTableOfContents,
        .message = "The Windows optical-disc table of contents is incomplete."},
        .reader = {}};
    }
    PhysicalDisc disc{
      .drive = {.id = driveId,
        .displayName = "Optical Drive (" + driveId + ')'},
      .tracks = {},
      .leadOutSector = 0U,
    };
    for (unsigned index = 0U; index < trackCount; ++index) {
      const auto& track = toc.TrackData[index];
      disc.tracks.push_back({
        .number = track.TrackNumber,
        .type = (track.Control & 0x04U) != 0U
          ? PhysicalTrackType::mode1Data
          : PhysicalTrackType::audio,
        .startSector = sectorFromAddress(track.Address),
      });
    }
    disc.leadOutSector = sectorFromAddress(toc.TrackData[trackCount].Address);
    if (const auto valid = validatePhysicalDisc(disc); !valid) {
      return {.status = valid, .reader = {}};
    }
    return {
      .status = {},
      .reader = std::make_unique<WindowsDiscReader>(
        std::move(handle), std::move(disc)),
    };
  }
};

} // namespace

bool nativePhysicalMediaSupported() noexcept { return true; }

std::shared_ptr<PhysicalMediaBackend> createNativePhysicalMediaBackend()
{
  return std::make_shared<WindowsPhysicalMediaBackend>();
}

} // namespace genplusgx::platform
