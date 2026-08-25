#include "desktop_core_host.h"
#include "osd.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "input.h"
#include "md_ntsc.h"
#include "sms_ntsc.h"
#include "ym2612.h"

t_config config;
md_ntsc_t *md_ntsc;
sms_ntsc_t *sms_ntsc;

char GG_ROM[GENPLUSGX_HOST_PATH_CAPACITY];
char AR_ROM[GENPLUSGX_HOST_PATH_CAPACITY];
char SK_ROM[GENPLUSGX_HOST_PATH_CAPACITY];
char SK_UPMEM[GENPLUSGX_HOST_PATH_CAPACITY];
char CD_BIOS_US[GENPLUSGX_HOST_PATH_CAPACITY];
char CD_BIOS_EU[GENPLUSGX_HOST_PATH_CAPACITY];
char CD_BIOS_JP[GENPLUSGX_HOST_PATH_CAPACITY];
char MD_BIOS[GENPLUSGX_HOST_PATH_CAPACITY];
char MS_BIOS_US[GENPLUSGX_HOST_PATH_CAPACITY];
char MS_BIOS_EU[GENPLUSGX_HOST_PATH_CAPACITY];
char MS_BIOS_JP[GENPLUSGX_HOST_PATH_CAPACITY];
char GG_BIOS[GENPLUSGX_HOST_PATH_CAPACITY];

void set_config_defaults(void)
{
  int index;

  memset(&config, 0, sizeof(config));
  config.psg_preamp = 150;
  config.fm_preamp = 100;
  config.cdda_volume = 100;
  config.pcm_volume = 100;
  config.hq_fm = 1;
  config.hq_psg = 1;
  config.filter = 1;
  config.low_freq = 200;
  config.high_freq = 8000;
  config.lg = 100;
  config.mg = 100;
  config.hg = 100;
  config.lp_range = 0x9999;
  config.ym2612 = YM2612_DISCRETE;
  config.ym2413 = 2;
  config.addr_error = 1;
  config.cd_latency = 1;
  config.enhanced_vscroll_limit = 8;
  config.gun_cursor[0] = 1;
  config.gun_cursor[1] = 1;

  for (index = 0; index < MAX_INPUTS; ++index)
  {
    config.input[index].padtype = DEVICE_PAD2B | DEVICE_PAD3B | DEVICE_PAD6B;
  }
}

void genplusgx_host_reset_defaults(void)
{
  set_config_defaults();
  memset(&input, 0, sizeof(input));
  input.system[0] = SYSTEM_GAMEPAD;
  input.system[1] = SYSTEM_GAMEPAD;
  old_system[0] = -1;
  old_system[1] = -1;
}

void osd_input_update(void)
{
}

int load_archive(char *filename, unsigned char *buffer, int maxsize, char *extension)
{
  FILE *file;
  long file_size;
  size_t bytes_read;

  if ((filename == NULL) || (buffer == NULL) || (maxsize <= 0))
  {
    return 0;
  }

  file = fopen(filename, "rb");
  if (file == NULL)
  {
    return 0;
  }

  if ((fseek(file, 0, SEEK_END) != 0) || ((file_size = ftell(file)) < 0) ||
      (file_size > maxsize) || (fseek(file, 0, SEEK_SET) != 0))
  {
    fclose(file);
    return 0;
  }

  bytes_read = fread(buffer, 1, (size_t)file_size, file);
  if ((bytes_read != (size_t)file_size) || (fclose(file) != 0))
  {
    return 0;
  }

  if (extension != NULL)
  {
    size_t filename_length = strlen(filename);
    memset(extension, 0, 4);
    if (filename_length >= 3)
    {
      memcpy(extension, filename + filename_length - 3, 3);
    }
  }

  return (int)bytes_read;
}

void error(char *format, ...)
{
  va_list arguments;

  if (format == NULL)
  {
    return;
  }

  va_start(arguments, format);
  vfprintf(stderr, format, arguments);
  va_end(arguments);
}
