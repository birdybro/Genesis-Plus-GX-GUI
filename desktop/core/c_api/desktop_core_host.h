#ifndef GENPLUSGX_DESKTOP_CORE_HOST_H
#define GENPLUSGX_DESKTOP_CORE_HOST_H

#include "config.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void genplusgx_host_reset_defaults(void);

typedef struct
{
  uint32_t address;
  uint16_t data;
  uint8_t reference;
  uint8_t width;
  uint8_t reference_required;
} genplusgx_host_cheat;

int genplusgx_host_set_cheats(
  const genplusgx_host_cheat *cheats,
  size_t count);
void genplusgx_host_clear_cheats(void);
void genplusgx_host_ram_cheats_update(void);
void genplusgx_host_rom_cheats_update(void);

#ifdef __cplusplus
}
#endif

#endif
