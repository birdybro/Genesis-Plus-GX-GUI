qt_add_executable(genplusgx_desktop
  main.cpp
)

target_link_libraries(
  genplusgx_desktop
  PRIVATE
    GenesisPlusGX::Audio
    GenesisPlusGX::Input
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
