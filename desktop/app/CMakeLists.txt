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
    GenesisPlusGX::Cheats
    GenesisPlusGX::Diagnostics
    GenesisPlusGX::Input
    GenesisPlusGX::Library
    GenesisPlusGX::Persistence
    GenesisPlusGX::Platform
    GenesisPlusGX::Screenshots
    GenesisPlusGX::Settings
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

if(UNIX AND NOT APPLE)
  set_target_properties(
    genplusgx_desktop
    PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
  )
  target_link_options(genplusgx_desktop PRIVATE "LINKER:--disable-new-dtags")
endif()

install(
  TARGETS genplusgx_desktop
  BUNDLE DESTINATION .
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

if(UNIX AND NOT APPLE)
  set(genplusgx_linux_plugins
    Qt6::QXcbIntegrationPlugin
    Qt6::QOffscreenIntegrationPlugin
    Qt6::QSQLiteDriverPlugin
  )
  foreach(optional_plugin IN ITEMS
      Qt6::QGifPlugin
      Qt6::QICOPlugin
      Qt6::QJpegPlugin)
    if(TARGET "${optional_plugin}")
      list(APPEND genplusgx_linux_plugins "${optional_plugin}")
    endif()
  endforeach()

  foreach(plugin IN LISTS genplusgx_linux_plugins)
    if(NOT TARGET "${plugin}")
      message(FATAL_ERROR "Required Linux Qt plugin target is missing: ${plugin}")
    endif()
    get_target_property(plugin_type "${plugin}" QT_PLUGIN_TYPE)
    if(NOT plugin_type)
      message(FATAL_ERROR "Qt plugin target has no plugin type: ${plugin}")
    endif()
    install(
      FILES "$<TARGET_FILE:${plugin}>"
      DESTINATION "${CMAKE_INSTALL_LIBDIR}/qt6/plugins/${plugin_type}"
    )
  endforeach()

  configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/LinuxDeploy.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LinuxDeploy.cmake"
    @ONLY
  )
  install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/LinuxDeploy.cmake")
else()
  qt_generate_deploy_app_script(
    TARGET genplusgx_desktop
    OUTPUT_SCRIPT genplusgx_deploy_script
    NO_TRANSLATIONS
    NO_UNSUPPORTED_PLATFORM_ERROR
  )
  install(SCRIPT "${genplusgx_deploy_script}")
endif()
