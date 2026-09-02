find_package(Threads REQUIRED)

add_executable(genplusgx_cloud_sync_test cloud_sync_test.cpp)
target_link_libraries(
  genplusgx_cloud_sync_test
  PRIVATE GenesisPlusGX::Cloud genplusgx_project_warnings Qt6::Core
)
add_test(NAME unit.cloud_sync COMMAND genplusgx_cloud_sync_test)
set_tests_properties(
  unit.cloud_sync
  PROPERTIES LABELS "unit;integration;cloud;network;security;persistence;filesystem;fuzz;bounded"
    TIMEOUT 30
)

qt_add_executable(genplusgx_cloud_webdav_test cloud_webdav_test.cpp)
set_target_properties(genplusgx_cloud_webdav_test PROPERTIES AUTOMOC ON)
target_link_libraries(
  genplusgx_cloud_webdav_test
  PRIVATE GenesisPlusGX::Cloud genplusgx_project_warnings Qt6::Network Qt6::Test
)
add_test(NAME integration.cloud_webdav COMMAND genplusgx_cloud_webdav_test)
set_tests_properties(
  integration.cloud_webdav
  PROPERTIES
    ENVIRONMENT "GENPLUSGX_TEST_FIXTURES=${PROJECT_SOURCE_DIR}/tests/fixtures"
    LABELS "integration;cloud;network;tls;security;bounded;fixture"
    TIMEOUT 30
)
if(APPLE)
  # Secure Transport imports explicitly configured CAs into a keychain. Keep the
  # loopback fixture isolated from the user's or CI runner's login keychain.
  set_property(
    TEST integration.cloud_webdav
    APPEND PROPERTY ENVIRONMENT "QT_SSL_USE_TEMPORARY_KEYCHAIN=1"
  )
endif()

if(GENPLUSGX_ENABLE_ACHIEVEMENTS)
  add_executable(genplusgx_achievements_test achievements_test.cpp)
  target_link_libraries(
    genplusgx_achievements_test
    PRIVATE
      GenesisPlusGX::Achievements
      genplusgx_project_warnings
      Qt6::Core
  )
  add_test(NAME unit.achievements COMMAND genplusgx_achievements_test)
  set_tests_properties(
    unit.achievements
    PROPERTIES LABELS "unit;integration;achievements;network;security;persistence;bounded"
      TIMEOUT 30
  )
endif()

add_executable(genplusgx_netplay_timeline_test netplay_timeline_test.cpp)
target_link_libraries(
  genplusgx_netplay_timeline_test
  PRIVATE GenesisPlusGX::NetplayCore genplusgx_project_warnings
)
add_test(NAME unit.netplay_timeline COMMAND genplusgx_netplay_timeline_test)
set_tests_properties(
  unit.netplay_timeline
  PROPERTIES LABELS "unit;netplay;input;rollback;bounded" TIMEOUT 30
)

add_executable(genplusgx_netplay_protocol_test netplay_protocol_test.cpp)
target_link_libraries(
  genplusgx_netplay_protocol_test
  PRIVATE GenesisPlusGX::Netplay genplusgx_project_warnings Qt6::Core
)

qt_add_executable(genplusgx_netplay_session_test netplay_session_test.cpp)
set_target_properties(genplusgx_netplay_session_test PROPERTIES AUTOMOC ON)
target_link_libraries(
  genplusgx_netplay_session_test
  PRIVATE GenesisPlusGX::Netplay genplusgx_project_warnings Qt6::Test
)
add_test(NAME integration.netplay_transport COMMAND genplusgx_netplay_session_test)
set_tests_properties(
  integration.netplay_transport
  PROPERTIES LABELS "unit;integration;netplay;network;security;loopback" TIMEOUT 30
)
add_test(NAME unit.netplay_protocol COMMAND genplusgx_netplay_protocol_test)
set_tests_properties(
  unit.netplay_protocol
  PROPERTIES LABELS "unit;netplay;protocol;security;fuzz;bounded" TIMEOUT 30
)

