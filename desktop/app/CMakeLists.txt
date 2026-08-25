add_library(
  genplusgx_app_support STATIC
  include/genplusgx/app/command_line.h
  command_line.cpp
)
add_library(GenesisPlusGX::AppSupport ALIAS genplusgx_app_support)
target_include_directories(
  genplusgx_app_support
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(
  genplusgx_app_support
  PUBLIC Qt6::Core
  PRIVATE genplusgx_project_options genplusgx_project_warnings
)

qt_add_executable(genplusgx_desktop main.cpp)

target_link_libraries(
  genplusgx_desktop
  PRIVATE
    GenesisPlusGX::Audio
    GenesisPlusGX::AppSupport
    GenesisPlusGX::Input
    GenesisPlusGX::Persistence
    genplusgx::ui
    GenesisPlusGX::CoreAdapter
    genplusgx_project_options
    genplusgx_project_warnings
    Qt6::Widgets
)

set_target_properties(
  genplusgx_desktop
  PROPERTIES
    OUTPUT_NAME genesis-plus-gx-gui
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "${GENPLUSGX_APP_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${GENPLUSGX_APP_ID}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

install(
  TARGETS genplusgx_desktop
  BUNDLE DESTINATION .
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
