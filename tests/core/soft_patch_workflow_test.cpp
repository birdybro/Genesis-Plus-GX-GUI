#include "genplusgx/core_adapter.h"
#include "genplusgx/game_patch.h"
#include "genplusgx/persistence.h"

#include "synthetic_rom.h"

#include <QTemporaryDir>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

bool check(bool condition, std::string_view message)
{
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

bool writeBytes(
  const std::filesystem::path& path,
  std::span<const std::uint8_t> bytes)
{
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()),
    static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(output);
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path)
{
  std::ifstream input(path, std::ios::binary);
  return {
    std::istreambuf_iterator<char>{input},
    std::istreambuf_iterator<char>{},
  };
}

} // namespace

int main()
{
  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "temporary workflow root was unavailable")) {
    return 1;
  }
  const auto root = std::filesystem::path{temporary.path().toStdString()};
  const auto sourcePath = root / "marker.md";
  const auto patchPath = root / "marker.ips";
  const auto source = genplusgx::test::makeGenesisRamMarkerRom();
  const std::vector<std::uint8_t> patch{
    'P', 'A', 'T', 'C', 'H',
    0x00, 0x02, 0x0a, 0x00, 0x02, 0x24, 0x68,
    'E', 'O', 'F',
  };
  if (!check(writeBytes(sourcePath, source) && writeBytes(patchPath, patch),
        "soft-patch workflow fixtures could not be written")) {
    return 2;
  }
  const auto patched = genplusgx::applyGamePatchFile(
    sourcePath, patchPath, root / "cache");
  if (!check(patched.status && patched.path != sourcePath,
        "IPS output was not prepared in a separate cache file") ||
      !check(readBytes(sourcePath) == source,
        "soft patching modified the source cartridge image") ||
      !check(readBytes(patched.path).at(0x20aU) == 0x24U &&
          readBytes(patched.path).at(0x20bU) == 0x68U,
        "patched instruction bytes were not written to runtime content")) {
    return 3;
  }
  const auto sourceIdentity = genplusgx::identifyGame(sourcePath);
  const auto patchedIdentity = genplusgx::identifyGame(patched.path);
  if (!check(sourceIdentity.status && patchedIdentity.status &&
        sourceIdentity.identity.sha256 != patchedIdentity.identity.sha256,
      "patched content did not receive an independent persistence identity")) {
    return 4;
  }

  genplusgx::CoreAdapter adapter;
  genplusgx::CoreDebugResponse response;
  if (!check(adapter.initialize(), "core initialization failed") ||
      !check(adapter.loadGame(patched.path) && adapter.runFrame(true) &&
          adapter.debugRequest({}, response) && response.snapshot,
        "patched cartridge did not execute through the real core") ||
      !check(response.snapshot->m68kRam[0] == 0x13U &&
          response.snapshot->m68kRam[1] == 0x57U &&
          response.snapshot->m68kRam[2] == 0x24U &&
          response.snapshot->m68kRam[3] == 0x68U,
        "the core did not execute the patched immediate value") ||
      !check(adapter.unloadGame(), "patched cartridge did not unload cleanly") ||
      !check(adapter.loadGame(sourcePath) && adapter.runFrame(true) &&
          adapter.debugRequest({}, response) && response.snapshot,
        "unpatched source did not reload after the patched session") ||
      !check(response.snapshot->m68kRam[2] == 0x9bU &&
          response.snapshot->m68kRam[3] == 0xdfU,
        "the original cartridge behavior changed after soft patching") ||
      !check(adapter.unloadGame() && adapter.shutdown(),
        "soft-patch core workflow did not shut down cleanly")) {
    return 5;
  }
  return 0;
}