add_executable(genplusgx_localization_test localization_test.cpp)
target_link_libraries(
  genplusgx_localization_test
  PRIVATE GenesisPlusGX::Localization genplusgx_project_warnings Qt6::Core
)
add_dependencies(genplusgx_localization_test genplusgx_release_translations)
add_test(
  NAME unit.localization
  COMMAND genplusgx_localization_test "${genplusgx_test_pseudo_qm}"
)
set_tests_properties(
  unit.localization
  PROPERTIES LABELS "unit;localization;settings;fallback;filesystem"
)

add_executable(
  genplusgx_translation_catalog_test translation_catalog_test.cpp
)
target_link_libraries(
  genplusgx_translation_catalog_test
  PRIVATE genplusgx_project_warnings Qt6::Core
)
add_dependencies(
  genplusgx_translation_catalog_test genplusgx_release_translations
)
add_test(
  NAME unit.translation_catalog
  COMMAND genplusgx_translation_catalog_test
    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/translations/genplusgx_en_XA.ts"
    "$<TARGET_FILE:Qt6::lupdate>"
    "${genplusgx_test_pseudo_qm}"
)
set_tests_properties(
  unit.translation_catalog
  PROPERTIES LABELS "unit;localization;catalog;extraction;placeholders"
)

add_executable(genplusgx_presentation_test presentation_test.cpp)
target_link_libraries(
  genplusgx_presentation_test
  PRIVATE GenesisPlusGX::VideoExchange genplusgx_project_warnings
)
add_test(NAME unit.presentation COMMAND genplusgx_presentation_test)
set_tests_properties(
  unit.presentation
  PROPERTIES LABELS "unit;video;presentation;timing;bounded" TIMEOUT 30
)

add_executable(genplusgx_game_patch_test game_patch_test.cpp)
target_link_libraries(
  genplusgx_game_patch_test
  PRIVATE GenesisPlusGX::GameFiles genplusgx_project_warnings Qt6::Core
)
add_test(NAME unit.game_patch COMMAND genplusgx_game_patch_test)
set_tests_properties(
  unit.game_patch
  PROPERTIES LABELS "unit;patch;parser;filesystem;security;fuzz" TIMEOUT 30
)

add_executable(genplusgx_rewind_buffer_test rewind_buffer_test.cpp)
target_link_libraries(
  genplusgx_rewind_buffer_test
  PRIVATE GenesisPlusGX::CoreAdapter genplusgx_project_warnings
)
add_test(NAME unit.rewind_buffer COMMAND genplusgx_rewind_buffer_test)
set_tests_properties(
  unit.rewind_buffer PROPERTIES LABELS "unit;rewind;state;bounded" TIMEOUT 30
)

add_executable(genplusgx_rewind_settings_test rewind_settings_test.cpp)
target_link_libraries(
  genplusgx_rewind_settings_test
  PRIVATE GenesisPlusGX::Settings genplusgx_project_warnings Qt6::Core
)

add_executable(genplusgx_session_settings_test session_settings_test.cpp)
target_link_libraries(
  genplusgx_session_settings_test
  PRIVATE GenesisPlusGX::Settings genplusgx_project_warnings Qt6::Core
)
add_test(NAME unit.session_settings COMMAND genplusgx_session_settings_test)
set_tests_properties(
  unit.session_settings
  PROPERTIES LABELS "unit;session;settings;persistence;filesystem" TIMEOUT 30
)
add_test(NAME unit.rewind_settings COMMAND genplusgx_rewind_settings_test)
set_tests_properties(
  unit.rewind_settings
  PROPERTIES LABELS "unit;rewind;settings;persistence;filesystem" TIMEOUT 30
)

add_executable(genplusgx_run_ahead_settings_test run_ahead_settings_test.cpp)
target_link_libraries(
  genplusgx_run_ahead_settings_test
  PRIVATE GenesisPlusGX::Settings genplusgx_project_warnings Qt6::Core
)
add_test(NAME unit.run_ahead_settings COMMAND genplusgx_run_ahead_settings_test)
set_tests_properties(
  unit.run_ahead_settings
  PROPERTIES LABELS "unit;run-ahead;settings;persistence;filesystem;security" TIMEOUT 30
)

