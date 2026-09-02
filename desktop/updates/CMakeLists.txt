if(NOT GENPLUSGX_ENABLE_SIGNED_UPDATES)
  add_library(genplusgx_updates STATIC
    include/genplusgx/updates/update_settings.h
    include/genplusgx/updates/update_types.h
    src/update_settings.cpp
    src/update_types.cpp)
  add_library(GenesisPlusGX::Updates ALIAS genplusgx_updates)
  target_include_directories(genplusgx_updates PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
  target_compile_definitions(genplusgx_updates PUBLIC
    GENPLUSGX_SIGNED_UPDATES_DISABLED=1)
  target_link_libraries(genplusgx_updates
    PUBLIC GenesisPlusGX::Persistence
    PRIVATE Qt6::Core genplusgx_project_options genplusgx_project_warnings)
  return()
endif()

add_library(genplusgx_updates STATIC
  include/genplusgx/updates/update_client.h
  include/genplusgx/updates/update_manifest.h
  include/genplusgx/updates/update_service.h
  include/genplusgx/updates/update_settings.h
  include/genplusgx/updates/update_types.h
  src/update_client.cpp
  src/update_manifest.cpp
  src/update_service.cpp
  src/update_settings.cpp
  src/update_types.cpp
)
add_library(GenesisPlusGX::Updates ALIAS genplusgx_updates)
target_include_directories(genplusgx_updates PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(genplusgx_updates
  PUBLIC GenesisPlusGX::Persistence
  PRIVATE
    GenesisPlusGX::Monocypher
    Qt6::Core
    Qt6::Network
    genplusgx_project_options
    genplusgx_project_warnings)
target_compile_definitions(genplusgx_updates PUBLIC GENPLUSGX_HAVE_SIGNED_UPDATES=1)
