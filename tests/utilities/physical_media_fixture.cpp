#include "physical_media_fixture.h"

#include "synthetic_rom.h"

#include <algorithm>
#include <array>
#include <thread>
#include <utility>

namespace genplusgx::test {
namespace {

constexpr std::uint32_t dataTrackSectors = 150U;
constexpr std::uint32_t audioTrackSectors = 75U;
constexpr std::size_t cookedSectorBytes = 2'048U;

class SyntheticPhysicalDiscReader final
  : public platform::PhysicalDiscReader {
public:
  explicit SyntheticPhysicalDiscReader(SyntheticPhysicalMediaBackend& backend)
    : backend_(backend)
  {
  }

  [[nodiscard]] const platform::PhysicalDisc& disc() const noexcept override
  {
    return backend_.disc;
  }

  [[nodiscard]] platform::PhysicalMediaStatus readRawSectors(
    std::uint32_t firstSector,
    std::uint32_t sectorCount,
    std::span<std::uint8_t> destination) override
  {
    ++backend_.readCalls;
    if (backend_.readDelay.count() > 0) {
      std::this_thread::sleep_for(backend_.readDelay);
    }
    const auto bytes = static_cast<std::size_t>(sectorCount) *
      platform::compactDiscRawSectorBytes;
    if (sectorCount == 0U || destination.size() != bytes ||
        firstSector >= backend_.disc.leadOutSector ||
        sectorCount > backend_.disc.leadOutSector - firstSector) {
      return {.error = platform::PhysicalMediaError::invalidRequest,
        .message = "Synthetic physical-sector request was invalid."};
    }
    if (firstSector <= backend_.failAtSector &&
        backend_.failAtSector < firstSector + sectorCount) {
      return {.error = platform::PhysicalMediaError::readFailed,
        .message = "Injected synthetic physical-media read failure."};
    }
    for (std::uint32_t index = 0U; index < sectorCount; ++index) {
      const auto sector = firstSector + index;
      auto raw = destination.subspan(
        static_cast<std::size_t>(index) * platform::compactDiscRawSectorBytes,
        platform::compactDiscRawSectorBytes);
      std::ranges::fill(raw, std::uint8_t{0U});
      if (sector < dataTrackSectors) {
        constexpr std::array<std::uint8_t, 12U> sync{
          0x00U, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
          0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0x00U};
        std::ranges::copy(sync, raw.begin());
        raw[15U] = 0x01U;
        const auto source = backend_.dataTrack.begin() +
          static_cast<std::ptrdiff_t>(
            static_cast<std::size_t>(sector) * cookedSectorBytes);
        std::copy_n(source, cookedSectorBytes, raw.begin() + 16);
      } else {
        for (std::size_t byte = 0U; byte < raw.size(); ++byte) {
          raw[byte] = static_cast<std::uint8_t>(
            (sector * 17U + static_cast<std::uint32_t>(byte)) & 0xffU);
        }
      }
    }
    return {};
  }

private:
  SyntheticPhysicalMediaBackend& backend_;
};

} // namespace

SyntheticPhysicalMediaBackend::SyntheticPhysicalMediaBackend()
  : disc{
      .drive = {.id = "synthetic-drive",
        .displayName = "Synthetic Test Optical Drive"},
      .tracks = {
        {.number = 1U,
          .type = platform::PhysicalTrackType::mode1Data,
          .startSector = 0U},
        {.number = 2U,
          .type = platform::PhysicalTrackType::audio,
          .startSector = dataTrackSectors},
      },
      .leadOutSector = dataTrackSectors + audioTrackSectors,
    },
    dataTrack(makeSegaCdDiscImage())
{
}

platform::PhysicalMediaStatus SyntheticPhysicalMediaBackend::discover(
  std::vector<platform::PhysicalDrive>& drives)
{
  drives.clear();
  if (failDiscovery) {
    return {.error = platform::PhysicalMediaError::discoveryFailed,
      .message = "Injected synthetic drive-discovery failure."};
  }
  drives.push_back(disc.drive);
  return {};
}

platform::PhysicalDiscOpenResult SyntheticPhysicalMediaBackend::open(
  const std::string& driveId)
{
  if (failOpen || driveId != disc.drive.id) {
    return {
      .status = {.error = platform::PhysicalMediaError::driveUnavailable,
        .message = "Injected synthetic optical-drive open failure."},
      .reader = {},
    };
  }
  return {
    .status = {},
    .reader = std::make_unique<SyntheticPhysicalDiscReader>(*this),
  };
}

} // namespace genplusgx::test
