#pragma once

#include "genplusgx/core_debug.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace genplusgx {

enum class DebugValueWidth : std::uint8_t {
  byte = 1,
  word = 2,
  longWord = 4,
};

enum class DebugValueEndian : std::uint8_t {
  big,
  little,
};

enum class DebugValueFormat : std::uint8_t {
  unsignedInteger,
  signedInteger,
};

enum class DebugRamComparison : std::uint8_t {
  equalTo,
  notEqualTo,
  changed,
  unchanged,
  increased,
  decreased,
  greaterThan,
  lessThan,
};

struct DebugRamCandidate final {
  std::uint32_t offset{0};
  std::uint32_t previousValue{0};

  [[nodiscard]] bool operator==(const DebugRamCandidate&) const = default;
};

class DebugRamSearch final {
public:
  static constexpr std::size_t maximumCandidates = 65'536U;

  [[nodiscard]] bool begin(
    std::span<const std::uint8_t> memory,
    DebugValueWidth width,
    DebugValueEndian endian);
  [[nodiscard]] bool filter(
    std::span<const std::uint8_t> memory,
    DebugRamComparison comparison,
    DebugValueFormat format,
    std::int64_t value = 0);
  void clear() noexcept;

  [[nodiscard]] bool active() const noexcept;
  [[nodiscard]] DebugValueWidth width() const noexcept;
  [[nodiscard]] DebugValueEndian endian() const noexcept;
  [[nodiscard]] const std::vector<DebugRamCandidate>& candidates() const noexcept;

private:
  DebugValueWidth width_{DebugValueWidth::byte};
  DebugValueEndian endian_{DebugValueEndian::big};
  std::vector<DebugRamCandidate> candidates_;
  bool active_{false};
};

struct DebugSymbol final {
  CoreDebugCpu cpu{CoreDebugCpu::m68k};
  std::uint32_t address{0};
  std::string name;

  [[nodiscard]] bool operator==(const DebugSymbol&) const = default;
};

class DebugSymbolTable final {
public:
  static constexpr std::size_t maximumFileBytes = 1024U * 1024U;
  static constexpr std::size_t maximumSymbols = 65'536U;
  static constexpr std::size_t maximumNameBytes = 128U;

  [[nodiscard]] bool load(
    std::string_view text,
    std::string& error);
  [[nodiscard]] bool loadFile(
    const std::filesystem::path& path,
    std::string& error);
  void clear() noexcept;

  [[nodiscard]] const DebugSymbol* find(
    CoreDebugCpu cpu,
    std::uint32_t address) const noexcept;
  [[nodiscard]] const std::vector<DebugSymbol>& symbols() const noexcept;

private:
  std::vector<DebugSymbol> symbols_;
};

[[nodiscard]] bool debugReadValue(
  std::span<const std::uint8_t> memory,
  std::uint32_t offset,
  DebugValueWidth width,
  DebugValueEndian endian,
  std::uint32_t& output) noexcept;

[[nodiscard]] std::int64_t debugInterpretValue(
  std::uint32_t value,
  DebugValueWidth width,
  DebugValueFormat format) noexcept;

[[nodiscard]] std::string debugTraceJson(
  std::span<const CoreDebugTraceEntry> entries,
  const DebugSymbolTable& symbols,
  std::uint64_t droppedEntries);

} // namespace genplusgx
