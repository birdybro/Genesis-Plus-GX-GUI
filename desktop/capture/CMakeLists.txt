add_library(genplusgx_capture STATIC
  include/genplusgx/capture/recording_service.h
  src/recording_service.cpp
)
add_library(GenesisPlusGX::Capture ALIAS genplusgx_capture)
target_include_directories(
  genplusgx_capture
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(
  genplusgx_capture
  PUBLIC
    GenesisPlusGX::FrontendTypes
  PRIVATE
    GenesisPlusGX::Persistence
    genplusgx_project_options
    genplusgx_project_warnings
    Qt6::Core
    Qt6::Gui
)
