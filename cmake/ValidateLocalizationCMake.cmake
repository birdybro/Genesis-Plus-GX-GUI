cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS SOURCE_ROOT MINIMUM_QT_VERSION)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

if(MINIMUM_QT_VERSION VERSION_LESS 6.8)
  message(FATAL_ERROR
    "Localization source-target extraction requires Qt 6.8 or newer")
endif()

file(READ "${SOURCE_ROOT}/desktop/app/CMakeLists.txt" app_cmake)
if(app_cmake MATCHES "(^|[ \t\r\n])QM_OUTPUT_DIRECTORY([ \t\r\n]|$)")
  message(FATAL_ERROR
    "QM_OUTPUT_DIRECTORY requires Qt 6.9 and violates the Qt 6.8 baseline")
endif()
if(NOT app_cmake MATCHES
   "set_source_files_properties\\([^)]*OUTPUT_LOCATION")
  message(FATAL_ERROR
    "Qt 6.8 translation output must use the TS OUTPUT_LOCATION property")
endif()

message(STATUS
  "Validated localization CMake against Qt ${MINIMUM_QT_VERSION} grammar")
