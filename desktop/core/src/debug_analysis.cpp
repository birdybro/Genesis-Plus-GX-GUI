#include "genplusgx/debug_analysis.h"

#include <algorithm>
#include <limits>

namespace genplusgx {
namespace {

std::size_t widthBytes(DebugValueWidth width) noexcept
{
  return static_cast<std::size_t>(width);
}

bool matches(
  DebugRamComparison comparison,
  std::int64_t current,
  std::int64_t previous,
  std::int64_t value) noexcept
{
  switch (comparison) {
    case DebugRamComparison::equalTo: return current == value;
    case DebugRamComparison::notEqualTo: return current != value;
    case DebugRamComparison::changed: return current != previous;
    case DebugRamComparison::unchanged: return current == previous;
    case DebugRamComparison::increased: return current > previous;
    case DebugRamComparison::decreased: return current < previous;
    case DebugRamComparison::greaterThan: return current > value;
    case DebugRamComparison::lessThan: return current < value;
  }
  return false;
}

} // namespace

bool debugReadValue(
  std::span<const std::uint8_t> memory,
  std::uint32_t offset,
  DebugValueWidth width,
  DebugValueEndian endian,
  std::uint32_t& output) noexcept
{
  const auto bytes = widthBytes(width);
  const auto start = static_cast<std::size_t>(offset);
  if ((bytes != 1U && bytes != 2U && bytes != 4U) ||
      start > memory.size() || bytes > memory.size() - start) {
    output = 0U;
    return false;
  }
  output = 0U;
  if (endian == DebugValueEndian::big) {
    for (std::size_t index = 0U; index < bytes; ++index) {
      output = (output << 8U) | memory[start + index];
    }
  } else {
    for (std::size_t index = bytes; index > 0U; --index) {
      output = (output << 8U) | memory[start + index - 1U];
    }
  }
  return true;
}

std::int64_t debugInterpretValue(
  std::uint32_t value,
  DebugValueWidth width,
  DebugValueFormat format) noexcept
{
  if (format == DebugValueFormat::unsignedInteger) {
    return static_cast<std::int64_t>(value);
  }
  switch (width) {
    case DebugValueWidth::byte:
      return static_cast<std::int8_t>(value & 0xFFU);
    case DebugValueWidth::word:
      return static_cast<std::int16_t>(value & 0xFFFFU);
    case DebugValueWidth::longWord:
      return static_cast<std::int32_t>(value);
  }
  return 0;
}

bool DebugRamSearch::begin(
  std::span<const std::uint8_t> memory,
  DebugValueWidth width,
  DebugValueEndian endian)
{
  clear();
  const auto bytes = widthBytes(width);
  if ((bytes != 1U && bytes != 2U && bytes != 4U) || memory.size() < bytes) {
    return false;
  }
  const auto count = memory.size() - bytes + 1U;
  if (count > maximumCandidates) {
    return false;
  }
  width_ = width;
  endian_ = endian;
  candidates_.reserve(count);
  for (std::size_t offset = 0U; offset < count; ++offset) {
    std::uint32_t value = 0U;
    if (!debugReadValue(memory, static_cast<std::uint32_t>(offset),
          width_, endian_, value)) {
      clear();
      return false;
    }
    candidates_.push_back({
      .offset = static_cast<std::uint32_t>(offset),
      .previousValue = value,
    });
  }
  active_ = true;
  return true;
}

bool DebugRamSearch::filter(
  std::span<const std::uint8_t> memory,
  DebugRamComparison comparison,
  DebugValueFormat format,
  std::int64_t value)
{
  if (!active_) {
    return false;
  }
  std::vector<DebugRamCandidate> retained;
  retained.reserve(candidates_.size());
  for (const auto& candidate : candidates_) {
    std::uint32_t currentValue = 0U;
    if (!debugReadValue(
          memory, candidate.offset, width_, endian_, currentValue)) {
      return false;
    }
    const auto current = debugInterpretValue(currentValue, width_, format);
    const auto previous = debugInterpretValue(
      candidate.previousValue, width_, format);
    if (matches(comparison, current, previous, value)) {
      retained.push_back({
        .offset = candidate.offset,
        .previousValue = currentValue,
      });
    }
  }
  candidates_ = std::move(retained);
  return true;
}

void DebugRamSearch::clear() noexcept
{
  candidates_.clear();
  active_ = false;
}

bool DebugRamSearch::active() const noexcept
{
  return active_;
}

DebugValueWidth DebugRamSearch::width() const noexcept
{
  return width_;
}

DebugValueEndian DebugRamSearch::endian() const noexcept
{
  return endian_;
}

const std::vector<DebugRamCandidate>& DebugRamSearch::candidates() const noexcept
{
  return candidates_;
}

} // namespace genplusgx