add_executable(genplusgx_speed_settings_test speed_settings_test.cpp)
target_link_libraries(
  genplusgx_speed_settings_test
  PRIVATE GenesisPlusGX::Settings genplusgx_project_warnings Qt6::Core
)
add_test(NAME unit.speed_settings COMMAND genplusgx_speed_settings_test)
set_tests_properties(
  unit.speed_settings
  PROPERTIES LABELS "unit;timing;settings;persistence;filesystem;security" TIMEOUT 30
)

add_executable(genplusgx_debug_analysis_test debug_analysis_test.cpp)
target_link_libraries(
  genplusgx_debug_analysis_test
  PRIVATE GenesisPlusGX::CoreAdapter genplusgx_project_warnings
)
add_test(NAME unit.debug_analysis COMMAND genplusgx_debug_analysis_test)
set_tests_properties(
  unit.debug_analysis
  PROPERTIES LABELS "unit;debug;parser;fuzz" TIMEOUT 30
)

if(GENPLUSGX_ENABLE_LIBRETRO_SHADERS)
  add_executable(
    genplusgx_shader_configuration_test shader_configuration_test.cpp
  )
  target_link_libraries(
    genplusgx_shader_configuration_test
    PRIVATE GenesisPlusGX::VideoWidget genplusgx_project_warnings Qt6::Core
  )
  add_test(
    NAME unit.shader_configuration COMMAND genplusgx_shader_configuration_test
  )
  set_tests_properties(
    unit.shader_configuration
    PROPERTIES LABELS "unit;video;shader;parser;filesystem" TIMEOUT 30
  )
endif()

add_executable(genplusgx_diagnostics_test diagnostics_test.cpp)
target_link_libraries(
  genplusgx_diagnostics_test
  PRIVATE GenesisPlusGX::Diagnostics genplusgx_project_warnings Qt6::Core
)
add_test(NAME unit.diagnostics COMMAND genplusgx_diagnostics_test)
set_tests_properties(
  unit.diagnostics
  PROPERTIES LABELS "unit;diagnostics;logging;filesystem;privacy" TIMEOUT 30
)

add_executable(genplusgx_appearance_settings_test appearance_settings_test.cpp)
target_link_libraries(
  genplusgx_appearance_settings_test
  PRIVATE GenesisPlusGX::Settings genplusgx_project_warnings Qt6::Core
)
add_test(NAME unit.appearance_settings COMMAND genplusgx_appearance_settings_test)
set_tests_properties(
  unit.appearance_settings
  PROPERTIES LABELS "unit;settings;persistence;filesystem;accessibility" TIMEOUT 30
)

add_executable(genplusgx_cheat_manager_test cheat_manager_test.cpp)
target_link_libraries(
  genplusgx_cheat_manager_test
  PRIVATE GenesisPlusGX::Cheats genplusgx_project_warnings Qt6::Core
)

add_executable(genplusgx_per_game_settings_test per_game_settings_test.cpp)
target_link_libraries(
  genplusgx_per_game_settings_test
  PRIVATE GenesisPlusGX::Settings genplusgx_project_warnings Qt6::Core
)
add_test(NAME unit.per_game_settings COMMAND genplusgx_per_game_settings_test)
set_tests_properties(
  unit.per_game_settings
  PROPERTIES LABELS "unit;settings;persistence;filesystem" TIMEOUT 30
)
add_test(NAME unit.cheats COMMAND genplusgx_cheat_manager_test)
set_tests_properties(
  unit.cheats PROPERTIES LABELS "unit;cheats;persistence;parser;fuzz" TIMEOUT 30
)

add_executable(genplusgx_game_file_test game_file_test.cpp)
target_link_libraries(
  genplusgx_game_file_test
  PRIVATE
    GenesisPlusGX::GameFiles
    genplusgx_project_warnings
    genplusgx_test_utilities
)
add_test(NAME unit.game_file COMMAND genplusgx_game_file_test)
set_tests_properties(
  unit.game_file
  PROPERTIES LABELS "unit;filesystem;loading;disc;parser;fuzz;security" TIMEOUT 30
)

