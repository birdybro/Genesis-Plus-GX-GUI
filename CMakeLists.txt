cmake_minimum_required(VERSION 3.25)

project(
  GenesisPlusGXGUI
  VERSION 0.1.0
  DESCRIPTION "Native desktop frontend for Genesis Plus GX"
  HOMEPAGE_URL "https://github.com/birdybro/Genesis-Plus-GX-GUI"
  LANGUAGES C CXX
)

include(CTest)
include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(GENPLUSGX_BUILD_DESKTOP "Build the Qt desktop application" ON)
option(GENPLUSGX_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(GENPLUSGX_ENABLE_CHD "Enable CHD disc-image support" ON)
option(GENPLUSGX_ENABLE_TREMOR "Enable bundled Tremor Ogg/Vorbis decoding" ON)
option(
  GENPLUSGX_ENABLE_EXTERNAL_FIXTURE_TESTS
  "Enable tests that require user-supplied firmware or hardware fixtures"
  OFF
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(GENPLUSGX_APP_NAME "Genesis Plus GX GUI")
set(GENPLUSGX_APP_ID "org.genesisplusgx.gui")
set(GENPLUSGX_VERSION "${PROJECT_VERSION}")

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets Sql Test)
find_package(SDL3 3.2 REQUIRED CONFIG)

execute_process(
  COMMAND git rev-parse --short=12 HEAD
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  OUTPUT_VARIABLE GENPLUSGX_GIT_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT GENPLUSGX_GIT_COMMIT)
  set(GENPLUSGX_GIT_COMMIT "unknown")
endif()

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/genplusgx/version.h"
  @ONLY
)

add_library(genplusgx_project_options INTERFACE)
target_compile_features(genplusgx_project_options INTERFACE cxx_std_20)
target_include_directories(
  genplusgx_project_options
  INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
)

add_library(genplusgx_project_warnings INTERFACE)
include(CompilerWarnings)
genplusgx_set_project_warnings(genplusgx_project_warnings)

if(GENPLUSGX_ENABLE_SANITIZERS)
  include(Sanitizers)
  genplusgx_enable_sanitizers(genplusgx_project_options)
endif()

add_library(genplusgx_frontend_types INTERFACE)
add_library(GenesisPlusGX::FrontendTypes ALIAS genplusgx_frontend_types)
target_include_directories(
  genplusgx_frontend_types
  INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/desktop/core/include>
)
target_link_libraries(genplusgx_frontend_types INTERFACE genplusgx_project_options)

message(STATUS "Genesis Plus GX GUI ${GENPLUSGX_VERSION} (${GENPLUSGX_GIT_COMMIT})")
message(STATUS "Qt: ${Qt6_VERSION}")
message(STATUS "SDL3: ${SDL3_VERSION}")
message(STATUS "Desktop frontend: ${GENPLUSGX_BUILD_DESKTOP}")
message(STATUS "Tests: ${BUILD_TESTING}")

add_subdirectory(desktop/video)
add_subdirectory(desktop/audio)
add_subdirectory(desktop/timing)
add_subdirectory(desktop/core)
add_subdirectory(desktop/persistence)
add_subdirectory(desktop/diagnostics)
add_subdirectory(desktop/cheats)
add_subdirectory(desktop/screenshots)
add_subdirectory(desktop/input)
add_subdirectory(desktop/platform)
add_subdirectory(desktop/settings)
add_subdirectory(desktop/library)

if(GENPLUSGX_BUILD_DESKTOP)
  add_subdirectory(desktop/ui)
  add_subdirectory(desktop/app)
  include(Packaging)
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
