add_subdirectory(infrastructure)
add_subdirectory(utilities)
add_subdirectory(core)
add_subdirectory(unit)

if(GENPLUSGX_BUILD_DESKTOP)
  add_subdirectory(gui)
endif()