add_executable(genplusgx_game_metadata_test game_metadata_test.cpp)
target_link_libraries(
  genplusgx_game_metadata_test
  PRIVATE
    GenesisPlusGX::Library
    GenesisPlusGX::Persistence
    genplusgx_project_warnings
    genplusgx_test_utilities
    Qt6::Core
    Threads::Threads
)
add_test(NAME unit.game_metadata COMMAND genplusgx_game_metadata_test)
set_tests_properties(
  unit.game_metadata
  PROPERTIES LABELS "unit;library;metadata;filesystem;fuzz;concurrency" TIMEOUT 30
)

add_executable(
  genplusgx_game_library_database_test game_library_database_test.cpp
)
target_link_libraries(
  genplusgx_game_library_database_test
  PRIVATE
    GenesisPlusGX::Library
    genplusgx_project_warnings
    genplusgx_test_utilities
    Qt6::Core
    Qt6::Sql
    Threads::Threads
)
add_test(
  NAME unit.game_library_database
  COMMAND genplusgx_game_library_database_test
)
set_tests_properties(
  unit.game_library_database
  PROPERTIES LABELS "unit;library;database;filesystem;persistence" TIMEOUT 30
)

add_executable(
  genplusgx_game_library_scanner_test game_library_scanner_test.cpp
)
target_link_libraries(
  genplusgx_game_library_scanner_test
  PRIVATE
    GenesisPlusGX::Library
    genplusgx_project_warnings
    genplusgx_test_utilities
    Qt6::Core
    Threads::Threads
)
add_test(
  NAME unit.game_library_scanner
  COMMAND genplusgx_game_library_scanner_test
)
set_tests_properties(
  unit.game_library_scanner
  PROPERTIES LABELS "unit;library;database;filesystem;concurrency;fixture" TIMEOUT 30
)

if(TARGET genplusgx_app_support)
  add_executable(genplusgx_command_line_test command_line_test.cpp)
  target_link_libraries(
    genplusgx_command_line_test
    PRIVATE GenesisPlusGX::AppSupport genplusgx_project_warnings Qt6::Core
  )
  add_test(NAME unit.command_line COMMAND genplusgx_command_line_test)
  set_tests_properties(unit.command_line PROPERTIES LABELS "unit;cli;loading")

  add_executable(genplusgx_shutdown_report_test shutdown_report_test.cpp)
  target_link_libraries(
    genplusgx_shutdown_report_test
    PRIVATE GenesisPlusGX::AppSupport genplusgx_project_warnings
  )
  add_test(NAME unit.shutdown_report COMMAND genplusgx_shutdown_report_test)
  set_tests_properties(
    unit.shutdown_report
    PROPERTIES LABELS "unit;lifecycle;shutdown;error-handling"
  )
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
    Qt6::Gui
)

add_test(NAME unit.state_manager COMMAND genplusgx_state_manager_test)
set_tests_properties(
  unit.state_manager PROPERTIES LABELS "unit;persistence;state" TIMEOUT 30
)

