add_executable(genplusgx_core_link_test core_link_test.cpp)
target_link_libraries(
  genplusgx_core_link_test
  PRIVATE GenesisPlusGX::Core genplusgx_project_options genplusgx_project_warnings
)

add_test(NAME core.library_link COMMAND genplusgx_core_link_test)
set_tests_properties(core.library_link PROPERTIES LABELS "core;smoke")

add_executable(genplusgx_core_synthetic_rom_test synthetic_rom_test.cpp)
target_link_libraries(
  genplusgx_core_synthetic_rom_test
  PRIVATE
    GenesisPlusGX::Core
    genplusgx_project_options
    genplusgx_project_warnings
    genplusgx_test_utilities
)

add_test(NAME core.synthetic_genesis_rom COMMAND genplusgx_core_synthetic_rom_test)
set_tests_properties(
  core.synthetic_genesis_rom PROPERTIES LABELS "core;integration;fixture"
)

add_executable(genplusgx_core_lifecycle_test core_lifecycle_test.cpp)
target_link_libraries(
  genplusgx_core_lifecycle_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    genplusgx_project_options
    genplusgx_project_warnings
    genplusgx_test_utilities
)

add_test(NAME core.lifecycle COMMAND genplusgx_core_lifecycle_test)
set_tests_properties(
  core.lifecycle PROPERTIES LABELS "core;integration;lifecycle" TIMEOUT 30
)

add_executable(genplusgx_core_video_test core_video_test.cpp)
target_link_libraries(
  genplusgx_core_video_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    genplusgx_project_options
    genplusgx_project_warnings
    genplusgx_test_utilities
)

add_test(NAME core.video_frame COMMAND genplusgx_core_video_test)
set_tests_properties(
  core.video_frame PROPERTIES LABELS "core;integration;video;fixture" TIMEOUT 30
)

add_executable(genplusgx_core_audio_test core_audio_test.cpp)
target_link_libraries(
  genplusgx_core_audio_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    genplusgx_project_options
    genplusgx_project_warnings
    genplusgx_test_utilities
)

add_test(NAME core.audio_samples COMMAND genplusgx_core_audio_test)
set_tests_properties(
  core.audio_samples PROPERTIES LABELS "core;integration;audio;fixture" TIMEOUT 30
)

add_executable(genplusgx_core_input_test core_input_test.cpp)
target_link_libraries(
  genplusgx_core_input_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    genplusgx_project_options
    genplusgx_project_warnings
    genplusgx_test_utilities
)

add_test(NAME core.input_snapshot COMMAND genplusgx_core_input_test)
set_tests_properties(
  core.input_snapshot PROPERTIES LABELS "core;integration;input;fixture" TIMEOUT 30
)

add_executable(genplusgx_core_state_test core_state_test.cpp)
target_link_libraries(
  genplusgx_core_state_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    GenesisPlusGX::Persistence
    genplusgx_project_options
    genplusgx_project_warnings
    genplusgx_test_utilities
)

add_test(NAME core.state_round_trip COMMAND genplusgx_core_state_test)
set_tests_properties(
  core.state_round_trip
  PROPERTIES LABELS "core;integration;state;persistence;fixture" TIMEOUT 30
)

add_executable(genplusgx_emulation_worker_test emulation_worker_test.cpp)
target_link_libraries(
  genplusgx_emulation_worker_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    genplusgx_test_utilities
    genplusgx_project_warnings
)
add_test(NAME core.emulation_worker COMMAND genplusgx_emulation_worker_test)
set_tests_properties(
  core.emulation_worker
  PROPERTIES LABELS "core;integration;fixture;concurrency;lifecycle;state;input" TIMEOUT 30
)
