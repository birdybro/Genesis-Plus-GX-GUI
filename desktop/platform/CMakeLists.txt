add_library(genplusgx_platform STATIC
  include/genplusgx/platform/bios_manager.h
  src/bios_manager.cpp
)
add_library(GenesisPlusGX::Platform ALIAS genplusgx_platform)
target_include_directories(
  genplusgx_platform
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(
  genplusgx_platform
  PUBLIC GenesisPlusGX::Persistence Qt6::Core
  PRIVATE genplusgx_project_options genplusgx_project_warnings
)
