find_package(Threads REQUIRED)

add_executable(genplusgx_game_file_test game_file_test.cpp)
target_link_libraries(
  genplusgx_game_file_test
  PRIVATE
    GenesisPlusGX::GameFiles
    genplusgx_project_warnings
    genplusgx_test_utilities
)
add_test(NAME unit.game_file COMMAND genplusgx_game_file_test)
set_tests_properties(unit.game_file PROPERTIES LABELS "unit;filesystem;loading")

if(TARGET genplusgx_app_support)
  add_executable(genplusgx_command_line_test command_line_test.cpp)
  target_link_libraries(
    genplusgx_command_line_test
    PRIVATE GenesisPlusGX::AppSupport genplusgx_project_warnings Qt6::Core
  )
  add_test(NAME unit.command_line COMMAND genplusgx_command_line_test)
  set_tests_properties(unit.command_line PROPERTIES LABELS "unit;cli;loading")
endif()

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

add_executable(genplusgx_input_aggregator_test input_aggregator_test.cpp)
target_link_libraries(
  genplusgx_input_aggregator_test
  PRIVATE GenesisPlusGX::Input genplusgx_project_warnings
)
add_test(NAME unit.input_aggregator COMMAND genplusgx_input_aggregator_test)
set_tests_properties(unit.input_aggregator PROPERTIES LABELS "unit;input")

add_executable(genplusgx_controller_input_test controller_input_test.cpp)
target_link_libraries(
  genplusgx_controller_input_test
  PRIVATE GenesisPlusGX::Input genplusgx_project_warnings Threads::Threads
)
add_test(NAME unit.controller_input COMMAND genplusgx_controller_input_test)
set_tests_properties(
  unit.controller_input
  PROPERTIES LABELS "unit;input;controller;device" TIMEOUT 30
)

add_executable(genplusgx_input_profile_test input_profile_test.cpp)
target_link_libraries(
  genplusgx_input_profile_test
  PRIVATE GenesisPlusGX::Input genplusgx_project_warnings
)
add_test(NAME unit.input_profile COMMAND genplusgx_input_profile_test)
set_tests_properties(
  unit.input_profile PROPERTIES LABELS "unit;input;persistence" TIMEOUT 30
)

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

add_executable(genplusgx_backup_store_test backup_store_test.cpp)
target_link_libraries(
  genplusgx_backup_store_test
  PRIVATE
    GenesisPlusGX::Persistence
    genplusgx_project_warnings
    genplusgx_test_utilities
    Qt6::Core
)
add_test(NAME unit.backup_store COMMAND genplusgx_backup_store_test)
set_tests_properties(
  unit.backup_store PROPERTIES LABELS "unit;persistence;filesystem" TIMEOUT 30
)

add_executable(genplusgx_recent_games_test recent_games_test.cpp)
target_link_libraries(
  genplusgx_recent_games_test
  PRIVATE GenesisPlusGX::Persistence genplusgx_project_warnings Qt6::Core
)
add_test(NAME unit.recent_games COMMAND genplusgx_recent_games_test)
set_tests_properties(
  unit.recent_games PROPERTIES LABELS "unit;persistence;loading" TIMEOUT 30
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
