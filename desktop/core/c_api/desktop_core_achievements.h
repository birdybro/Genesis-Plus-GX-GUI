#ifndef GENPLUSGX_DESKTOP_CORE_ACHIEVEMENTS_H
#define GENPLUSGX_DESKTOP_CORE_ACHIEVEMENTS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RetroAchievements console identifiers from rcheevos rc_consoles.h. */
enum
{
  GENPLUSGX_ACHIEVEMENTS_CONSOLE_UNKNOWN = 0,
  GENPLUSGX_ACHIEVEMENTS_CONSOLE_MEGA_DRIVE = 1,
  GENPLUSGX_ACHIEVEMENTS_CONSOLE_SEGA_CD = 9,
  GENPLUSGX_ACHIEVEMENTS_CONSOLE_MASTER_SYSTEM = 11,
  GENPLUSGX_ACHIEVEMENTS_CONSOLE_GAME_GEAR = 15,
  GENPLUSGX_ACHIEVEMENTS_CONSOLE_SG1000 = 33
};

uint32_t genplusgx_achievements_console_id(void);
size_t genplusgx_achievements_read_memory(
  uint32_t address,
  uint8_t *output,
  size_t size);

#ifdef __cplusplus
}
#endif

#endif
