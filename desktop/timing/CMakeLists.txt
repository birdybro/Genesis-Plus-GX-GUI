add_library(genplusgx_timing STATIC src/frame_pacer.cpp)
add_library(GenesisPlusGX::Timing ALIAS genplusgx_timing)
target_include_directories(
  genplusgx_timing PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
  genplusgx_timing
  PUBLIC genplusgx_project_options
  PRIVATE genplusgx_project_warnings
)
set_target_properties(genplusgx_timing PROPERTIES OUTPUT_NAME genplusgx_timing)
