add_library(
  genplusgx_audio STATIC
  src/audio_output.cpp
  src/audio_ring_buffer.cpp
)
add_library(GenesisPlusGX::Audio ALIAS genplusgx_audio)
target_include_directories(
  genplusgx_audio PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
  genplusgx_audio
  PUBLIC GenesisPlusGX::FrontendTypes SDL3::SDL3
  PRIVATE genplusgx_project_warnings
)
set_target_properties(genplusgx_audio PROPERTIES OUTPUT_NAME genplusgx_audio)
