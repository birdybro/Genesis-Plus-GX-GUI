if(NOT TARGET genplusgx_desktop)
  message(FATAL_ERROR "Desktop packaging requires the genplusgx_desktop target")
endif()

if(WIN32)
  set(GENPLUSGX_PACKAGE_PLATFORM "windows")
  set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
  set(GENPLUSGX_PACKAGE_PLATFORM "macos")
  set(CPACK_GENERATOR "ZIP;DragNDrop")
else()
  set(GENPLUSGX_PACKAGE_PLATFORM "linux")
  set(CPACK_GENERATOR "TGZ")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" genplusgx_processor)
if(genplusgx_processor MATCHES "^(amd64|x86_64|x64)$")
  set(GENPLUSGX_PACKAGE_ARCHITECTURE "x86_64")
elseif(genplusgx_processor MATCHES "^(aarch64|arm64)$")
  set(GENPLUSGX_PACKAGE_ARCHITECTURE "arm64")
elseif(genplusgx_processor MATCHES "^(i[3-6]86|x86)$")
  set(GENPLUSGX_PACKAGE_ARCHITECTURE "x86")
else()
  string(REGEX REPLACE "[^a-z0-9_.-]" "-"
    GENPLUSGX_PACKAGE_ARCHITECTURE "${genplusgx_processor}")
endif()

set(GENPLUSGX_PACKAGE_BASENAME
  "Genesis-Plus-GX-GUI-${PROJECT_VERSION}-${GENPLUSGX_PACKAGE_PLATFORM}-${GENPLUSGX_PACKAGE_ARCHITECTURE}")

set(genplusgx_documentation
  "${PROJECT_SOURCE_DIR}/LICENSE.txt"
  "${PROJECT_SOURCE_DIR}/README.md"
)
foreach(document IN ITEMS
    CHANGELOG.md
    CONTRIBUTING.md
    THIRD_PARTY_NOTICES.md)
  if(EXISTS "${PROJECT_SOURCE_DIR}/${document}")
    list(APPEND genplusgx_documentation "${PROJECT_SOURCE_DIR}/${document}")
  endif()
endforeach()
foreach(document IN ITEMS
    APPEARANCE_AND_ACCESSIBILITY.md
    ACHIEVEMENTS.md
    ARCHITECTURE.md
    BIOS.md
    BUILDING.md
    CHEATS.md
    DEBUG_TOOLS.md
    DEVELOPMENT.md
    FINAL_TEST_REPORT.md
    GAME_INFORMATION.md
    GAME_LIBRARY.md
    INPUT_CONFIGURATION.md
    KEYBOARD_SHORTCUTS.md
    LIBRETRO_SHADERS.md
    LOCALIZATION.md
    ARTWORK_OVERLAYS.md
    DISPLAY_SYNCHRONIZATION.md
    LOGGING_AND_DIAGNOSTICS.md
    NETPLAY.md
    PACKAGING.md
    PHYSICAL_MEDIA.md
    PORTABLE_MODE.md
    PER_GAME_SETTINGS.md
    RECORDING.md
    RUN_AHEAD.md
    RELEASES.md
    REQUIREMENTS_AUDIT.md
    SAVE_STATES.md
    TESTING.md
    TEST_MATRIX.md
    UPSTREAM_MAINTENANCE.md
    USER_GUIDE.md)
  list(APPEND genplusgx_documentation "${PROJECT_SOURCE_DIR}/docs/${document}")
endforeach()

if(APPLE)
  install(
    FILES ${genplusgx_documentation}
    DESTINATION "genesis-plus-gx-gui.app/Contents/Resources/documentation"
  )
else()
  install(
    FILES ${genplusgx_documentation}
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/Genesis-Plus-GX-GUI"
  )
endif()

if(UNIX AND NOT APPLE)
  install(
    FILES "${PROJECT_SOURCE_DIR}/desktop/resources/org.genesisplusgx.gui.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
  )
  install(
    FILES "${PROJECT_SOURCE_DIR}/desktop/resources/org.genesisplusgx.gui.svg"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
  )
endif()

set(CPACK_PACKAGE_NAME "Genesis-Plus-GX-GUI")
set(CPACK_PACKAGE_VENDOR "Genesis Plus GX GUI contributors")
set(CPACK_PACKAGE_CONTACT "https://github.com/birdybro/Genesis-Plus-GX-GUI/issues")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native desktop frontend for Genesis Plus GX")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${GENPLUSGX_PACKAGE_BASENAME}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE.txt")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_THREADS 0)

set(CPACK_DMG_VOLUME_NAME "Genesis Plus GX GUI ${PROJECT_VERSION}")
set(CPACK_DMG_FORMAT "UDZO")
set(CPACK_DMG_DISABLE_APPLICATIONS_SYMLINK OFF)

include(CPack)
