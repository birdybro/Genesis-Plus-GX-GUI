add_library(genplusgx_cheats STATIC
  include/genplusgx/cheats/cheat_manager.h
  src/cheat_manager.cpp
)
add_library(GenesisPlusGX::Cheats ALIAS genplusgx_cheats)
target_include_directories(
  genplusgx_cheats
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
  genplusgx_cheats
  PUBLIC
    GenesisPlusGX::FrontendTypes
    GenesisPlusGX::Persistence
    Qt6::Core
  PRIVATE genplusgx_project_warnings
)
set_target_properties(genplusgx_cheats PROPERTIES OUTPUT_NAME genplusgx_cheats)
