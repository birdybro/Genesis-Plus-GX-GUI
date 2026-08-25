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

add_executable(genplusgx_audio_output_test audio_output_test.cpp)
target_link_libraries(
  genplusgx_audio_output_test
  PRIVATE GenesisPlusGX::Audio genplusgx_project_warnings Threads::Threads
)
add_test(NAME unit.audio_output COMMAND genplusgx_audio_output_test)
set_tests_properties(
  unit.audio_output
  PROPERTIES
    LABELS "unit;audio;device"
    TIMEOUT 30
    ENVIRONMENT "SDL_AUDIODRIVER=dummy"
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

add_executable(genplusgx_persistence_test persistence_test.cpp)
target_link_libraries(
  genplusgx_persistence_test
  PRIVATE
    GenesisPlusGX::Persistence
    genplusgx_project_warnings
    genplusgx_test_utilities
)

add_test(NAME unit.persistence COMMAND genplusgx_persistence_test)
set_tests_properties(
  unit.persistence PROPERTIES LABELS "unit;persistence;filesystem" TIMEOUT 30
)

add_executable(genplusgx_state_manager_test state_manager_test.cpp)
target_link_libraries(
  genplusgx_state_manager_test
  PRIVATE
    GenesisPlusGX::Persistence
    genplusgx_project_warnings
)

add_test(NAME unit.state_manager COMMAND genplusgx_state_manager_test)
set_tests_properties(
  unit.state_manager PROPERTIES LABELS "unit;persistence;state" TIMEOUT 30
)

add_executable(genplusgx_bounded_queue_test bounded_queue_test.cpp)
target_link_libraries(
  genplusgx_bounded_queue_test
  PRIVATE GenesisPlusGX::FrontendTypes genplusgx_project_warnings
)
add_test(NAME unit.bounded_queue COMMAND genplusgx_bounded_queue_test)
set_tests_properties(unit.bounded_queue PROPERTIES LABELS "unit;concurrency")

add_executable(genplusgx_frame_exchange_test frame_exchange_test.cpp)
target_link_libraries(
  genplusgx_frame_exchange_test
  PRIVATE GenesisPlusGX::VideoExchange genplusgx_project_warnings
)
add_test(NAME unit.frame_exchange COMMAND genplusgx_frame_exchange_test)
set_tests_properties(unit.frame_exchange PROPERTIES LABELS "unit;video;concurrency")

add_executable(genplusgx_video_geometry_test video_geometry_test.cpp)
target_link_libraries(
  genplusgx_video_geometry_test
  PRIVATE GenesisPlusGX::VideoExchange genplusgx_project_warnings
)
add_test(NAME unit.video_geometry COMMAND genplusgx_video_geometry_test)
set_tests_properties(unit.video_geometry PROPERTIES LABELS "unit;video")

add_executable(genplusgx_frame_pacer_test frame_pacer_test.cpp)
target_link_libraries(
  genplusgx_frame_pacer_test
  PRIVATE GenesisPlusGX::Timing genplusgx_project_warnings
)
add_test(NAME unit.frame_pacer COMMAND genplusgx_frame_pacer_test)
set_tests_properties(unit.frame_pacer PROPERTIES LABELS "unit;timing")
