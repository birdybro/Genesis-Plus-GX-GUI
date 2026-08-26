add_library(
  genplusgx_timing STATIC
  include/genplusgx/timing/frame_rate_sampler.h
  include/genplusgx/timing/host_timer_resolution.h
  src/frame_rate_sampler.cpp
  src/frame_pacer.cpp
  src/host_timer_resolution.cpp
)
add_library(GenesisPlusGX::Timing ALIAS genplusgx_timing)
target_include_directories(
  genplusgx_timing PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
  genplusgx_timing
  PUBLIC genplusgx_project_options
  PRIVATE genplusgx_project_warnings
)
if(WIN32)
  target_link_libraries(genplusgx_timing PRIVATE winmm)
endif()
set_target_properties(genplusgx_timing PROPERTIES OUTPUT_NAME genplusgx_timing)
