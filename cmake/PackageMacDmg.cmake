cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS CPACK_CONFIG PACKAGE_DIRECTORY)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
  if(NOT IS_ABSOLUTE "${${required}}")
    message(FATAL_ERROR "${required} must be absolute")
  endif()
endforeach()
if(NOT EXISTS "${CPACK_CONFIG}")
  message(FATAL_ERROR "CPack configuration does not exist: ${CPACK_CONFIG}")
endif()
cmake_path(NORMAL_PATH PACKAGE_DIRECTORY OUTPUT_VARIABLE package_directory)
cmake_path(GET package_directory ROOT_PATH package_directory_root)
if("${package_directory}" STREQUAL "${package_directory_root}")
  message(FATAL_ERROR "PACKAGE_DIRECTORY cannot be a filesystem root")
endif()

if(DEFINED GENPLUSGX_DMG_TEST_DRIVER)
  foreach(required IN ITEMS
      GENPLUSGX_DMG_TEST_DRIVER GENPLUSGX_DMG_TEST_STATE
      GENPLUSGX_DMG_TEST_SUCCEED_ON GENPLUSGX_DMG_TEST_TRANSIENT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
      message(FATAL_ERROR "${required} is required by the DMG retry test seam")
    endif()
  endforeach()
  set(package_command
    "${CMAKE_COMMAND}"
    "-DGENPLUSGX_DMG_TEST_STATE=${GENPLUSGX_DMG_TEST_STATE}"
    "-DGENPLUSGX_DMG_TEST_SUCCEED_ON=${GENPLUSGX_DMG_TEST_SUCCEED_ON}"
    "-DGENPLUSGX_DMG_TEST_TRANSIENT=${GENPLUSGX_DMG_TEST_TRANSIENT}"
    -P "${GENPLUSGX_DMG_TEST_DRIVER}")
else()
  find_program(cpack_executable NAMES cpack REQUIRED)
  set(package_command
    "${cpack_executable}"
    --config "${CPACK_CONFIG}"
    -G DragNDrop
    -B "${package_directory}")
endif()

set(maximum_attempts 3)
foreach(attempt RANGE 1 ${maximum_attempts})
  execute_process(
    COMMAND ${package_command}
    RESULT_VARIABLE package_result
    OUTPUT_VARIABLE package_output
    ERROR_VARIABLE package_error)
  if(NOT "${package_output}" STREQUAL "")
    message(STATUS "${package_output}")
  endif()
  if(package_result EQUAL 0)
    return()
  endif()

  set(package_diagnostic "${package_output}${package_error}")
  if(NOT package_diagnostic MATCHES "Resource busy")
    message(FATAL_ERROR
      "DMG packaging failed with a non-retryable error:\n${package_diagnostic}")
  endif()
  if(attempt EQUAL maximum_attempts)
    message(FATAL_ERROR
      "DMG packaging remained resource-busy after ${maximum_attempts} attempts:\n"
      "${package_diagnostic}")
  endif()

  message(STATUS
    "DMG packaging encountered a transient resource-busy condition; retrying "
    "attempt ${attempt} of ${maximum_attempts}.")
  file(REMOVE_RECURSE
    "${package_directory}/_CPack_Packages/Darwin/DragNDrop")
  if(NOT DEFINED GENPLUSGX_DMG_TEST_DRIVER)
    math(EXPR retry_delay_seconds "${attempt} * 5")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E sleep "${retry_delay_seconds}")
  endif()
endforeach()
