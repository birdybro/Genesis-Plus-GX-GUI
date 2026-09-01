add_executable(genplusgx_core_link_test core_link_test.cpp)
target_link_libraries(
  genplusgx_core_link_test
  PRIVATE GenesisPlusGX::Core genplusgx_project_options genplusgx_project_warnings
)

add_test(NAME core.library_link COMMAND genplusgx_core_link_test)
set_tests_properties(core.library_link PROPERTIES LABELS "core;smoke")

add_executable(
  genplusgx_soft_patch_workflow_test soft_patch_workflow_test.cpp
)
target_link_libraries(
  genplusgx_soft_patch_workflow_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    GenesisPlusGX::Persistence
    genplusgx_project_warnings
    genplusgx_test_utilities
    Qt6::Core
)
add_test(
  NAME integration.soft_patch
  COMMAND genplusgx_soft_patch_workflow_test
)
set_tests_properties(
  integration.soft_patch
  PROPERTIES
    LABELS "core;integration;patch;ips;fixture;lifecycle;persistence"
    TIMEOUT 30
)

add_executable(
  genplusgx_archive_playlist_workflow_test archive_playlist_workflow_test.cpp
)
target_link_libraries(
  genplusgx_archive_playlist_workflow_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    GenesisPlusGX::Persistence
    genplusgx_project_warnings
    genplusgx_test_utilities
    Qt6::Core
)
add_test(
  NAME integration.archive_playlist
  COMMAND genplusgx_archive_playlist_workflow_test
)
set_tests_properties(
  integration.archive_playlist
  PROPERTIES
    LABELS "core;integration;archive;zip;m3u;sega-cd;fixture;lifecycle"
    TIMEOUT 30
)

add_executable(genplusgx_core_debug_test core_debug_test.cpp)
target_link_libraries(
  genplusgx_core_debug_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    genplusgx_project_warnings
    genplusgx_test_utilities
)
add_test(NAME core.debug_tools COMMAND genplusgx_core_debug_test)
set_tests_properties(
  core.debug_tools
  PROPERTIES LABELS "core;integration;debug;fixture;concurrency" TIMEOUT 30
)

add_executable(genplusgx_run_ahead_worker_test run_ahead_worker_test.cpp)
target_link_libraries(
  genplusgx_run_ahead_worker_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    genplusgx_project_warnings
    genplusgx_test_utilities
)
add_test(NAME integration.run_ahead COMMAND genplusgx_run_ahead_worker_test)
set_tests_properties(
  integration.run_ahead
  PROPERTIES
    LABELS "core;integration;run-ahead;state;input;audio;video;sega-cd;fixture;concurrency"
    TIMEOUT 30
)

add_executable(genplusgx_core_cheat_test core_cheat_test.cpp)
target_link_libraries(
  genplusgx_core_cheat_test
  PRIVATE
    GenesisPlusGX::Cheats
    GenesisPlusGX::CoreAdapter
    genplusgx_project_warnings
    genplusgx_test_utilities
)
add_test(NAME core.cheats COMMAND genplusgx_core_cheat_test)
set_tests_properties(
  core.cheats PROPERTIES LABELS "core;integration;cheats;fixture" TIMEOUT 30
)

add_executable(
  genplusgx_per_game_settings_workflow_test
  per_game_settings_workflow_test.cpp
)
target_link_libraries(
  genplusgx_per_game_settings_workflow_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    GenesisPlusGX::Settings
    genplusgx_project_warnings
    genplusgx_test_utilities
)
add_test(
  NAME integration.per_game_settings
  COMMAND genplusgx_per_game_settings_workflow_test
)
set_tests_properties(
  integration.per_game_settings
  PROPERTIES LABELS "core;integration;settings;fixture;concurrency" TIMEOUT 30
)

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

add_executable(genplusgx_eight_bit_systems_test eight_bit_systems_test.cpp)
target_link_libraries(
  genplusgx_eight_bit_systems_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    genplusgx_project_warnings
    genplusgx_test_utilities
)
add_test(NAME core.eight_bit_systems COMMAND genplusgx_eight_bit_systems_test)
set_tests_properties(
  core.eight_bit_systems
  PROPERTIES LABELS "core;integration;fixture;system;sg1000;mark-iii;sms;game-gear"
)

add_executable(
  genplusgx_core_firmware_application_test
  core_firmware_application_test.cpp
)
target_link_libraries(
  genplusgx_core_firmware_application_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    genplusgx_project_warnings
    genplusgx_test_utilities
)
add_test(
  NAME core.firmware_application
  COMMAND genplusgx_core_firmware_application_test
)
set_tests_properties(
  core.firmware_application
  PROPERTIES LABELS "core;integration;firmware;bios;fixture"
)

add_executable(genplusgx_core_backup_memory_test core_backup_memory_test.cpp)
target_link_libraries(
  genplusgx_core_backup_memory_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    genplusgx_project_warnings
    genplusgx_test_utilities
)
add_test(NAME core.backup_memory COMMAND genplusgx_core_backup_memory_test)
set_tests_properties(
  core.backup_memory PROPERTIES LABELS "core;integration;persistence;fixture"
)

