#ifndef GENPLUSGX_DESKTOP_OSD_H
#define GENPLUSGX_DESKTOP_OSD_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_MSC_VER)
#include <strings.h>
#else
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

#include "config.h"

#define GENPLUSGX_HOST_PATH_CAPACITY 4096

extern char GG_ROM[GENPLUSGX_HOST_PATH_CAPACITY];
extern char AR_ROM[GENPLUSGX_HOST_PATH_CAPACITY];
extern char SK_ROM[GENPLUSGX_HOST_PATH_CAPACITY];
extern char SK_UPMEM[GENPLUSGX_HOST_PATH_CAPACITY];
extern char CD_BIOS_US[GENPLUSGX_HOST_PATH_CAPACITY];
extern char CD_BIOS_EU[GENPLUSGX_HOST_PATH_CAPACITY];
extern char CD_BIOS_JP[GENPLUSGX_HOST_PATH_CAPACITY];
extern char MD_BIOS[GENPLUSGX_HOST_PATH_CAPACITY];
extern char MS_BIOS_US[GENPLUSGX_HOST_PATH_CAPACITY];
extern char MS_BIOS_EU[GENPLUSGX_HOST_PATH_CAPACITY];
extern char MS_BIOS_JP[GENPLUSGX_HOST_PATH_CAPACITY];
extern char GG_BIOS[GENPLUSGX_HOST_PATH_CAPACITY];

void osd_input_update(void);
#define CHEATS_UPDATE() genplusgx_host_rom_cheats_update()
void genplusgx_host_rom_cheats_update(void);
int load_archive(char *filename, unsigned char *buffer, int maxsize, char *extension);
unsigned long crc32(unsigned long crc, const unsigned char *buffer, unsigned int length);
void error(char *format, ...);

#endif
