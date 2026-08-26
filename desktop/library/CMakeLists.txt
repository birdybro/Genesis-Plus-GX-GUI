add_library(
  genplusgx_library STATIC
  include/genplusgx/library/game_metadata.h
  include/genplusgx/library/game_metadata_service.h
  include/genplusgx/library/game_library_database.h
  include/genplusgx/library/game_library_scanner.h
  src/game_metadata.cpp
  src/game_metadata_service.cpp
  src/game_library_database.cpp
  src/game_library_scanner.cpp
)
add_library(GenesisPlusGX::Library ALIAS genplusgx_library)

target_include_directories(
  genplusgx_library
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(
  genplusgx_library
  PRIVATE
    Qt6::Core
    Qt6::Sql
    GenesisPlusGX::FrontendTypes
    GenesisPlusGX::GameFiles
    GenesisPlusGX::Persistence
    genplusgx_project_warnings
)
set_target_properties(genplusgx_library PROPERTIES OUTPUT_NAME genplusgx_library)
