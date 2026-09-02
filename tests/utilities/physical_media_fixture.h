#pragma once

#include "genplusgx/platform/physical_media.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace genplusgx::test {

class SyntheticPhysicalMediaBackend final
  : public platform::PhysicalMediaBackend {
public:
  SyntheticPhysicalMediaBackend();

  [[nodiscard]] platform::PhysicalMediaStatus discover(
    std::vector<platform::PhysicalDrive>& drives) override;
  [[nodiscard]] platform::PhysicalDiscOpenResult open(
    const std::string& driveId) override;

  platform::PhysicalDisc disc;
  std::vector<std::uint8_t> dataTrack;
  bool failDiscovery{false};
  bool failOpen{false};
  std::uint32_t failAtSector{0xffffffffU};
  std::chrono::milliseconds readDelay{0};
  std::atomic_uint32_t readCalls{0U};
};

} // namespace genplusgx::test
