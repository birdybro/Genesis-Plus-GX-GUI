#pragma once

#include <cstdint>

namespace genplusgx {

enum class CoreSoundOutput : std::uint8_t { stereo, mono };
enum class CoreAudioFilter : std::uint8_t { disabled, lowPass, equalizer };
enum class CoreYm2612Core : std::uint8_t {
  mameDiscrete,
  mameIntegrated,
  mameEnhanced,
  nukedYm2612,
  nukedYm3438,
};
enum class CoreYm2413Mode : std::uint8_t { disabled, enabled, autoDetect };
enum class CoreYm2413Core : std::uint8_t { mame, nuked };

struct CoreAudioSettings final {
  CoreSoundOutput output{CoreSoundOutput::stereo};
  CoreAudioFilter filter{CoreAudioFilter::lowPass};
  CoreYm2612Core ym2612Core{CoreYm2612Core::mameDiscrete};
  CoreYm2413Mode ym2413Mode{CoreYm2413Mode::autoDetect};
  CoreYm2413Core ym2413Core{CoreYm2413Core::mame};
  int psgLevelPercent{150};
  int fmLevelPercent{100};
  int cddaLevelPercent{100};
  int pcmLevelPercent{100};
  int lowPassPercent{60};
  int equalizerLowPercent{100};
  int equalizerMidPercent{100};
  int equalizerHighPercent{100};
  bool highQualityFm{true};
  bool highQualityPsg{true};

  [[nodiscard]] bool operator==(const CoreAudioSettings&) const = default;
};

[[nodiscard]] constexpr bool validateCoreAudioSettings(
  const CoreAudioSettings& settings) noexcept
{
  return static_cast<unsigned>(settings.output) <=
      static_cast<unsigned>(CoreSoundOutput::mono) &&
    static_cast<unsigned>(settings.filter) <=
      static_cast<unsigned>(CoreAudioFilter::equalizer) &&
    static_cast<unsigned>(settings.ym2612Core) <=
      static_cast<unsigned>(CoreYm2612Core::nukedYm3438) &&
    static_cast<unsigned>(settings.ym2413Mode) <=
      static_cast<unsigned>(CoreYm2413Mode::autoDetect) &&
    static_cast<unsigned>(settings.ym2413Core) <=
      static_cast<unsigned>(CoreYm2413Core::nuked) &&
    settings.psgLevelPercent >= 0 && settings.psgLevelPercent <= 200 &&
    settings.fmLevelPercent >= 0 && settings.fmLevelPercent <= 200 &&
    settings.cddaLevelPercent >= 0 && settings.cddaLevelPercent <= 100 &&
    settings.pcmLevelPercent >= 0 && settings.pcmLevelPercent <= 100 &&
    settings.lowPassPercent >= 5 && settings.lowPassPercent <= 95 &&
    settings.equalizerLowPercent >= 0 && settings.equalizerLowPercent <= 200 &&
    settings.equalizerMidPercent >= 0 && settings.equalizerMidPercent <= 200 &&
    settings.equalizerHighPercent >= 0 && settings.equalizerHighPercent <= 200;
}

} // namespace genplusgx
