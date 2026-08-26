add_library(
  genplusgx_app_support STATIC
  include/genplusgx/app/command_line.h
  include/genplusgx/app/shutdown_report.h
  command_line.cpp
  shutdown_report.cpp
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
    GenesisPlusGX::AppSupport
    GenesisPlusGX::Screenshots
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
      Qt6::QXcbEglIntegrationPlugin
      Qt6::QXcbGlxIntegrationPlugin
      Qt6::QGifPlugin
      Qt6::QICOPlugin
      Qt6::QJpegPlugin)
    if(TARGET "${optional_plugin}")
      list(APPEND genplusgx_linux_plugins "${optional_plugin}")
    endif()
  endforeach()

  set(genplusgx_has_xcb_gl_integration FALSE)
  foreach(xcb_gl_plugin IN ITEMS
      Qt6::QXcbEglIntegrationPlugin
      Qt6::QXcbGlxIntegrationPlugin)
    if(TARGET "${xcb_gl_plugin}")
      set(genplusgx_has_xcb_gl_integration TRUE)
    endif()
  endforeach()
  if(NOT genplusgx_has_xcb_gl_integration)
    message(FATAL_ERROR
      "The Linux package requires at least one Qt XCB OpenGL integration plugin")
  endif()

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

  get_filename_component(genplusgx_qt_library_directory
    "${Qt6_DIR}/../.." ABSOLUTE)
  get_filename_component(genplusgx_sdl_library_directory
    "${SDL3_DIR}/../.." ABSOLUTE)
  set(genplusgx_dependency_search_directories
    "${genplusgx_qt_library_directory};${genplusgx_sdl_library_directory}")

  configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/LinuxDeploy.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LinuxDeploy.cmake"
    @ONLY
  )
  install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/LinuxDeploy.cmake")
else()
  if(TARGET SDL3::SDL3-shared)
    if(APPLE)
      install(
        FILES "$<TARGET_FILE:SDL3::SDL3-shared>"
        DESTINATION "genesis-plus-gx-gui.app/Contents/Frameworks"
      )
    elseif(WIN32)
      install(
        FILES "$<TARGET_FILE:SDL3::SDL3-shared>"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
      )
    endif()
  endif()

  if(APPLE)
    set(genplusgx_macos_plugins
      Qt6::QCocoaIntegrationPlugin
      Qt6::QSQLiteDriverPlugin
    )
    foreach(optional_plugin IN ITEMS
        Qt6::QGifPlugin
        Qt6::QICOPlugin
        Qt6::QJpegPlugin)
      if(TARGET "${optional_plugin}")
        list(APPEND genplusgx_macos_plugins "${optional_plugin}")
      endif()
    endforeach()
    foreach(plugin IN LISTS genplusgx_macos_plugins)
      if(NOT TARGET "${plugin}")
        message(FATAL_ERROR "Required macOS Qt plugin target is missing: ${plugin}")
      endif()
      get_target_property(plugin_type "${plugin}" QT_PLUGIN_TYPE)
      if(NOT plugin_type)
        message(FATAL_ERROR "Qt plugin target has no plugin type: ${plugin}")
      endif()
      install(
        FILES "$<TARGET_FILE:${plugin}>"
        DESTINATION "genesis-plus-gx-gui.app/Contents/PlugIns/${plugin_type}"
      )
    endforeach()
    qt_generate_deploy_app_script(
      TARGET genplusgx_desktop
      OUTPUT_SCRIPT genplusgx_deploy_script
      DEPLOY_TOOL_OPTIONS -no-plugins
      NO_TRANSLATIONS
      NO_UNSUPPORTED_PLATFORM_ERROR
    )
  else()
    qt_generate_deploy_app_script(
      TARGET genplusgx_desktop
      OUTPUT_SCRIPT genplusgx_deploy_script
      NO_TRANSLATIONS
      NO_UNSUPPORTED_PLATFORM_ERROR
    )
  endif()
  install(SCRIPT "${genplusgx_deploy_script}")
endif()
