#include "genplusgx/core_adapter.h"

#include "synthetic_rom.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
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

} // namespace

int main()
{
  const genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisSramWriterRom(), ".md"};
  genplusgx::CoreAdapter adapter;
  if (!check(adapter.initialize(), "adapter initialization failed") ||
      !check(adapter.loadGame(fixture.path()), "SRAM fixture load failed")) {
    return 1;
  }

  genplusgx::BackupMemoryInfo cartridge;
  genplusgx::BackupMemoryInfo internalBram;
  if (!check(adapter.backupMemoryInfo(
        genplusgx::BackupMemoryKind::cartridgeSram, cartridge),
        "cartridge SRAM description failed") ||
      !check(cartridge.available && cartridge.size == 0x10000U,
        "cartridge SRAM availability or size was wrong") ||
      !check(adapter.backupMemoryInfo(
        genplusgx::BackupMemoryKind::scdInternalBram, internalBram),
        "Sega CD BRAM description failed") ||
      !check(!internalBram.available && internalBram.size == 0U,
        "Sega CD BRAM was exposed for cartridge hardware")) {
    return 2;
  }

  std::vector<std::uint8_t> memory(cartridge.size, 0U);
  genplusgx::BackupMemoryInfo copied;
  if (!check(adapter.copyBackupMemory(
        genplusgx::BackupMemoryKind::cartridgeSram, memory, copied),
        "initial SRAM copy failed") ||
      !check(std::ranges::all_of(memory, [](std::uint8_t value) {
        return value == 0xFFU;
      }), "new SRAM did not retain the core's erased value") ||
      !check(adapter.runFrame(), "SRAM writer frame failed") ||
      !check(adapter.copyBackupMemory(
        genplusgx::BackupMemoryKind::cartridgeSram, memory, copied),
        "written SRAM copy failed") ||
      !check(memory.front() == 0x5AU,
        "synthetic 68000 program did not write through the SRAM mapping")) {
    return 3;
  }

  std::vector<std::uint8_t> tooSmall(cartridge.size - 1U, 0U);
  if (!check(adapter.copyBackupMemory(
        genplusgx::BackupMemoryKind::cartridgeSram, tooSmall, copied).error ==
          genplusgx::CoreError::invalidBackupMemory,
        "undersized SRAM destination was accepted")) {
    return 4;
  }

  std::vector<std::uint8_t> replacement(cartridge.size, 0xA5U);
  if (!check(adapter.loadBackupMemory(
        genplusgx::BackupMemoryKind::cartridgeSram, replacement),
        "exact SRAM replacement failed") ||
      !check(adapter.reset(), "reset after SRAM replacement failed") ||
      !check(adapter.copyBackupMemory(
        genplusgx::BackupMemoryKind::cartridgeSram, memory, copied),
        "post-reset SRAM copy failed") ||
      !check(std::ranges::equal(memory, replacement),
        "hard reset destroyed loaded SRAM") ||
      !check(adapter.loadBackupMemory(
        genplusgx::BackupMemoryKind::cartridgeSram,
        std::span<const std::uint8_t>{replacement}.first(replacement.size() - 1U)).error ==
          genplusgx::CoreError::invalidBackupMemory,
        "wrong-sized SRAM image was accepted") ||
      !check(adapter.unloadGame(), "unload failed") ||
      !check(adapter.shutdown(), "shutdown failed")) {
    return 5;
  }
  return 0;
}
