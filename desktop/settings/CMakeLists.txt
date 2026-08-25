add_library(genplusgx_settings STATIC
  include/genplusgx/settings/audio_settings.h
  include/genplusgx/settings/system_settings.h
  include/genplusgx/settings/video_settings.h
  src/audio_settings.cpp
  src/system_settings.cpp
  src/video_settings.cpp
)
add_library(GenesisPlusGX::Settings ALIAS genplusgx_settings)
target_include_directories(
  genplusgx_settings
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(
  genplusgx_settings
  PUBLIC
    GenesisPlusGX::FrontendTypes
    GenesisPlusGX::Persistence
    GenesisPlusGX::VideoExchange
    Qt6::Core
  PRIVATE
    genplusgx_project_options
    genplusgx_project_warnings
)
