#pragma once

#include <cstdint>

namespace genplusgx {

struct StereoAudioFrame final {
  std::int16_t left{0};
  std::int16_t right{0};

  friend bool operator==(const StereoAudioFrame&, const StereoAudioFrame&) = default;
};

static_assert(sizeof(StereoAudioFrame) == sizeof(std::int16_t) * 2U);

} // namespace genplusgx
