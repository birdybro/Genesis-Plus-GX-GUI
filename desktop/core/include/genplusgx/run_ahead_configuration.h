#pragma once

#include "genplusgx/core_input_settings.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace genplusgx {

inline constexpr std::uint32_t minimumRunAheadFrames = 1U;
inline constexpr std::uint32_t maximumRunAheadFrames = 4U;

struct RunAheadConfiguration final {
  bool enabled{false};
  std::uint32_t frames{1U};

  friend bool operator==(
    const RunAheadConfiguration&,
    const RunAheadConfiguration&) = default;
};

enum class RunAheadVerificationResult {
  notPending,
  verified,
  mismatch,
};

class RunAheadDeterminismGuard final {
public:
  void reset(bool enabled, bool preserveFailures) noexcept
  {
    pending_ = enabled;
    verified_ = false;
    faulted_ = false;
    if (!preserveFailures) {
      failures_ = 0U;
    }
  }

  [[nodiscard]] RunAheadVerificationResult verify(
    std::span<const std::uint8_t> speculative,
    std::span<const std::uint8_t> authoritative) noexcept
  {
    if (!pending_) {
      return RunAheadVerificationResult::notPending;
    }
    pending_ = false;
    if (speculative.size() != authoritative.size()) {
      faulted_ = true;
      ++failures_;
      return RunAheadVerificationResult::mismatch;
    }
    for (std::size_t index = 0U; index < speculative.size(); ++index) {
      if (speculative[index] != authoritative[index]) {
        faulted_ = true;
        ++failures_;
        return RunAheadVerificationResult::mismatch;
      }
    }
    verified_ = true;
    return RunAheadVerificationResult::verified;
  }

  [[nodiscard]] bool pending() const noexcept { return pending_; }
  [[nodiscard]] bool verified() const noexcept { return verified_; }
  [[nodiscard]] bool faulted() const noexcept { return faulted_; }
  [[nodiscard]] std::uint64_t failures() const noexcept { return failures_; }

private:
  bool pending_{false};
  bool verified_{false};
  bool faulted_{false};
  std::uint64_t failures_{0U};
};

[[nodiscard]] constexpr bool validateRunAheadConfiguration(
  const RunAheadConfiguration& configuration) noexcept
{
  return configuration.frames >= minimumRunAheadFrames &&
    configuration.frames <= maximumRunAheadFrames;
}

[[nodiscard]] constexpr bool runAheadSupportedForHardware(
  std::uint32_t hardware) noexcept
{
  switch (hardware) {
    case 0x01U:
    case 0x02U:
    case 0x03U:
    case 0x10U:
    case 0x20U:
    case 0x21U:
    case 0x40U:
    case 0x41U:
    case 0x80U:
    case 0x81U:
      return true;
    default:
      return false;
  }
}

[[nodiscard]] constexpr bool runAheadSupportedForInputSettings(
  const CoreInputSettings& settings) noexcept
{
  for (const auto device : settings.devices) {
    if (device != CoreInputDevice::none && !isCorePad(device)) {
      return false;
    }
  }
  return true;
}

} // namespace genplusgx
