#include "desktop_core_host.h"

#include <string.h>

#include "shared.h"

#define GENPLUSGX_HOST_MAX_CHEATS 150U

typedef struct {
  genplusgx_host_cheat patch;
  uint16 old_word;
  uint8 old_byte;
  uint8* previous_bank;
  uint8 rom_patched;
} host_cheat_entry;

static host_cheat_entry cheat_entries[GENPLUSGX_HOST_MAX_CHEATS];
static size_t cheat_count;

static int genesis_hardware(void) { return (system_hw & SYSTEM_PBC) == SYSTEM_MD; }

static int sega_cd_without_cartridge(void)
{
  return (system_hw == SYSTEM_MCD) && !scd.cartridge.boot;
}

static int ram_address(uint32 address)
{
  if (address >= 0xff0000U) {
    return 1;
  }
  return sega_cd_without_cartridge() &&
         ((address < 0x080000U) || ((address >= 0x200000U) && (address < 0x240000U)));
}

static void clear_rom_patches(void)
{
  size_t index = cheat_count;
  while (index > 0U) {
    host_cheat_entry* entry = &cheat_entries[--index];
    if (entry->rom_patched && genesis_hardware() &&
        (entry->patch.address & 0xfffffeU) + 1U < cart.romsize) {
      *(uint16*)(cart.rom + (entry->patch.address & 0xfffffeU)) = entry->old_word;
    } else if (entry->previous_bank != NULL) {
      *entry->previous_bank = entry->old_byte;
    }
    entry->previous_bank = NULL;
    entry->rom_patched = 0U;
  }
}

static void apply_rom_patches(void)
{
  size_t index;
  if (sega_cd_without_cartridge()) {
    return;
  }
  for (index = 0U; index < cheat_count; ++index) {
    host_cheat_entry* entry = &cheat_entries[index];
    const uint32 address = entry->patch.address;
    if (ram_address(address)) {
      continue;
    }
    if (genesis_hardware()) {
      const uint32 aligned = address & 0xfffffeU;
      if ((entry->patch.width == 2U) && (aligned + 1U < cart.romsize)) {
        entry->old_word = *(uint16*)(cart.rom + aligned);
        *(uint16*)(cart.rom + aligned) = entry->patch.data;
        entry->rom_patched = 1U;
      }
    } else if ((entry->patch.width == 1U) && (address <= 0xffffU)) {
      uint8* map = z80_readmap[address >> 10];
      if (map != NULL) {
        uint8* target = &map[address & 0x03ffU];
        if (!entry->patch.reference_required || (entry->patch.reference == *target)) {
          entry->old_byte = *target;
          *target = (uint8)entry->patch.data;
          entry->previous_bank = target;
          entry->rom_patched = 1U;
        }
      }
    }
  }
}

int genplusgx_host_set_cheats(const genplusgx_host_cheat* cheats, size_t count)
{
  size_t index;
  if ((count > GENPLUSGX_HOST_MAX_CHEATS) || ((count > 0U) && (cheats == NULL))) {
    return 0;
  }
  for (index = 0U; index < count; ++index) {
    if ((cheats[index].address > 0x00ffffffU) ||
        ((cheats[index].width != 1U) && (cheats[index].width != 2U)) ||
        (cheats[index].reference_required && (cheats[index].width != 1U))) {
      return 0;
    }
  }

  clear_rom_patches();
  memset(cheat_entries, 0, sizeof(cheat_entries));
  cheat_count = count;
  for (index = 0U; index < count; ++index) {
    cheat_entries[index].patch = cheats[index];
  }
  apply_rom_patches();
  return 1;
}

void genplusgx_host_clear_cheats(void)
{
  clear_rom_patches();
  memset(cheat_entries, 0, sizeof(cheat_entries));
  cheat_count = 0U;
}

void genplusgx_host_ram_cheats_update(void)
{
  size_t index;
  for (index = 0U; index < cheat_count; ++index) {
    const genplusgx_host_cheat* patch = &cheat_entries[index].patch;
    uint8* base;
    uint32 mask;
    if (!ram_address(patch->address)) {
      continue;
    }
    switch ((patch->address >> 20) & 0x0fU) {
      case 0x0U:
        base = scd.prg_ram;
        mask = 0x7fffeU;
        break;
      case 0x2U:
        base = scd.word_ram_2M;
        mask = 0x3fffeU;
        break;
      default:
        base = work_ram;
        mask = 0xfffeU;
        break;
    }
    if (patch->width == 2U) {
      *(uint16*)(base + (patch->address & mask)) = patch->data;
    } else {
      mask |= 1U;
      base[patch->address & mask] = (uint8)patch->data;
    }
  }
}

void genplusgx_host_rom_cheats_update(void)
{
  size_t index;
  if (genesis_hardware() || sega_cd_without_cartridge()) {
    return;
  }
  for (index = 0U; index < cheat_count; ++index) {
    host_cheat_entry* entry = &cheat_entries[index];
    const uint32 address = entry->patch.address;
    uint8* map;
    uint8* target;
    if ((entry->patch.width != 1U) || ram_address(address) || (address > 0xffffU)) {
      continue;
    }
    if (entry->previous_bank != NULL) {
      *entry->previous_bank = entry->old_byte;
      entry->previous_bank = NULL;
      entry->rom_patched = 0U;
    }
    map = z80_readmap[address >> 10];
    if (map == NULL) {
      continue;
    }
    target = &map[address & 0x03ffU];
    if (!entry->patch.reference_required || (entry->patch.reference == *target)) {
      entry->old_byte = *target;
      *target = (uint8)entry->patch.data;
      entry->previous_bank = target;
      entry->rom_patched = 1U;
    }
  }
}
