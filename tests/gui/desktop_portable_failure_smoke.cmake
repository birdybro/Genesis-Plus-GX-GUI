cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS
    GENPLUSGX_SMOKE_EXECUTABLE GENPLUSGX_SMOKE_ROOT GENPLUSGX_SMOKE_BASE
    GENPLUSGX_SMOKE_PORTABLE_EXECUTABLE)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
if(NOT EXISTS "${GENPLUSGX_SMOKE_EXECUTABLE}")
  message(FATAL_ERROR "The desktop smoke-test executable does not exist")
endif()
foreach(path_variable IN ITEMS
    GENPLUSGX_SMOKE_ROOT GENPLUSGX_SMOKE_BASE
    GENPLUSGX_SMOKE_PORTABLE_EXECUTABLE)
  if(NOT IS_ABSOLUTE "${${path_variable}}")
    message(FATAL_ERROR "${path_variable} must be absolute")
  endif()
endforeach()
cmake_path(
  IS_PREFIX GENPLUSGX_SMOKE_BASE "${GENPLUSGX_SMOKE_ROOT}"
  NORMALIZE smoke_root_is_scoped)
if(NOT smoke_root_is_scoped OR
   "${GENPLUSGX_SMOKE_ROOT}" STREQUAL "${GENPLUSGX_SMOKE_BASE}")
  message(FATAL_ERROR "The portable failure root is outside its build directory")
endif()

file(REMOVE_RECURSE "${GENPLUSGX_SMOKE_ROOT}")
get_filename_component(smoke_parent "${GENPLUSGX_SMOKE_ROOT}" DIRECTORY)
file(MAKE_DIRECTORY "${smoke_parent}")
file(WRITE "${GENPLUSGX_SMOKE_ROOT}"
  "This regular file intentionally blocks the portable-data directory.\n")

execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env
    "QT_QPA_PLATFORM=offscreen"
    "SDL_AUDIODRIVER=dummy"
    "GENPLUSGX_FORCE_SOFTWARE_VIDEO=1"
    "GENPLUSGX_TEST_MODE=1"
    "GENPLUSGX_TEST_DATA_ROOT=${GENPLUSGX_SMOKE_ROOT}"
    "GENPLUSGX_TEST_PORTABLE_EXECUTABLE=${GENPLUSGX_SMOKE_PORTABLE_EXECUTABLE}"
    "GENPLUSGX_TEST_AUTO_QUIT_MS=250"
    "${GENPLUSGX_SMOKE_EXECUTABLE}" --portable
  RESULT_VARIABLE smoke_result
  OUTPUT_VARIABLE smoke_stdout
  ERROR_VARIABLE smoke_stderr
  TIMEOUT 30)

if(NOT "${smoke_result}" STREQUAL "2")
  message(FATAL_ERROR
    "Blocked portable startup exited ${smoke_result} instead of 2.\n"
    "stdout:\n${smoke_stdout}\n"
    "stderr:\n${smoke_stderr}")
endif()
string(FIND "${smoke_stderr}"
  "Unable to create application-data directory" diagnostic_position)
if(diagnostic_position EQUAL -1)
  message(FATAL_ERROR
    "Blocked portable startup did not explain its data-root failure.\n"
    "stderr:\n${smoke_stderr}")
endif()
if(IS_DIRECTORY "${GENPLUSGX_SMOKE_ROOT}")
  message(FATAL_ERROR "Portable startup replaced the blocking file unexpectedly")
endif()