add_executable(
  genplusgx_state_storage_service_test state_storage_service_test.cpp
)
target_link_libraries(
  genplusgx_state_storage_service_test
  PRIVATE
    GenesisPlusGX::Persistence
    genplusgx_project_warnings
    genplusgx_test_utilities
    Qt6::Core
)
add_test(
  NAME unit.state_storage_service COMMAND genplusgx_state_storage_service_test
)
set_tests_properties(
  unit.state_storage_service
  PROPERTIES LABELS "unit;persistence;state;concurrency" TIMEOUT 30
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

add_executable(genplusgx_artwork_test artwork_test.cpp)
target_link_libraries(
  genplusgx_artwork_test
  PRIVATE
    GenesisPlusGX::VideoWidget
    genplusgx_project_options
    genplusgx_project_warnings
    Qt6::Core
    Qt6::Gui
)
add_test(NAME unit.artwork COMMAND genplusgx_artwork_test)
set_tests_properties(
  unit.artwork PROPERTIES LABELS "unit;video;artwork;security" TIMEOUT 30
)

add_executable(genplusgx_video_settings_test video_settings_test.cpp)
target_link_libraries(
  genplusgx_video_settings_test
  PRIVATE
    GenesisPlusGX::Settings
    genplusgx_project_warnings
    Qt6::Core
)
add_test(NAME unit.video_settings COMMAND genplusgx_video_settings_test)
set_tests_properties(
  unit.video_settings PROPERTIES LABELS "unit;video;settings;persistence" TIMEOUT 30
)

add_executable(genplusgx_audio_settings_test audio_settings_test.cpp)
target_link_libraries(
  genplusgx_audio_settings_test
  PRIVATE GenesisPlusGX::Settings genplusgx_project_warnings Qt6::Core
)
add_test(NAME unit.audio_settings COMMAND genplusgx_audio_settings_test)
set_tests_properties(
  unit.audio_settings PROPERTIES LABELS "unit;audio;settings;persistence" TIMEOUT 30
)

add_executable(genplusgx_system_settings_test system_settings_test.cpp)
target_link_libraries(
  genplusgx_system_settings_test
  PRIVATE GenesisPlusGX::Settings genplusgx_project_warnings Qt6::Core
)
add_test(NAME unit.system_settings COMMAND genplusgx_system_settings_test)
set_tests_properties(
  unit.system_settings PROPERTIES LABELS "unit;system;settings;persistence" TIMEOUT 30
)

add_executable(genplusgx_screenshot_service_test screenshot_service_test.cpp)
target_link_libraries(
  genplusgx_screenshot_service_test
  PRIVATE
    GenesisPlusGX::Screenshots
    GenesisPlusGX::Settings
    genplusgx_project_warnings
    Qt6::Core
    Qt6::Gui
    Threads::Threads
)
add_test(NAME unit.screenshot_service COMMAND genplusgx_screenshot_service_test)
set_tests_properties(
  unit.screenshot_service
  PROPERTIES
    LABELS "unit;screenshot;filesystem;persistence;settings;concurrency"
    TIMEOUT 30
)

add_executable(genplusgx_recording_service_test recording_service_test.cpp)
target_link_libraries(
  genplusgx_recording_service_test
  PRIVATE
    GenesisPlusGX::Capture
    genplusgx_project_warnings
    Qt6::Core
    Qt6::Gui
    Threads::Threads
)
add_test(NAME unit.recording_service COMMAND genplusgx_recording_service_test)
set_tests_properties(
  unit.recording_service
  PROPERTIES
    LABELS "unit;recording;video;audio;filesystem;concurrency;bounded"
    TIMEOUT 30
)

add_executable(genplusgx_bios_manager_test bios_manager_test.cpp)
target_link_libraries(
  genplusgx_bios_manager_test
  PRIVATE GenesisPlusGX::Platform genplusgx_project_warnings Qt6::Core
)
add_test(NAME unit.bios_manager COMMAND genplusgx_bios_manager_test)
set_tests_properties(
  unit.bios_manager PROPERTIES LABELS "unit;bios;platform;persistence" TIMEOUT 30
)

add_executable(genplusgx_physical_media_test physical_media_test.cpp)
target_link_libraries(
  genplusgx_physical_media_test
  PRIVATE
    GenesisPlusGX::GameFiles
    GenesisPlusGX::Platform
    genplusgx_project_warnings
    genplusgx_test_utilities
    Qt6::Core
)
add_test(NAME unit.physical_media COMMAND genplusgx_physical_media_test)
set_tests_properties(
  unit.physical_media
  PROPERTIES
    LABELS "unit;platform;physical-media;sega-cd;filesystem;security;concurrency;bounded;fixture"
    TIMEOUT 30
)

add_executable(genplusgx_frame_pacer_test frame_pacer_test.cpp)
target_link_libraries(
  genplusgx_frame_pacer_test
  PRIVATE GenesisPlusGX::Timing genplusgx_project_warnings
)
add_test(NAME unit.frame_pacer COMMAND genplusgx_frame_pacer_test)
set_tests_properties(unit.frame_pacer PROPERTIES LABELS "unit;timing")
