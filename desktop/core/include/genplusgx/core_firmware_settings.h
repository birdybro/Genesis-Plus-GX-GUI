#pragma once

#include <cstddef>
#include <filesystem>

namespace genplusgx {

struct CoreFirmwareSettings final {
  std::filesystem::path genesis;
  std::filesystem::path masterSystemUsa;
  std::filesystem::path masterSystemEurope;
  std::filesystem::path masterSystemJapan;
  std::filesystem::path gameGear;
  std::filesystem::path segaCdUsa;
  std::filesystem::path segaCdEurope;
  std::filesystem::path segaCdJapan;

  [[nodiscard]] bool operator==(const CoreFirmwareSettings&) const = default;
};

inline constexpr std::size_t maximumCoreFirmwarePathBytes = 4'095U;

[[nodiscard]] inline bool validateCoreFirmwareSettings(
  const CoreFirmwareSettings& settings)
{
  const auto validPath = [](const std::filesystem::path& path) {
    return path.empty() || path.string().size() <= maximumCoreFirmwarePathBytes;
  };
  return validPath(settings.genesis) &&
         validPath(settings.masterSystemUsa) &&
         validPath(settings.masterSystemEurope) &&
         validPath(settings.masterSystemJapan) &&
         validPath(settings.gameGear) &&
         validPath(settings.segaCdUsa) &&
         validPath(settings.segaCdEurope) &&
         validPath(settings.segaCdJapan);
}

} // namespace genplusgx
