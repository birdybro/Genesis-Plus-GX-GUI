add_library(
  genplusgx_persistence STATIC
  src/persistence.cpp
  src/recent_games.cpp
  src/state_manager.cpp
)
add_library(GenesisPlusGX::Persistence ALIAS genplusgx_persistence)
target_include_directories(
  genplusgx_persistence PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
  genplusgx_persistence
  PUBLIC GenesisPlusGX::FrontendTypes Qt6::Core
  PRIVATE genplusgx_project_warnings
)
set_target_properties(
  genplusgx_persistence PROPERTIES OUTPUT_NAME genplusgx_persistence
)
