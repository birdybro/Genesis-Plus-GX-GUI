#pragma once

#include <cstdint>

namespace genplusgx {

enum class CoreOverscanMode : std::uint8_t {
  disabled = 0,
  vertical = 1,
  horizontal = 2,
  full = 3,
};

enum class CoreNtscFilter : std::uint8_t {
  disabled = 0,
  monochrome,
  composite,
  sVideo,
  rgb,
};

enum class CoreInterlacedRenderMode : std::uint8_t {
  singleField = 0,
  doubleField = 1,
};

struct CoreVideoSettings final {
  CoreOverscanMode overscan{CoreOverscanMode::disabled};
  CoreNtscFilter ntscFilter{CoreNtscFilter::disabled};
  CoreInterlacedRenderMode interlacedRender{
    CoreInterlacedRenderMode::singleField};
  bool gameGearExtendedScreen{false};

  [[nodiscard]] bool operator==(const CoreVideoSettings&) const = default;
};

[[nodiscard]] constexpr bool validateCoreVideoSettings(
  const CoreVideoSettings& settings) noexcept
{
  const auto overscan = static_cast<std::uint8_t>(settings.overscan);
  const auto ntsc = static_cast<std::uint8_t>(settings.ntscFilter);
  const auto render = static_cast<std::uint8_t>(settings.interlacedRender);
  return overscan <= static_cast<std::uint8_t>(CoreOverscanMode::full) &&
    ntsc <= static_cast<std::uint8_t>(CoreNtscFilter::rgb) &&
    render <= static_cast<std::uint8_t>(CoreInterlacedRenderMode::doubleField);
}

} // namespace genplusgx
