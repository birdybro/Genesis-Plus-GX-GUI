#include "desktop_core_host.h"
#include "synthetic_rom.h"

extern "C" {
#include "shared.h"
}

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::uint16_t hostWordAt(std::size_t offset)
{
  std::uint16_t value = 0;
  std::memcpy(&value, work_ram + offset, sizeof(value));
  return value;
}

std::string_view trimRightSpaces(std::string_view text)
{
  const auto lastCharacter = text.find_last_not_of(' ');
  return lastCharacter == std::string_view::npos
           ? std::string_view{}
           : text.substr(0U, lastCharacter + 1U);
}

} // namespace

int main()
{
  const auto romBytes = genplusgx::test::makeGenesisRamMarkerRom();
  if (romBytes.size() != 64U * 1024U) {
    std::cerr << "Synthetic ROM generator returned an unexpected size\n";
    return 1;
  }

  const genplusgx::test::TemporaryFixture fixture{romBytes, ".bin"};
  std::string path = fixture.path().string();
  std::vector<char> mutablePath(path.begin(), path.end());
  mutablePath.push_back('\0');

  genplusgx_host_reset_defaults();
  std::vector<std::uint8_t> framebuffer(720U * 576U * 2U, 0U);
  std::memset(&bitmap, 0, sizeof(bitmap));
  bitmap.width = 720;
  bitmap.height = 576;
  bitmap.pitch = bitmap.width * 2;
  bitmap.data = framebuffer.data();
  bitmap.viewport.changed = 3;

  if (load_rom(mutablePath.data()) == 0) {
    std::cerr << "Core rejected the generated Genesis ROM\n";
    return 2;
  }
  if (system_hw != SYSTEM_MD) {
    std::cerr << "Generated .bin ROM was not detected as Genesis hardware\n";
    return 3;
  }
  if (trimRightSpaces(rominfo.domestic) != "GENPLUSGX CORE TEST") {
    std::cerr << "Generated ROM metadata was not parsed correctly\n";
    return 4;
  }

  if (audio_init(48'000, 0.0) != 0) {
    std::cerr << "Core audio initialization failed\n";
    return 5;
  }
  system_init();
  system_reset();
  system_frame_gen(1);

  const bool longMarker = hostWordAt(0U) == 0x1357U && hostWordAt(2U) == 0x9BDFU;
  const bool wordMarker = hostWordAt(4U) == 0xCAFEU;
  const bool byteMarker = work_ram[7U] == 0x42U;
  const auto programCounter = m68k_get_reg(M68K_REG_PC);

  audio_shutdown();

  if (!longMarker || !wordMarker || !byteMarker) {
    std::cerr << "68000 program did not write the expected semantic RAM markers\n";
    return 6;
  }
  if (programCounter < 0x200U || programCounter >= 0x300U) {
    std::cerr << "68000 program counter escaped the generated test program\n";
    return 7;
  }

  return 0;
}
