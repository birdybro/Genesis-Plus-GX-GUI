cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED GENPLUSGX_SMOKE_EXECUTABLE OR
   NOT EXISTS "${GENPLUSGX_SMOKE_EXECUTABLE}")
  message(FATAL_ERROR "The desktop smoke-test executable does not exist")
endif()
if(NOT DEFINED GENPLUSGX_SMOKE_ROOT OR
   NOT IS_ABSOLUTE "${GENPLUSGX_SMOKE_ROOT}")
  message(FATAL_ERROR "The desktop smoke-test root must be absolute")
endif()
if(NOT DEFINED GENPLUSGX_SMOKE_BASE OR
   NOT IS_ABSOLUTE "${GENPLUSGX_SMOKE_BASE}")
  message(FATAL_ERROR "The desktop smoke-test base must be absolute")
endif()
cmake_path(
  IS_PREFIX GENPLUSGX_SMOKE_BASE "${GENPLUSGX_SMOKE_ROOT}"
  NORMALIZE smoke_root_is_scoped
)
if(NOT smoke_root_is_scoped OR
   "${GENPLUSGX_SMOKE_ROOT}" STREQUAL "${GENPLUSGX_SMOKE_BASE}")
  message(FATAL_ERROR "The desktop smoke-test root is outside its build directory")
endif()
if(DEFINED GENPLUSGX_SMOKE_PORTABLE_EXECUTABLE AND
   NOT IS_ABSOLUTE "${GENPLUSGX_SMOKE_PORTABLE_EXECUTABLE}")
  message(FATAL_ERROR "The portable executable test seam must be absolute")
endif()

file(REMOVE_RECURSE "${GENPLUSGX_SMOKE_ROOT}")
file(MAKE_DIRECTORY "${GENPLUSGX_SMOKE_ROOT}")
if(GENPLUSGX_SMOKE_PSEUDO_LANGUAGE)
  file(MAKE_DIRECTORY "${GENPLUSGX_SMOKE_ROOT}/config")
  file(WRITE
    "${GENPLUSGX_SMOKE_ROOT}/config/appearance-settings.json"
    "{\n"
    "  \"schemaVersion\": 3,\n"
    "  \"appearance\": {\n"
    "    \"theme\": \"system\",\n"
    "    \"language\": \"en_XA\",\n"
    "    \"developerToolsEnabled\": false\n"
    "  }\n"
    "}\n")
endif()
if(GENPLUSGX_SMOKE_INJECT_CORRUPT_SETTINGS)
  file(MAKE_DIRECTORY "${GENPLUSGX_SMOKE_ROOT}/config")
  file(WRITE
    "${GENPLUSGX_SMOKE_ROOT}/config/audio-settings.json"
    "{ this is intentionally invalid startup-test JSON")
  file(WRITE
    "${GENPLUSGX_SMOKE_ROOT}/config/speed-settings.json"
    "{ this is intentionally invalid startup-test JSON")
  file(WRITE
    "${GENPLUSGX_SMOKE_ROOT}/config/run-ahead-settings.json"
    "{ this is intentionally invalid startup-test JSON")
endif()

set(smoke_command
  "${CMAKE_COMMAND}" -E env)
if(GENPLUSGX_SMOKE_NATIVE_PLATFORM)
  list(APPEND smoke_command "--unset=QT_QPA_PLATFORM")
else()
  list(APPEND smoke_command "QT_QPA_PLATFORM=offscreen")
endif()
list(APPEND smoke_command
    "SDL_AUDIODRIVER=dummy"
    "GENPLUSGX_FORCE_SOFTWARE_VIDEO=1"
    "GENPLUSGX_TEST_MODE=1"
    "GENPLUSGX_TEST_DATA_ROOT=${GENPLUSGX_SMOKE_ROOT}"
    "GENPLUSGX_TEST_AUTO_QUIT_MS=250"
)
if(DEFINED GENPLUSGX_SMOKE_PORTABLE_EXECUTABLE)
  list(APPEND smoke_command
    "GENPLUSGX_TEST_PORTABLE_EXECUTABLE=${GENPLUSGX_SMOKE_PORTABLE_EXECUTABLE}")
endif()
list(APPEND smoke_command "${GENPLUSGX_SMOKE_EXECUTABLE}")
if(GENPLUSGX_SMOKE_PORTABLE)
  list(APPEND smoke_command "--portable")
endif()
execute_process(
  COMMAND ${smoke_command}
  RESULT_VARIABLE smoke_result
  OUTPUT_VARIABLE smoke_stdout
  ERROR_VARIABLE smoke_stderr
  TIMEOUT 30
)

if(NOT "${smoke_result}" STREQUAL "0")
  message(FATAL_ERROR
    "Desktop startup smoke test exited ${smoke_result}.\n"
    "stdout:\n${smoke_stdout}\n"
    "stderr:\n${smoke_stderr}")
endif()

foreach(required_directory IN ITEMS
    config saves states screenshots recordings library logs cache)
  if(NOT IS_DIRECTORY "${GENPLUSGX_SMOKE_ROOT}/${required_directory}")
    message(FATAL_ERROR
      "Desktop startup did not create ${required_directory} in its isolated root")
  endif()
endforeach()

set(frontend_log "${GENPLUSGX_SMOKE_ROOT}/logs/frontend.jsonl")
if(NOT EXISTS "${frontend_log}")
  message(FATAL_ERROR "Desktop startup did not create its structured log")
endif()
file(READ "${frontend_log}" frontend_log_text)
set(required_messages
    "Application startup:"
    "Renderer selected:"
    "Automated startup smoke test entered the event loop."
    "Application shutdown complete.")
if(GENPLUSGX_SMOKE_PSEUDO_LANGUAGE)
  list(APPEND required_messages
    "Interface language: requested en_XA effective en_XA English fallback false")
endif()
if(GENPLUSGX_SMOKE_PORTABLE)
  list(APPEND required_messages "Application data mode: Portable")
else()
  list(APPEND required_messages "Application data mode: Custom")
endif()
foreach(required_message IN LISTS required_messages)
  string(FIND "${frontend_log_text}" "${required_message}" message_position)
  if(message_position EQUAL -1)
    message(FATAL_ERROR
      "Desktop structured log is missing: ${required_message}\n"
      "Log:\n${frontend_log_text}")
  endif()
endforeach()

if(GENPLUSGX_SMOKE_INJECT_CORRUPT_SETTINGS)
  foreach(required_error_message IN ITEMS
      "audio settings file"
      "speed settings file"
      "run-ahead settings file"
      "Startup issues presented:")
    string(FIND
      "${frontend_log_text}" "${required_error_message}" error_message_position)
    if(error_message_position EQUAL -1)
      message(FATAL_ERROR
        "Desktop startup did not surface: ${required_error_message}\n"
        "Log:\n${frontend_log_text}")
    endif()
  endforeach()
endif()

if(NOT EXISTS "${GENPLUSGX_SMOKE_ROOT}/library/game-library.sqlite3")
  message(FATAL_ERROR "Desktop startup did not initialize the library database")
endif()
