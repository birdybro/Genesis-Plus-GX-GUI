#include "desktop_core_host.h"

extern "C" {
#include "input.h"
#include "loadrom.h"
#include "system.h"
}

#include <iostream>

int main()
{
  genplusgx_host_reset_defaults();

  if (config.psg_preamp != 150 || config.fm_preamp != 100 || config.addr_error != 1) {
    std::cerr << "Desktop core defaults do not match the established core frontend\n";
    return 1;
  }

  if (config.input[0].padtype != (DEVICE_PAD2B | DEVICE_PAD3B | DEVICE_PAD6B)) {
    std::cerr << "Default controller capability mask is incorrect\n";
    return 2;
  }

  rominfo.domestic[0] = 0;
  if (rominfo.domestic[0] != 0) {
    std::cerr << "Core ROM metadata object is not linked\n";
    return 3;
  }

  audio_shutdown();
  return 0;
}
