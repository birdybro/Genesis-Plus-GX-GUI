add_library(genplusgx_test_utilities STATIC
  physical_media_fixture.cpp
  physical_media_fixture.h
  synthetic_rom.cpp
)
target_include_directories(genplusgx_test_utilities PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  genplusgx_test_utilities
  PUBLIC GenesisPlusGX::Platform genplusgx_project_options
  PRIVATE genplusgx_archive_zlib
  PRIVATE genplusgx_project_warnings
)
