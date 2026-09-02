find_package(Threads REQUIRED)

set(genplusgx_physical_media_source src/physical_media_stub.cpp)
if(WIN32)
  set(genplusgx_physical_media_source src/physical_media_windows.cpp)
elseif(APPLE)
  set(genplusgx_physical_media_source src/physical_media_macos.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(genplusgx_physical_media_source src/physical_media_linux.cpp)
endif()

add_library(genplusgx_platform STATIC
  include/genplusgx/platform/bios_manager.h
  include/genplusgx/platform/physical_media.h
  src/bios_manager.cpp
  src/physical_media.cpp
  ${genplusgx_physical_media_source}
)
add_library(GenesisPlusGX::Platform ALIAS genplusgx_platform)
target_include_directories(
  genplusgx_platform
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(
  genplusgx_platform
  PUBLIC GenesisPlusGX::Persistence Qt6::Core
  PRIVATE Threads::Threads genplusgx_project_options genplusgx_project_warnings
)

if(APPLE)
  target_link_libraries(
    genplusgx_platform PRIVATE "-framework CoreFoundation" "-framework IOKit"
  )
endif()
