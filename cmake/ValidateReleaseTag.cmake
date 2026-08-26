cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED RELEASE_TAG OR RELEASE_TAG STREQUAL "")
  message(FATAL_ERROR "RELEASE_TAG is required")
endif()

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
  get_filename_component(SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

if(NOT RELEASE_TAG MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)$")
  message(FATAL_ERROR
    "Release tag '${RELEASE_TAG}' must use the stable vMAJOR.MINOR.PATCH form")
endif()
set(tag_version "${CMAKE_MATCH_1}")

file(STRINGS "${SOURCE_ROOT}/CMakeLists.txt" version_lines
  REGEX "^[ \\t]*VERSION[ \\t]+[0-9]+\\.[0-9]+\\.[0-9]+[ \\t]*$")
list(LENGTH version_lines version_line_count)
if(NOT version_line_count EQUAL 1)
  message(FATAL_ERROR "Could not read GenesisPlusGXGUI VERSION from CMakeLists.txt")
endif()
list(GET version_lines 0 version_line)
string(REGEX MATCH "([0-9]+\\.[0-9]+\\.[0-9]+)" project_version "${version_line}")
set(project_version "${CMAKE_MATCH_1}")

if(NOT tag_version STREQUAL project_version)
  message(FATAL_ERROR
    "Release tag '${RELEASE_TAG}' does not match project version '${project_version}'")
endif()

message(STATUS "Validated release tag ${RELEASE_TAG} for project ${project_version}")
