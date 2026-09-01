add_subdirectory(infrastructure)
add_subdirectory(utilities)
add_subdirectory(core)

if(GENPLUSGX_BUILD_DESKTOP)
  if(NOT GENPLUSGX_TRANSLATION_CATALOGS)
    message(FATAL_ERROR "Desktop translation catalogs were not configured")
  endif()
  list(GET GENPLUSGX_TRANSLATION_CATALOGS 0 genplusgx_test_pseudo_qm)
endif()

add_subdirectory(unit)

if(GENPLUSGX_BUILD_DESKTOP)
  add_subdirectory(gui)
endif()
