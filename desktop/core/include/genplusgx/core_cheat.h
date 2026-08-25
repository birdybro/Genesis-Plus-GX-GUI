#pragma once

#include <cstddef>
#include <cstdint>

namespace genplusgx {

enum class CoreCheatWidth : std::uint8_t {
  byte = 1U,
  word = 2U,
};

struct CoreCheatPatch final {
  std::uint32_t address{0};
  std::uint16_t data{0};
  std::uint8_t reference{0};
  CoreCheatWidth width{CoreCheatWidth::word};
  bool referenceRequired{false};

  [[nodiscard]] bool operator==(const CoreCheatPatch&) const = default;
};

inline constexpr std::size_t maximumCoreCheatPatches = 150U;

[[nodiscard]] inline bool validateCoreCheatPatch(const CoreCheatPatch& patch) noexcept
{
  return patch.address <= 0x00ff'ffffU &&
         (patch.width == CoreCheatWidth::byte || patch.width == CoreCheatWidth::word) &&
         (!patch.referenceRequired || patch.width == CoreCheatWidth::byte);
}

} // namespace genplusgx
