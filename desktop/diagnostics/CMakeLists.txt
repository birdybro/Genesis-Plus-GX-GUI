add_library(genplusgx_diagnostics STATIC
  include/genplusgx/diagnostics/diagnostics.h
  src/diagnostics.cpp
)
add_library(GenesisPlusGX::Diagnostics ALIAS genplusgx_diagnostics)
target_include_directories(
  genplusgx_diagnostics
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(
  genplusgx_diagnostics
  PUBLIC Qt6::Core
  PRIVATE
    SDL3::SDL3
    genplusgx_project_options
    genplusgx_project_warnings
)
