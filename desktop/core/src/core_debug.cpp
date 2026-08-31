#include "genplusgx/core_debug.h"

namespace genplusgx {

std::array<CoreDebugMemoryRegionInfo, 7>
coreDebugMemoryRegions(std::uint32_t romSize) noexcept
{
  return {{
    {CoreDebugMemoryRegion::rom, "ROM", 0x000000U, romSize, true},
    {CoreDebugMemoryRegion::m68kRam, "68K RAM", 0xFF0000U, 0x10000U, true},
    {CoreDebugMemoryRegion::z80Ram, "Z80 RAM", 0x0000U, 0x2000U, true},
    {CoreDebugMemoryRegion::vram, "VRAM", 0x0000U, 0x10000U, true},
    {CoreDebugMemoryRegion::cram, "CRAM", 0x0000U, 0x80U, true},
    {CoreDebugMemoryRegion::vsram, "VSRAM", 0x0000U, 0x80U, true},
    {CoreDebugMemoryRegion::vdpRegisters,
      "VDP registers", 0x0000U, 0x20U, true},
  }};
}

std::uint32_t coreDebugCramColor(std::uint16_t packedColor) noexcept
{
  const auto red = static_cast<std::uint32_t>((packedColor >> 0U) & 0x7U);
  const auto green = static_cast<std::uint32_t>((packedColor >> 3U) & 0x7U);
  const auto blue = static_cast<std::uint32_t>((packedColor >> 6U) & 0x7U);
  const auto expand = [](std::uint32_t value) {
    return (value << 5U) | (value << 2U) | (value >> 1U);
  };
  return 0xFF000000U | (expand(red) << 16U) | (expand(green) << 8U) |
    expand(blue);
}

} // namespace genplusgx
