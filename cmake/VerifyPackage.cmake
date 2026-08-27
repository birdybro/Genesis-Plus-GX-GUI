cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS PACKAGE_ROOT VERIFY_PLATFORM)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

file(REAL_PATH "${PACKAGE_ROOT}" package_root)
if(NOT IS_DIRECTORY "${package_root}")
  message(FATAL_ERROR "Package root does not exist: ${package_root}")
endif()

function(require_file path description)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Missing ${description}: ${path}")
  endif()
endfunction()

function(require_match pattern description)
  file(GLOB_RECURSE matches LIST_DIRECTORIES FALSE "${package_root}/${pattern}")
  if(NOT matches)
    message(FATAL_ERROR "Missing ${description} matching ${pattern}")
  endif()
endfunction()

if(VERIFY_PLATFORM STREQUAL "windows")
  require_file(
    "${package_root}/bin/genesis-plus-gx-gui.exe"
    "Windows application executable")
  require_match("*Qt6Core.dll" "deployed Qt Core runtime")
  require_match("*SDL3*.dll" "deployed SDL3 runtime")
  require_match("*qwindows.dll" "deployed Qt Windows platform plugin")
  require_match("*librashader.dll" "deployed libretro shader runtime")
  require_match(
    "*vc_redist.x64.exe"
    "official Microsoft Visual C++ x64 Redistributable installer")
  set(shader_directory
    "${package_root}/share/Genesis-Plus-GX-GUI/shaders")
  set(librashader_license
    "${package_root}/share/doc/Genesis-Plus-GX-GUI/librashader-MPL-2.0.md")
elseif(VERIFY_PLATFORM STREQUAL "macos")
  set(bundle "${package_root}/genesis-plus-gx-gui.app")
  require_file(
    "${bundle}/Contents/MacOS/genesis-plus-gx-gui"
    "macOS bundle executable")
  require_file(
    "${bundle}/Contents/Frameworks/QtCore.framework/Versions/A/QtCore"
    "deployed Qt Core framework")
  require_match("*SDL3*.dylib" "deployed SDL3 runtime")
  require_match("*libqcocoa.dylib" "deployed Qt Cocoa platform plugin")
  require_match("*librashader.dylib" "deployed libretro shader runtime")
  set(shader_directory "${bundle}/Contents/Resources/shaders")
  set(librashader_license
    "${bundle}/Contents/Resources/licenses/librashader-MPL-2.0.md")
elseif(VERIFY_PLATFORM STREQUAL "linux")
  require_file(
    "${package_root}/bin/genesis-plus-gx-gui"
    "Linux application executable")
  require_match("*libQt6Core.so*" "deployed Qt Core runtime")
  require_match("*libSDL3.so*" "deployed SDL3 runtime")
  require_match("*librashader.so" "deployed libretro shader runtime")
  require_match("*libicudata.so.*" "deployed ICU data runtime")
  require_match("*libicui18n.so.*" "deployed ICU internationalization runtime")
  require_match("*libicuuc.so.*" "deployed ICU common runtime")
  file(GLOB_RECURSE platform_plugins LIST_DIRECTORIES FALSE
    "${package_root}/*libqxcb.so"
    "${package_root}/*libqoffscreen.so")
  if(NOT platform_plugins)
    message(FATAL_ERROR "Missing deployed Qt Linux platform plugin")
  endif()
  require_match(
    "*libqxcb-*-integration.so"
    "deployed Qt XCB OpenGL integration plugin")
  set(shader_directory
    "${package_root}/share/Genesis-Plus-GX-GUI/shaders")
  set(librashader_license
    "${package_root}/share/doc/Genesis-Plus-GX-GUI/librashader-MPL-2.0.md")
else()
  message(FATAL_ERROR "Unsupported VERIFY_PLATFORM value: ${VERIFY_PLATFORM}")
endif()

require_file(
  "${shader_directory}/genplusgx-crt.slangp" "built-in CRT shader preset")
require_file(
  "${shader_directory}/genplusgx-crt.slang" "built-in CRT shader source")
require_file("${librashader_license}" "librashader MPL-2.0 license")

message(STATUS "Verified self-contained ${VERIFY_PLATFORM} layout at ${package_root}")