add_executable(genplusgx_backup_persistence_test backup_persistence_test.cpp)
target_link_libraries(
  genplusgx_backup_persistence_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    GenesisPlusGX::Persistence
    genplusgx_project_warnings
    genplusgx_test_utilities
    Qt6::Core
)
add_test(NAME core.backup_persistence COMMAND genplusgx_backup_persistence_test)
set_tests_properties(
  core.backup_persistence
  PROPERTIES LABELS "core;integration;persistence;fixture;lifecycle" TIMEOUT 30
)

add_executable(genplusgx_sega_cd_workflow_test sega_cd_workflow_test.cpp)
target_link_libraries(
  genplusgx_sega_cd_workflow_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    GenesisPlusGX::Persistence
    genplusgx_project_warnings
    genplusgx_test_utilities
    Qt6::Core
)
add_test(NAME core.sega_cd_workflow COMMAND genplusgx_sega_cd_workflow_test)
set_tests_properties(
  core.sega_cd_workflow
  PROPERTIES LABELS "core;integration;sega-cd;firmware;disc;persistence;fixture;lifecycle" TIMEOUT 30
)

if(GENPLUSGX_ENABLE_EXTERNAL_FIXTURE_TESTS)
  add_executable(
    genplusgx_sega_cd_external_bios_test
    sega_cd_external_bios_test.cpp
  )
  target_link_libraries(
    genplusgx_sega_cd_external_bios_test
    PRIVATE
      GenesisPlusGX::CoreAdapter
      genplusgx_project_warnings
      genplusgx_test_utilities
  )
  add_test(
    NAME core.sega_cd_external_bios
    COMMAND genplusgx_sega_cd_external_bios_test
  )
  set_tests_properties(
    core.sega_cd_external_bios
    PROPERTIES LABELS "core;integration;sega-cd;firmware;external" TIMEOUT 30
  )
endif()

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

add_executable(genplusgx_core_system_settings_test core_system_settings_test.cpp)
target_link_libraries(
  genplusgx_core_system_settings_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    genplusgx_project_warnings
    genplusgx_test_utilities
)
add_test(NAME core.system_settings COMMAND genplusgx_core_system_settings_test)
set_tests_properties(
  core.system_settings PROPERTIES LABELS "core;integration;system;settings;fixture" TIMEOUT 30
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

add_executable(genplusgx_core_input_devices_test core_input_devices_test.cpp)
target_link_libraries(
  genplusgx_core_input_devices_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    genplusgx_project_warnings
    genplusgx_test_utilities
)
add_test(NAME core.input_devices COMMAND genplusgx_core_input_devices_test)
set_tests_properties(
  core.input_devices
  PROPERTIES LABELS "core;integration;input;controller;multitap;fixture" TIMEOUT 30
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

add_executable(genplusgx_rewind_worker_test rewind_worker_test.cpp)
target_link_libraries(
  genplusgx_rewind_worker_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    genplusgx_test_utilities
    genplusgx_project_warnings
)
add_test(NAME core.rewind_worker COMMAND genplusgx_rewind_worker_test)
set_tests_properties(
  core.rewind_worker
  PROPERTIES LABELS "core;integration;fixture;concurrency;state;rewind;audio" TIMEOUT 30
)
add_test(NAME core.emulation_worker COMMAND genplusgx_emulation_worker_test)
set_tests_properties(
  core.emulation_worker
  PROPERTIES LABELS "core;integration;fixture;concurrency;lifecycle;state;input" TIMEOUT 30
)

add_executable(genplusgx_recording_workflow_test recording_workflow_test.cpp)
target_link_libraries(
  genplusgx_recording_workflow_test
  PRIVATE
    GenesisPlusGX::Capture
    GenesisPlusGX::CoreAdapter
    genplusgx_test_utilities
    genplusgx_project_warnings
    Qt6::Core
    Qt6::Gui
)
add_test(NAME integration.recording COMMAND genplusgx_recording_workflow_test)
set_tests_properties(
  integration.recording
  PROPERTIES
    LABELS "core;integration;recording;video;audio;fixture;concurrency;lifecycle"
    TIMEOUT 30
)

add_executable(genplusgx_timing_pacing_test timing_pacing_test.cpp)
target_link_libraries(
  genplusgx_timing_pacing_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    genplusgx_test_utilities
    genplusgx_project_warnings
)
add_test(NAME core.timing_pacing COMMAND genplusgx_timing_pacing_test)
set_tests_properties(
  core.timing_pacing
  PROPERTIES LABELS "core;integration;fixture;concurrency;timing" TIMEOUT 30
)

add_executable(
  genplusgx_long_running_stability_test long_running_stability_test.cpp
)
target_link_libraries(
  genplusgx_long_running_stability_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    genplusgx_test_utilities
    genplusgx_project_warnings
)
add_test(
  NAME core.long_running_stability
  COMMAND genplusgx_long_running_stability_test
)
set_tests_properties(
  core.long_running_stability
  PROPERTIES
    LABELS "core;integration;fixture;concurrency;lifecycle;timing;audio;video;stress;sanitizer"
    TIMEOUT 90
)
