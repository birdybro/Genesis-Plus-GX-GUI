add_library(genplusgx_achievements STATIC
  include/genplusgx/achievements/achievement_bridge.h
  include/genplusgx/achievements/achievement_credentials.h
  include/genplusgx/achievements/achievement_network_client.h
  include/genplusgx/achievements/achievement_runtime.h
  include/genplusgx/achievements/achievement_settings.h
  include/genplusgx/achievements/achievement_types.h
  src/achievement_bridge.cpp
  src/achievement_credentials.cpp
  src/achievement_network_client.cpp
  src/achievement_runtime.cpp
  src/achievement_settings.cpp
)
add_library(GenesisPlusGX::Achievements ALIAS genplusgx_achievements)
target_include_directories(genplusgx_achievements
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(genplusgx_achievements
  PUBLIC
    GenesisPlusGX::FrontendTypes
    GenesisPlusGX::Persistence
    Qt6::Core
  PRIVATE
    Qt6::Network
    genplusgx_project_options
    genplusgx_project_warnings)
if(GENPLUSGX_ENABLE_ACHIEVEMENTS)
  target_compile_definitions(genplusgx_achievements
    PUBLIC GENPLUSGX_HAVE_ACHIEVEMENTS=1)
  target_link_libraries(genplusgx_achievements
    PRIVATE GenesisPlusGX::QtKeychain GenesisPlusGX::Rcheevos)
endif()
set_target_properties(genplusgx_achievements PROPERTIES AUTOMOC ON)
