add_library(
  genplusgx_app_support STATIC
  include/genplusgx/app/command_line.h
  include/genplusgx/app/platform_bootstrap.h
  include/genplusgx/app/shutdown_report.h
  command_line.cpp
  platform_bootstrap.cpp
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
    GenesisPlusGX::Netplay
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

set(genplusgx_translation_sources
  "${PROJECT_SOURCE_DIR}/translations/genplusgx_en_XA.ts"
)
set_source_files_properties(
  ${genplusgx_translation_sources}
  PROPERTIES OUTPUT_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/translations"
)
qt_add_translations(
  TARGETS genplusgx_desktop
  SOURCE_TARGETS
    genplusgx_app_support genplusgx_localization genplusgx_ui
    genplusgx_video_widget genplusgx_desktop
  TS_FILES ${genplusgx_translation_sources}
  LUPDATE_TARGET genplusgx_update_translations
  LRELEASE_TARGET genplusgx_release_translations
  QM_FILES_OUTPUT_VARIABLE genplusgx_translation_catalogs
  LUPDATE_OPTIONS -no-obsolete -locations relative
  LRELEASE_OPTIONS -nounfinished
  IMMEDIATE_CALL
)
set(GENPLUSGX_TRANSLATION_CATALOGS
  "${genplusgx_translation_catalogs}" PARENT_SCOPE)
add_dependencies(genplusgx_desktop genplusgx_release_translations)
if(APPLE)
  set(genplusgx_build_translation_destination
    "$<TARGET_BUNDLE_CONTENT_DIR:genplusgx_desktop>/Resources/translations")
else()
  set(genplusgx_build_translation_destination
    "$<TARGET_FILE_DIR:genplusgx_desktop>/translations")
endif()
add_custom_command(
  TARGET genplusgx_desktop POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${genplusgx_build_translation_destination}"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    ${genplusgx_translation_catalogs}
    "${genplusgx_build_translation_destination}"
  VERBATIM
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

if(APPLE)
  set(genplusgx_translation_destination
    "genesis-plus-gx-gui.app/Contents/Resources/translations")
else()
  set(genplusgx_translation_destination
    "${CMAKE_INSTALL_DATADIR}/Genesis-Plus-GX-GUI/translations")
endif()
install(
  FILES ${genplusgx_translation_catalogs}
  DESTINATION "${genplusgx_translation_destination}"
)

if(GENPLUSGX_ENABLE_LIBRETRO_SHADERS)
  if(APPLE)
    set(genplusgx_librashader_destination
      "genesis-plus-gx-gui.app/Contents/Frameworks")
    set(genplusgx_shader_destination
      "genesis-plus-gx-gui.app/Contents/Resources/shaders")
    set(genplusgx_librashader_license_destination
      "genesis-plus-gx-gui.app/Contents/Resources/licenses")
  elseif(WIN32)
    set(genplusgx_librashader_destination "${CMAKE_INSTALL_BINDIR}")
    set(genplusgx_shader_destination
      "${CMAKE_INSTALL_DATADIR}/Genesis-Plus-GX-GUI/shaders")
    set(genplusgx_librashader_license_destination
      "${CMAKE_INSTALL_DATADIR}/doc/Genesis-Plus-GX-GUI")
  else()
    set(genplusgx_librashader_destination "${CMAKE_INSTALL_LIBDIR}")
    set(genplusgx_shader_destination
      "${CMAKE_INSTALL_DATADIR}/Genesis-Plus-GX-GUI/shaders")
    set(genplusgx_librashader_license_destination
      "${CMAKE_INSTALL_DATADIR}/doc/Genesis-Plus-GX-GUI")
  endif()
  install(
    FILES "${GENPLUSGX_LIBRASHADER_BUILT_LIBRARY}"
    DESTINATION "${genplusgx_librashader_destination}"
    RENAME "${GENPLUSGX_LIBRASHADER_INSTALL_NAME}"
    PERMISSIONS
      OWNER_READ OWNER_WRITE OWNER_EXECUTE
      GROUP_READ GROUP_EXECUTE
      WORLD_READ WORLD_EXECUTE
  )
  install(
    FILES
      "${PROJECT_SOURCE_DIR}/desktop/resources/shaders/genplusgx-crt.slangp"
      "${PROJECT_SOURCE_DIR}/desktop/resources/shaders/genplusgx-crt.slang"
    DESTINATION "${genplusgx_shader_destination}"
  )
  install(
    FILES "${librashader_source_SOURCE_DIR}/LICENSE.md"
    DESTINATION "${genplusgx_librashader_license_destination}"
    RENAME "librashader-MPL-2.0.md"
  )
endif()

if(GENPLUSGX_ENABLE_ACHIEVEMENTS)
  if(APPLE)
    set(genplusgx_achievement_license_destination
      "genesis-plus-gx-gui.app/Contents/Resources/licenses")
  else()
    set(genplusgx_achievement_license_destination
      "${CMAKE_INSTALL_DATADIR}/doc/Genesis-Plus-GX-GUI")
  endif()
  install(
    FILES "${GENPLUSGX_RCHEEVOS_LICENSE}"
    DESTINATION "${genplusgx_achievement_license_destination}"
    RENAME "rcheevos-MIT.txt"
  )
endif()

if(GENPLUSGX_ENABLE_ACHIEVEMENTS OR GENPLUSGX_ENABLE_CLOUD_SYNC)
  if(APPLE)
    set(genplusgx_qtkeychain_license_destination
      "genesis-plus-gx-gui.app/Contents/Resources/licenses")
  else()
    set(genplusgx_qtkeychain_license_destination
      "${CMAKE_INSTALL_DATADIR}/doc/Genesis-Plus-GX-GUI")
  endif()
  install(
    FILES "${GENPLUSGX_QTKEYCHAIN_LICENSE}"
    DESTINATION "${genplusgx_qtkeychain_license_destination}"
    RENAME "QtKeychain-BSD-3-Clause.txt"
  )
endif()

if(GENPLUSGX_ENABLE_SIGNED_UPDATES)
  if(APPLE)
    set(genplusgx_monocypher_license_destination
      "genesis-plus-gx-gui.app/Contents/Resources/licenses")
  else()
    set(genplusgx_monocypher_license_destination
      "${CMAKE_INSTALL_DATADIR}/doc/Genesis-Plus-GX-GUI")
  endif()
  install(FILES "${GENPLUSGX_MONOCYPHER_LICENSE}"
    DESTINATION "${genplusgx_monocypher_license_destination}"
    RENAME "Monocypher-BSD-2-Clause-or-CC0.md")
endif()

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

  set(genplusgx_linux_runtime_targets
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Network
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Sql
    Qt6::Widgets
  )
  if(TARGET SDL3::SDL3-shared)
    list(APPEND genplusgx_linux_runtime_targets SDL3::SDL3-shared)
  else()
    message(FATAL_ERROR "The Linux package requires the shared SDL3 target")
  endif()
  # Stage the resolved imported libraries and their SONAME links explicitly. This
  # lets GET_RUNTIME_DEPENDENCIES inspect only the package closure instead of using
  # fallback search directories that produce deployment warnings in modern CMake.
  foreach(runtime_target IN LISTS genplusgx_linux_runtime_targets)
    if(NOT TARGET "${runtime_target}")
      message(FATAL_ERROR
        "Required Linux runtime target is missing: ${runtime_target}")
    endif()
    install(
      FILES
        "$<TARGET_FILE:${runtime_target}>"
        "$<TARGET_SONAME_FILE:${runtime_target}>"
      DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )
  endforeach()

  # Qt's XCB platform plugin links to a private runtime, and the official Linux Qt
  # build links Core to its bundled ICU. Finding the private XCB CMake component
  # would unnecessarily require the complete XCB development graph. Locate only
  # these shipped runtimes beside the public Qt libraries and stage each versioned
  # implementation plus SONAME link.
  get_filename_component(genplusgx_qt_library_directory
    "${Qt6_DIR}/../.." ABSOLUTE)
  function(genplusgx_install_linux_companion library_basename)
    find_library(genplusgx_companion_library
      NAMES "${library_basename}"
      PATHS "${genplusgx_qt_library_directory}"
      NO_DEFAULT_PATH
      NO_CACHE
    )
    if(NOT genplusgx_companion_library)
      file(GLOB genplusgx_companion_candidates LIST_DIRECTORIES FALSE
        "${genplusgx_qt_library_directory}/lib${library_basename}.so.*")
      set(genplusgx_companion_real_candidates)
      foreach(candidate IN LISTS genplusgx_companion_candidates)
        file(REAL_PATH "${candidate}" real_candidate)
        list(APPEND genplusgx_companion_real_candidates "${real_candidate}")
      endforeach()
      list(REMOVE_DUPLICATES genplusgx_companion_real_candidates)
      list(LENGTH genplusgx_companion_real_candidates candidate_count)
      if(NOT candidate_count EQUAL 1)
        message(FATAL_ERROR
          "Expected one version of ${library_basename} beside Qt, found: "
          "${genplusgx_companion_real_candidates}")
      endif()
      list(GET genplusgx_companion_real_candidates 0
        genplusgx_companion_library)
    endif()
    file(REAL_PATH "${genplusgx_companion_library}"
      genplusgx_companion_real_library)
    get_filename_component(genplusgx_companion_directory
      "${genplusgx_companion_real_library}" DIRECTORY)
    get_filename_component(genplusgx_companion_filename
      "${genplusgx_companion_real_library}" NAME)
    if(NOT genplusgx_companion_filename MATCHES
       "^lib${library_basename}\\.so\\.([0-9]+)")
      message(FATAL_ERROR
        "Cannot determine the SONAME for ${genplusgx_companion_real_library}")
    endif()
    set(genplusgx_companion_soname_library
      "${genplusgx_companion_directory}/lib${library_basename}.so.${CMAKE_MATCH_1}")
    if(NOT EXISTS "${genplusgx_companion_soname_library}")
      message(FATAL_ERROR
        "Required SONAME library is missing: ${genplusgx_companion_soname_library}")
    endif()
    set(genplusgx_companion_runtime_files
      "${genplusgx_companion_real_library}"
      "${genplusgx_companion_soname_library}"
    )
    list(REMOVE_DUPLICATES genplusgx_companion_runtime_files)
    install(
      FILES ${genplusgx_companion_runtime_files}
      DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )
  endfunction()

  foreach(companion_library IN ITEMS Qt6XcbQpa icudata icui18n icuuc)
    genplusgx_install_linux_companion("${companion_library}")
  endforeach()

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

  if(WIN32)
    set(genplusgx_windows_redist "${GENPLUSGX_WINDOWS_REDIST}")
    if(NOT genplusgx_windows_redist AND
       DEFINED ENV{GENPLUSGX_WINDOWS_REDIST})
      set(genplusgx_windows_redist "$ENV{GENPLUSGX_WINDOWS_REDIST}")
    endif()
    if(genplusgx_windows_redist)
      cmake_path(CONVERT "${genplusgx_windows_redist}"
        TO_CMAKE_PATH_LIST genplusgx_windows_redist NORMALIZE)
      if(NOT EXISTS "${genplusgx_windows_redist}")
        message(FATAL_ERROR
          "GENPLUSGX_WINDOWS_REDIST does not exist: ${genplusgx_windows_redist}")
      endif()
      install(PROGRAMS "${genplusgx_windows_redist}" DESTINATION .)
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
      NO_COMPILER_RUNTIME
      DEPLOY_TOOL_OPTIONS
        --no-system-d3d-compiler
        --no-system-dxc-compiler
      NO_TRANSLATIONS
      NO_UNSUPPORTED_PLATFORM_ERROR
    )
  endif()
  install(SCRIPT "${genplusgx_deploy_script}")
endif()
