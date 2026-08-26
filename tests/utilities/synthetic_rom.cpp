#include "synthetic_rom.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>

namespace genplusgx::test {
namespace {

constexpr std::size_t romSize = 64U * 1024U;
constexpr std::size_t programAddress = 0x200U;
constexpr std::size_t segaCdBiosSize = 128U * 1024U;
constexpr std::size_t segaCdSectorSize = 2'048U;
constexpr std::size_t minimumSegaCdSectors = 150U;

void write16(std::vector<std::uint8_t>& rom, std::size_t offset, std::uint16_t value)
{
  rom.at(offset) = static_cast<std::uint8_t>(value >> 8U);
  rom.at(offset + 1U) = static_cast<std::uint8_t>(value & 0xFFU);
}

void write32(std::vector<std::uint8_t>& rom, std::size_t offset, std::uint32_t value)
{
  write16(rom, offset, static_cast<std::uint16_t>(value >> 16U));
  write16(rom, offset + 2U, static_cast<std::uint16_t>(value & 0xFFFFU));
}

template<std::size_t Size>
void writeText(
  std::vector<std::uint8_t>& rom,
  std::size_t offset,
  const std::array<char, Size>& text)
{
  const auto end = text.back() == '\0' ? text.end() - 1 : text.end();
  std::transform(text.begin(), end, rom.begin() + static_cast<std::ptrdiff_t>(offset),
    [](char character) { return static_cast<std::uint8_t>(character); });
}

std::string uniqueStem()
{
  const auto clockValue = std::chrono::steady_clock::now().time_since_epoch().count();
  std::random_device random;
  return "genplusgx-fixture-" + std::to_string(clockValue) + '-' +
         std::to_string(random());
}

void updateChecksum(std::vector<std::uint8_t>& rom)
{
  std::uint32_t checksum = 0U;
  for (std::size_t offset = 0x200U; offset < rom.size(); offset += 2U) {
    checksum += static_cast<std::uint32_t>(rom[offset] << 8U) | rom[offset + 1U];
  }
  write16(rom, 0x18EU, static_cast<std::uint16_t>(checksum & 0xFFFFU));
}

} // namespace

std::vector<std::uint8_t> makeGenesisRamMarkerRom()
{
  std::vector<std::uint8_t> rom(romSize, 0U);

  // Initial stack pointer, followed by reset and exception vectors. Every exception
  // returns to the small idempotent marker program so a host interrupt cannot escape
  // into uninitialized data.
  write32(rom, 0x000U, 0x00FFFF00U);
  for (std::size_t vector = 1; vector < 64U; ++vector) {
    write32(rom, vector * 4U, static_cast<std::uint32_t>(programAddress));
  }

  writeText(rom, 0x100U, std::to_array("SEGA GENESIS    "));
  writeText(rom, 0x110U, std::to_array("(C)OPEN 2026    "));

  std::fill_n(rom.begin() + 0x120, 48, static_cast<std::uint8_t>(' '));
  writeText(rom, 0x120U, std::to_array("GENPLUSGX CORE TEST"));
  std::fill_n(rom.begin() + 0x150, 48, static_cast<std::uint8_t>(' '));
  writeText(rom, 0x150U, std::to_array("GENPLUSGX SYNTHETIC RAM MARKER"));
  writeText(rom, 0x180U, std::to_array("GM"));
  writeText(rom, 0x182U, std::to_array("TEST-000001"));
  writeText(rom, 0x190U, std::to_array("J"));
  write32(rom, 0x1A0U, 0x00000000U);
  write32(rom, 0x1A4U, static_cast<std::uint32_t>(romSize - 1U));
  writeText(rom, 0x1F0U, std::to_array("U"));

  std::size_t cursor = programAddress;
  const auto emit16 = [&rom, &cursor](std::uint16_t word) {
    write16(rom, cursor, word);
    cursor += 2U;
  };

  // move.l #$00ff0000,a0
  emit16(0x207CU);
  emit16(0x00FFU);
  emit16(0x0000U);
  // move.l #$13579bdf,(a0)
  emit16(0x20BCU);
  emit16(0x1357U);
  emit16(0x9BDFU);
  // move.w #$cafe,4(a0)
  emit16(0x317CU);
  emit16(0xCAFEU);
  emit16(0x0004U);
  // move.b #$42,6(a0)
  emit16(0x117CU);
  emit16(0x0042U);
  emit16(0x0006U);

  // Configure the VDP for a visible 320x224 Mode 5 display with a light backdrop.
  // move.l #$00c00004,a1 (VDP control port)
  emit16(0x227CU);
  emit16(0x00C0U);
  emit16(0x0004U);
  // move.l #$00c00000,a2 (VDP data port)
  emit16(0x247CU);
  emit16(0x00C0U);
  emit16(0x0000U);
  // Register 0/1: Mode 5 with display enabled; register 12: 320-pixel mode.
  emit16(0x32BCU);
  emit16(0x8004U);
  emit16(0x32BCU);
  emit16(0x8174U);
  emit16(0x32BCU);
  emit16(0x8C81U);
  // Use CRAM entry zero for the backdrop, select it, and make it light gray.
  emit16(0x32BCU);
  emit16(0x8700U);
  emit16(0x22BCU);
  emit16(0xC000U);
  emit16(0x0000U);
  emit16(0x34BCU);
  emit16(0x0EEEU);

  // Program PSG channel zero with a deterministic centered square wave.
  // move.l #$00c00011,a3 (PSG byte-write port)
  emit16(0x267CU);
  emit16(0x00C0U);
  emit16(0x0011U);
  // Tone divider 0x0fe (latch low nibble, then upper six bits), full volume.
  emit16(0x16BCU);
  emit16(0x008EU);
  emit16(0x16BCU);
  emit16(0x000FU);
  emit16(0x16BCU);
  emit16(0x0090U);
  // Poll the controller A data port and expose its active-low result in work RAM.
  // move.b $00a10003.l,d0
  emit16(0x1039U);
  emit16(0x00A1U);
  emit16(0x0003U);
  // move.b d0,8(a0)
  emit16(0x1140U);
  emit16(0x0008U);
  // bra.s back twelve bytes to the controller read
  emit16(0x60F4U);

  updateChecksum(rom);

  return rom;
}

std::vector<std::uint8_t> makeGenesisSramWriterRom()
{
  auto rom = makeGenesisRamMarkerRom();
  std::fill(rom.begin() + static_cast<std::ptrdiff_t>(programAddress), rom.end(),
    std::uint8_t{0U});
  rom[0x1B0U] = static_cast<std::uint8_t>('R');
  rom[0x1B1U] = static_cast<std::uint8_t>('A');
  rom[0x1B2U] = 0xF8U;
  rom[0x1B3U] = 0x20U;
  write32(rom, 0x1B4U, 0x00200000U);
  write32(rom, 0x1B8U, 0x0020FFFFU);

  std::size_t cursor = programAddress;
  const auto emit16 = [&rom, &cursor](std::uint16_t word) {
    write16(rom, cursor, word);
    cursor += 2U;
  };
  // move.l #$00200000,a0; move.b #$5a,(a0); bra.s forever
  emit16(0x207CU);
  emit16(0x0020U);
  emit16(0x0000U);
  emit16(0x10BCU);
  emit16(0x005AU);
  emit16(0x60FEU);
  updateChecksum(rom);
  return rom;
}

std::vector<std::uint8_t> makeGenesisBootRom()
{
  std::vector<std::uint8_t> bios(2U * 1024U, 0U);
  write32(bios, 0x000U, 0x00FFFF00U);
  for (std::size_t vector = 1U; vector < 64U; ++vector) {
    write32(bios, vector * 4U, static_cast<std::uint32_t>(programAddress));
  }
  writeText(bios, 0x120U, std::to_array("GENESIS OS TEST "));
  write16(bios, programAddress, 0x60FEU);
  for (std::size_t offset = 0x300U; offset < bios.size(); ++offset) {
    bios[offset] = static_cast<std::uint8_t>((offset * 29U + 7U) & 0xFFU);
  }
  return bios;
}

std::vector<std::uint8_t> makeZ80RamMarkerRom(std::uint8_t marker)
{
  std::vector<std::uint8_t> rom(32U * 1024U, 0U);
  // DI; LD SP,$DFF0; LD A,marker; LD ($C000),A; JP $0009.
  // The program is valid on SG-1000, Mark III, Master System, and Game Gear.
  constexpr std::array prefix{
    std::uint8_t{0xF3U}, std::uint8_t{0x31U}, std::uint8_t{0xF0U},
    std::uint8_t{0xDFU}, std::uint8_t{0x3EU},
  };
  std::ranges::copy(prefix, rom.begin());
  rom[5U] = marker;
  rom[6U] = 0x32U;
  rom[7U] = 0x00U;
  rom[8U] = 0xC0U;
  rom[9U] = 0xC3U;
  rom[10U] = 0x09U;
  rom[11U] = 0x00U;
  return rom;
}

std::vector<std::uint8_t> makeZ80BootRom(std::size_t size)
{
  if (size == 0U || size > 4U * 1024U * 1024U ||
      (size % 1'024U) != 0U) {
    throw std::invalid_argument{"Z80 boot ROM size must be 1 KiB aligned"};
  }
  auto bios = makeZ80RamMarkerRom(0xB1U);
  bios.resize(size);
  for (std::size_t offset = 0x100U; offset < bios.size(); ++offset) {
    bios[offset] = static_cast<std::uint8_t>((offset * 17U + 3U) & 0xFFU);
  }
  return bios;
}

std::vector<std::uint8_t> makeSegaCdBios()
{
  std::vector<std::uint8_t> bios(segaCdBiosSize, 0U);

  // A test-only 68000 boot image. It contains no Sega firmware code: every
  // vector enters a two-byte self-loop, which is sufficient to exercise core
  // lifecycle, video/audio initialization, and backup-memory plumbing without
  // attempting to boot copyrighted software.
  write32(bios, 0x000U, 0x00FFFF00U);
  for (std::size_t vector = 1; vector < 64U; ++vector) {
    write32(bios, vector * 4U, static_cast<std::uint32_t>(programAddress));
  }
  writeText(bios, 0x100U, std::to_array("OPEN TEST BIOS  "));
  writeText(bios, 0x120U, std::to_array("GENPLUSGX TEST   "));
  write16(bios, programAddress, 0x60FEU); // bra.s $200

  // Make the image intentionally non-uniform and deterministic so the BIOS
  // validator can reject trivial repeated-byte files while accepting this one.
  for (std::size_t offset = 0x400U; offset < bios.size(); ++offset) {
    bios[offset] = static_cast<std::uint8_t>((offset * 37U + 11U) & 0xFFU);
  }
  return bios;
}

std::vector<std::uint8_t> makeSegaCdDiscImage(SyntheticSegaCdRegion region)
{
  std::vector<std::uint8_t> image(
    segaCdSectorSize * minimumSegaCdSectors, 0U);
  writeText(image, 0x000U, std::to_array("SEGADISCSYSTEM"));
  writeText(image, 0x100U, std::to_array("SEGA MEGA DRIVE "));
  writeText(image, 0x110U, std::to_array("(C)OPEN 2026    "));
  std::fill_n(image.begin() + 0x120, 48, static_cast<std::uint8_t>(' '));
  writeText(image, 0x120U, std::to_array("GENPLUSGX SYNTHETIC SEGA CD FIXTURE"));
  std::fill_n(image.begin() + 0x150, 48, static_cast<std::uint8_t>(' '));
  writeText(image, 0x150U, std::to_array("GENPLUSGX TEST DISC"));

  // Genesis Plus GX follows the Sega CD security-area region marker used by
  // known images: 0x64 Europe, 0xa1 Japan, and any other value USA.
  switch (region) {
    case SyntheticSegaCdRegion::usa:
      image[0x20BU] = 0x00U;
      break;
    case SyntheticSegaCdRegion::europe:
      image[0x20BU] = 0x64U;
      break;
    case SyntheticSegaCdRegion::japan:
      image[0x20BU] = 0xA1U;
      break;
  }
  return image;
}

TemporaryFixture::TemporaryFixture(
  std::vector<std::uint8_t> bytes,
  std::string_view extension)
  : path_(std::filesystem::temp_directory_path() /
          (uniqueStem() + std::string{extension}))
{
  std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
  if (!stream) {
    throw std::runtime_error{"Unable to create synthetic ROM fixture"};
  }

  stream.write(
    reinterpret_cast<const char*>(bytes.data()),
    static_cast<std::streamsize>(bytes.size()));
  if (!stream) {
    throw std::runtime_error{"Unable to write synthetic ROM fixture"};
  }
}

TemporaryFixture::~TemporaryFixture()
{
  std::error_code error;
  std::filesystem::remove(path_, error);
}

const std::filesystem::path& TemporaryFixture::path() const noexcept
{
  return path_;
}

} // namespace genplusgx::test
