cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS
    GENPLUSGX_DMG_TEST_STATE GENPLUSGX_DMG_TEST_SUCCEED_ON
    GENPLUSGX_DMG_TEST_TRANSIENT)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

set(attempt 0)
if(EXISTS "${GENPLUSGX_DMG_TEST_STATE}")
  file(READ "${GENPLUSGX_DMG_TEST_STATE}" attempt)
endif()
math(EXPR attempt "${attempt} + 1")
file(WRITE "${GENPLUSGX_DMG_TEST_STATE}" "${attempt}")

if(attempt LESS GENPLUSGX_DMG_TEST_SUCCEED_ON)
  if(GENPLUSGX_DMG_TEST_TRANSIENT)
    message(FATAL_ERROR "hdiutil: create failed - Resource busy")
  endif()
  message(FATAL_ERROR "hdiutil: create failed - Invalid argument")
endif()
