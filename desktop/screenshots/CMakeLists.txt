add_library(genplusgx_screenshots STATIC
  include/genplusgx/screenshots/screenshot_service.h
  src/screenshot_service.cpp
)
add_library(GenesisPlusGX::Screenshots ALIAS genplusgx_screenshots)
target_include_directories(
  genplusgx_screenshots
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(
  genplusgx_screenshots
  PUBLIC
    GenesisPlusGX::FrontendTypes
    GenesisPlusGX::Persistence
    GenesisPlusGX::VideoExchange
    Qt6::Gui
  PRIVATE
    genplusgx_project_options
    genplusgx_project_warnings
)
