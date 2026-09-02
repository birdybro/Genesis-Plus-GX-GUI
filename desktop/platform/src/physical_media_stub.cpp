#include "genplusgx/platform/physical_media.h"

#include <memory>

namespace genplusgx::platform {
namespace {

class UnsupportedPhysicalMediaBackend final : public PhysicalMediaBackend {
public:
  [[nodiscard]] PhysicalMediaStatus discover(
    std::vector<PhysicalDrive>& drives) override
  {
    drives.clear();
    return {.error = PhysicalMediaError::unsupportedPlatform,
      .message = "Physical optical-media access is unavailable on this platform."};
  }

  [[nodiscard]] PhysicalDiscOpenResult open(const std::string&) override
  {
    return {.status = {.error = PhysicalMediaError::unsupportedPlatform,
      .message = "Physical optical-media access is unavailable on this platform."},
      .reader = {}};
  }
};

} // namespace

bool nativePhysicalMediaSupported() noexcept { return false; }

std::shared_ptr<PhysicalMediaBackend> createNativePhysicalMediaBackend()
{
  return std::make_shared<UnsupportedPhysicalMediaBackend>();
}

} // namespace genplusgx::platform
