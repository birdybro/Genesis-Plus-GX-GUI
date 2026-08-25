find_package(Threads REQUIRED)

add_executable(genplusgx_audio_ring_buffer_test audio_ring_buffer_test.cpp)
target_link_libraries(
  genplusgx_audio_ring_buffer_test
  PRIVATE
    GenesisPlusGX::Audio
    genplusgx_project_options
    genplusgx_project_warnings
    Threads::Threads
)

add_test(NAME unit.audio_ring_buffer COMMAND genplusgx_audio_ring_buffer_test)
set_tests_properties(
  unit.audio_ring_buffer PROPERTIES LABELS "unit;audio;concurrency" TIMEOUT 30
)

add_executable(genplusgx_input_snapshot_test input_snapshot_test.cpp)
target_link_libraries(
  genplusgx_input_snapshot_test
  PRIVATE
    GenesisPlusGX::FrontendTypes
    genplusgx_project_warnings
)

add_test(NAME unit.input_snapshot COMMAND genplusgx_input_snapshot_test)
set_tests_properties(unit.input_snapshot PROPERTIES LABELS "unit;input")
