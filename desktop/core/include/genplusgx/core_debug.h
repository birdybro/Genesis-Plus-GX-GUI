#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace genplusgx {

enum class CoreDebugMemoryRegion : std::uint8_t {
  rom,
  m68kRam,
  z80Ram,
  vram,
  cram,
  vsram,
  vdpRegisters,
};

struct CoreDebugMemoryRegionInfo final {
  CoreDebugMemoryRegion region{CoreDebugMemoryRegion::rom};
  std::string_view name;
  std::uint32_t displayBase{0};
  std::uint32_t size{0};
  bool writable{false};
};

struct CoreDebugM68kRegisters final {
  std::array<std::uint32_t, 8> data{};
  std::array<std::uint32_t, 8> address{};
  std::uint32_t programCounter{0};
  std::uint32_t status{0};
  std::uint32_t userStackPointer{0};
  std::uint32_t interruptStackPointer{0};

  [[nodiscard]] bool operator==(const CoreDebugM68kRegisters&) const = default;
};

struct CoreDebugZ80Registers final {
  std::uint16_t af{0};
  std::uint16_t bc{0};
  std::uint16_t de{0};
  std::uint16_t hl{0};
  std::uint16_t afAlternate{0};
  std::uint16_t bcAlternate{0};
  std::uint16_t deAlternate{0};
  std::uint16_t hlAlternate{0};
  std::uint16_t ix{0};
  std::uint16_t iy{0};
  std::uint16_t stackPointer{0};
  std::uint16_t programCounter{0};
  std::uint8_t interruptVector{0};
  std::uint8_t refresh{0};
  std::uint8_t interruptMode{0};
  bool interruptFlipFlop1{false};
  bool interruptFlipFlop2{false};
  bool halted{false};
  std::uint32_t bank{0};

  [[nodiscard]] bool operator==(const CoreDebugZ80Registers&) const = default;
};

struct CoreDebugVdpState final {
  std::array<std::uint8_t, 0x20> registers{};
  std::uint16_t status{0};
  std::uint32_t dmaLength{0};
  std::uint32_t dmaSource{0};
  std::uint8_t dmaType{0};
  std::uint16_t horizontalCounter{0};
  std::uint16_t verticalCounter{0};
  bool pal{false};
  bool interlaced{false};
  bool oddField{false};
  std::array<std::uint8_t, 0x10000> vram{};
  std::array<std::uint16_t, 0x40> cram{};
  std::array<std::uint16_t, 0x40> vsram{};
  std::array<std::uint8_t, 0x400> spriteTable{};
};

struct CoreDebugSoundState final {
  std::array<std::array<std::uint8_t, 0x100>, 2> fmRegisters{};
  std::array<std::int32_t, 8> psgRegisters{};
};

struct CoreDebugInputState final {
  std::array<std::uint16_t, 8> buttons{};
  std::array<std::array<std::int16_t, 2>, 8> analog{};
};

struct CoreDebugSnapshot final {
  std::uint64_t frameNumber{0};
  std::uint32_t hardware{0};
  std::uint32_t romSize{0};
  bool m68kActive{false};
  CoreDebugM68kRegisters m68k;
  CoreDebugZ80Registers z80;
  CoreDebugVdpState vdp;
  CoreDebugSoundState sound;
  CoreDebugInputState input;
  std::array<std::uint8_t, 0x10000> m68kRam{};
  std::array<std::uint8_t, 0x2000> z80Ram{};
};

enum class CoreDebugCpu : std::uint8_t {
  m68k,
  z80,
};

struct CoreDebugBreakpoint final {
  CoreDebugCpu cpu{CoreDebugCpu::m68k};
  std::uint32_t address{0};

  [[nodiscard]] bool operator==(const CoreDebugBreakpoint&) const = default;
};

struct CoreDebugProgramCounters final {
  bool m68kActive{false};
  std::uint32_t m68k{0};
  std::uint16_t z80{0};
};

inline constexpr std::size_t maximumCoreDebugBreakpoints = 64U;

enum class CoreDebugRequestType : std::uint8_t {
  captureSnapshot,
  readMemory,
  writeMemory,
  setM68kRegisters,
  setZ80Registers,
  setVdpRegister,
  setFrameBreakpoints,
};

struct CoreDebugRequest final {
  CoreDebugRequestType type{CoreDebugRequestType::captureSnapshot};
  CoreDebugMemoryRegion region{CoreDebugMemoryRegion::m68kRam};
  std::uint32_t offset{0};
  std::uint32_t size{256};
  std::vector<std::uint8_t> bytes;
  CoreDebugM68kRegisters m68k;
  CoreDebugZ80Registers z80;
  std::uint8_t vdpRegister{0};
  std::uint8_t vdpValue{0};
  std::vector<CoreDebugBreakpoint> breakpoints;
};

struct CoreDebugResponse final {
  CoreDebugRequestType type{CoreDebugRequestType::captureSnapshot};
  CoreDebugMemoryRegion region{CoreDebugMemoryRegion::m68kRam};
  std::uint32_t offset{0};
  std::shared_ptr<const CoreDebugSnapshot> snapshot;
  std::vector<std::uint8_t> bytes;
  std::optional<CoreDebugBreakpoint> breakpointHit;
};

[[nodiscard]] std::array<CoreDebugMemoryRegionInfo, 7>
coreDebugMemoryRegions(std::uint32_t romSize) noexcept;

[[nodiscard]] std::uint32_t coreDebugCramColor(
  std::uint16_t packedColor) noexcept;

} // namespace genplusgx
