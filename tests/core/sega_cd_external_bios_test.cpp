#include "genplusgx/core_adapter.h"

#include "synthetic_rom.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

int main()
{
  const char* configured = std::getenv("GENPLUSGX_TEST_SEGA_CD_US_BIOS");
  if (configured == nullptr || *configured == '\0') {
    std::cerr << "GENPLUSGX_TEST_SEGA_CD_US_BIOS must name a legally obtained "
                 "128 KiB USA Sega CD BIOS when external fixture tests are enabled.\n";
    return 2;
  }
  const std::filesystem::path biosPath{configured};
  std::error_code error;
  if (!std::filesystem::is_regular_file(biosPath, error) || error ||
      std::filesystem::file_size(biosPath, error) != 128U * 1024U || error) {
    std::cerr << "The external USA Sega CD BIOS is missing or not exactly 128 KiB.\n";
    return 3;
  }

  const genplusgx::test::TemporaryFixture disc{
    genplusgx::test::makeSegaCdDiscImage(), ".iso"};
  genplusgx::CoreAdapter adapter;
  genplusgx::CoreFirmwareSettings firmware;
  firmware.segaCdUsa = biosPath;
  if (!adapter.initialize() || !adapter.applyFirmwareSettings(firmware)) {
    std::cerr << "The core or external firmware configuration could not initialize.\n";
    return 4;
  }
  const auto loaded = adapter.loadGame(disc.path());
  if (!loaded) {
    std::cerr << loaded.message << '\n';
    return 5;
  }
  const auto frame = adapter.runFrame();
  const auto shutdown = adapter.shutdown();
  if (!frame || !shutdown) {
    std::cerr << (frame ? shutdown.message : frame.message) << '\n';
    return 6;
  }
  return 0;
}
