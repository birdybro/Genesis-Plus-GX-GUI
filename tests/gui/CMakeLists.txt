qt_add_executable(genplusgx_main_window_test
  main_window_test.cpp
)

qt_add_executable(genplusgx_achievements_dialog_test
  achievements_dialog_test.cpp
)
set_target_properties(genplusgx_achievements_dialog_test PROPERTIES AUTOMOC ON)
target_link_libraries(
  genplusgx_achievements_dialog_test
  PRIVATE genplusgx::ui genplusgx_project_warnings Qt6::Test Qt6::Widgets
)
add_test(NAME gui.achievements COMMAND genplusgx_achievements_dialog_test)
set_tests_properties(
  gui.achievements
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;GENPLUSGX_FORCE_SOFTWARE_VIDEO=1"
    LABELS "gui;unit;integration;achievements;security;accessibility"
    TIMEOUT 30
)

qt_add_executable(genplusgx_netplay_dialog_test netplay_dialog_test.cpp)
set_target_properties(genplusgx_netplay_dialog_test PROPERTIES AUTOMOC ON)
target_link_libraries(
  genplusgx_netplay_dialog_test
  PRIVATE genplusgx::ui genplusgx_project_warnings Qt6::Test Qt6::Widgets
)
add_test(NAME gui.netplay COMMAND genplusgx_netplay_dialog_test)
set_tests_properties(
  gui.netplay
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;GENPLUSGX_FORCE_SOFTWARE_VIDEO=1"
    LABELS "gui;unit;integration;netplay;security;accessibility"
    TIMEOUT 30
)

qt_add_executable(genplusgx_localization_gui_test localization_test.cpp)
set_target_properties(genplusgx_localization_gui_test PROPERTIES AUTOMOC ON)
target_link_libraries(
  genplusgx_localization_gui_test
  PRIVATE
    genplusgx::ui
    genplusgx_project_warnings
    Qt6::Test
    Qt6::Widgets
)
add_dependencies(
  genplusgx_localization_gui_test genplusgx_release_translations
)
add_test(NAME gui.localization COMMAND genplusgx_localization_gui_test)
set_tests_properties(
  gui.localization
  PROPERTIES
    ENVIRONMENT
      "QT_QPA_PLATFORM=offscreen;GENPLUSGX_TEST_PSEUDO_QM=${genplusgx_test_pseudo_qm}"
    LABELS "gui;unit;localization;fallback;layout;accessibility"
    TIMEOUT 30
)

qt_add_executable(genplusgx_debug_tools_window_test debug_tools_window_test.cpp)
set_target_properties(genplusgx_debug_tools_window_test PROPERTIES AUTOMOC ON)
target_link_libraries(
  genplusgx_debug_tools_window_test
  PRIVATE
    genplusgx::ui
    genplusgx_project_warnings
    Qt6::Test
    Qt6::Widgets
)
add_test(NAME gui.debug_tools COMMAND genplusgx_debug_tools_window_test)
set_tests_properties(
  gui.debug_tools
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "gui;unit;integration;debug;accessibility"
    TIMEOUT 30
)

qt_add_executable(genplusgx_physical_media_gui_test physical_media_test.cpp)
set_target_properties(genplusgx_physical_media_gui_test PROPERTIES AUTOMOC ON)
target_link_libraries(
  genplusgx_physical_media_gui_test
  PRIVATE
    genplusgx::ui
    genplusgx_project_warnings
    genplusgx_test_utilities
    Qt6::Test
    Qt6::Widgets
)
add_test(NAME gui.physical_media COMMAND genplusgx_physical_media_gui_test)
set_tests_properties(
  gui.physical_media
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;GENPLUSGX_FORCE_SOFTWARE_VIDEO=1"
    LABELS "gui;integration;platform;physical-media;sega-cd;filesystem;accessibility;fixture"
    TIMEOUT 30
)

qt_add_executable(genplusgx_debug_tools_live_test debug_tools_live_test.cpp)
set_target_properties(genplusgx_debug_tools_live_test PROPERTIES AUTOMOC ON)
target_link_libraries(
  genplusgx_debug_tools_live_test
  PRIVATE
    genplusgx::ui
    GenesisPlusGX::CoreAdapter
    genplusgx_project_warnings
    genplusgx_test_utilities
    Qt6::Test
    Qt6::Widgets
)
add_test(NAME gui.debug_tools_live COMMAND genplusgx_debug_tools_live_test)
set_tests_properties(
  gui.debug_tools_live
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;GENPLUSGX_FORCE_SOFTWARE_VIDEO=1"
    LABELS "gui;integration;debug;fixture;concurrency;lifecycle"
    TIMEOUT 30
)

qt_add_executable(genplusgx_emulation_controls_test emulation_controls_test.cpp)
set_target_properties(genplusgx_emulation_controls_test PROPERTIES AUTOMOC ON)
target_link_libraries(
  genplusgx_emulation_controls_test
  PRIVATE
    genplusgx::ui
    GenesisPlusGX::CoreAdapter
    genplusgx_project_warnings
    genplusgx_test_utilities
    Qt6::Test
    Qt6::Widgets
)
qt_add_executable(genplusgx_navigation_regression_test navigation_regression_test.cpp)
set_target_properties(genplusgx_navigation_regression_test PROPERTIES AUTOMOC ON)
target_link_libraries(
  genplusgx_navigation_regression_test
  PRIVATE
    genplusgx::ui
    genplusgx_project_warnings
    Qt6::Test
    Qt6::Widgets
)
add_test(NAME gui.navigation_regression COMMAND genplusgx_navigation_regression_test)
set_tests_properties(
  gui.navigation_regression
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "gui;unit;regression;navigation;accessibility;hotkeys"
    TIMEOUT 30
)

qt_add_executable(genplusgx_settings_dialog_test settings_dialog_test.cpp)
set_target_properties(genplusgx_settings_dialog_test PROPERTIES AUTOMOC ON)
target_link_libraries(
  genplusgx_settings_dialog_test
  PRIVATE
    genplusgx::ui
    genplusgx_project_warnings
    Qt6::Test
    Qt6::Widgets
)

qt_add_executable(
  genplusgx_rewind_settings_dialog_test rewind_settings_dialog_test.cpp
)
set_target_properties(
  genplusgx_rewind_settings_dialog_test PROPERTIES AUTOMOC ON
)
target_link_libraries(
  genplusgx_rewind_settings_dialog_test
  PRIVATE
    genplusgx::ui
    genplusgx_project_warnings
    Qt6::Test
    Qt6::Widgets
)
add_test(
  NAME gui.rewind_settings COMMAND genplusgx_rewind_settings_dialog_test
)
set_tests_properties(
  gui.rewind_settings
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "gui;unit;integration;rewind;settings;persistence;accessibility"
    TIMEOUT 30
)

qt_add_executable(
  genplusgx_run_ahead_settings_dialog_test run_ahead_settings_dialog_test.cpp
)
set_target_properties(
  genplusgx_run_ahead_settings_dialog_test PROPERTIES AUTOMOC ON
)
target_link_libraries(
  genplusgx_run_ahead_settings_dialog_test
  PRIVATE
    genplusgx::ui
    genplusgx_project_warnings
    Qt6::Test
    Qt6::Widgets
)
add_test(
  NAME gui.run_ahead_settings COMMAND genplusgx_run_ahead_settings_dialog_test
)
set_tests_properties(
  gui.run_ahead_settings
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "gui;unit;integration;run-ahead;settings;persistence;accessibility"
    TIMEOUT 30
)
add_test(NAME gui.settings COMMAND genplusgx_settings_dialog_test)
set_tests_properties(
  gui.settings
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "gui;unit;settings;accessibility;filesystem"
    TIMEOUT 30
)

qt_add_executable(
  genplusgx_speed_settings_dialog_test speed_settings_dialog_test.cpp
)
set_target_properties(
  genplusgx_speed_settings_dialog_test PROPERTIES AUTOMOC ON
)
target_link_libraries(
  genplusgx_speed_settings_dialog_test
  PRIVATE
    genplusgx::ui
    genplusgx_project_warnings
    Qt6::Test
    Qt6::Widgets
)
add_test(
  NAME gui.speed_settings COMMAND genplusgx_speed_settings_dialog_test
)
set_tests_properties(
  gui.speed_settings
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "gui;unit;integration;timing;settings;persistence;accessibility"
    TIMEOUT 30
)

qt_add_executable(
  genplusgx_session_settings_dialog_test session_settings_dialog_test.cpp
)
set_target_properties(
  genplusgx_session_settings_dialog_test PROPERTIES AUTOMOC ON
)
target_link_libraries(
  genplusgx_session_settings_dialog_test
  PRIVATE
    genplusgx::ui
    genplusgx_project_warnings
    Qt6::Test
    Qt6::Widgets
)
add_test(
  NAME gui.session_settings COMMAND genplusgx_session_settings_dialog_test
)
set_tests_properties(
  gui.session_settings
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "gui;unit;session;settings;persistence;accessibility"
    TIMEOUT 30
)

add_executable(
  genplusgx_session_resume_application_test session_resume_application_test.cpp
)
target_link_libraries(
  genplusgx_session_resume_application_test
  PRIVATE
    GenesisPlusGX::Settings
    genplusgx_project_warnings
    genplusgx_test_utilities
    Qt6::Core
)
add_dependencies(genplusgx_session_resume_application_test genplusgx_desktop)
add_test(
  NAME gui.session_resume_application
  COMMAND genplusgx_session_resume_application_test $<TARGET_FILE:genplusgx_desktop>
)
set_tests_properties(
  gui.session_resume_application
  PROPERTIES
    LABELS "gui;integration;session;state;lifecycle;fixture;process;patch;cli"
    TIMEOUT 75
)
add_test(NAME gui.emulation_controls COMMAND genplusgx_emulation_controls_test)
set_tests_properties(
  gui.emulation_controls
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;GENPLUSGX_FORCE_SOFTWARE_VIDEO=1"
    LABELS "gui;integration;emulation;fixture;concurrency;timing"
    TIMEOUT 30
)

qt_add_executable(genplusgx_diagnostics_dialog_test diagnostics_dialog_test.cpp)
set_target_properties(genplusgx_diagnostics_dialog_test PROPERTIES AUTOMOC ON)
target_link_libraries(
  genplusgx_diagnostics_dialog_test
  PRIVATE
    genplusgx::ui
    genplusgx_project_warnings
    Qt6::Test
    Qt6::Widgets
)
add_test(NAME gui.diagnostics COMMAND genplusgx_diagnostics_dialog_test)
set_tests_properties(
  gui.diagnostics
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "gui;unit;diagnostics;logging;privacy;accessibility"
    TIMEOUT 30
)

qt_add_executable(
  genplusgx_appearance_accessibility_test appearance_accessibility_test.cpp
)
set_target_properties(
  genplusgx_appearance_accessibility_test PROPERTIES AUTOMOC ON
)
target_link_libraries(
  genplusgx_appearance_accessibility_test
  PRIVATE
    genplusgx::ui
    genplusgx_project_warnings
    Qt6::Test
    Qt6::Widgets
)
add_test(
  NAME gui.appearance_accessibility
  COMMAND genplusgx_appearance_accessibility_test
)
set_tests_properties(
  gui.appearance_accessibility
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "gui;unit;settings;persistence;accessibility;high-dpi"
    TIMEOUT 30
)

qt_add_executable(
  genplusgx_cheat_manager_dialog_test cheat_manager_dialog_test.cpp
)
set_target_properties(genplusgx_cheat_manager_dialog_test PROPERTIES AUTOMOC ON)
target_link_libraries(
  genplusgx_cheat_manager_dialog_test
  PRIVATE
    genplusgx::ui
    genplusgx_project_warnings
    Qt6::Test
    Qt6::Widgets
)

qt_add_executable(
  genplusgx_per_game_settings_dialog_test per_game_settings_dialog_test.cpp
)
set_target_properties(
  genplusgx_per_game_settings_dialog_test PROPERTIES AUTOMOC ON
)
target_link_libraries(
  genplusgx_per_game_settings_dialog_test
  PRIVATE
    genplusgx::ui
    genplusgx_project_warnings
    Qt6::Test
    Qt6::Widgets
)
add_test(
  NAME gui.per_game_settings COMMAND genplusgx_per_game_settings_dialog_test
)
set_tests_properties(
  gui.per_game_settings
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "gui;unit;integration;settings;persistence"
    TIMEOUT 30
)
add_test(NAME gui.cheats COMMAND genplusgx_cheat_manager_dialog_test)
set_tests_properties(
  gui.cheats
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "gui;unit;integration;cheats;persistence"
    TIMEOUT 30
)

set_target_properties(genplusgx_main_window_test PROPERTIES AUTOMOC ON)

target_link_libraries(
  genplusgx_main_window_test
  PRIVATE
    genplusgx::ui
    genplusgx_test_utilities
    genplusgx_project_options
    genplusgx_project_warnings
    Qt6::Test
    Qt6::Widgets
)

add_test(NAME gui.main_window COMMAND genplusgx_main_window_test)
set_tests_properties(
  gui.main_window
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "gui;unit"
)

qt_add_executable(
  genplusgx_game_library_dialog_test game_library_dialog_test.cpp
)
set_target_properties(genplusgx_game_library_dialog_test PROPERTIES AUTOMOC ON)
target_link_libraries(
  genplusgx_game_library_dialog_test
  PRIVATE
    genplusgx::ui
    genplusgx_test_utilities
    genplusgx_project_warnings
    Qt6::Test
    Qt6::Widgets
)
add_test(NAME gui.game_library COMMAND genplusgx_game_library_dialog_test)
set_tests_properties(
  gui.game_library
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "gui;unit;integration;library;database;filesystem"
    TIMEOUT 30
)

qt_add_executable(genplusgx_game_loading_test game_loading_test.cpp)
set_target_properties(genplusgx_game_loading_test PROPERTIES AUTOMOC ON)
target_link_libraries(
  genplusgx_game_loading_test
  PRIVATE
    genplusgx::ui
    GenesisPlusGX::CoreAdapter
    genplusgx_project_warnings
    genplusgx_test_utilities
    Qt6::Test
    Qt6::Widgets
)
add_test(NAME gui.game_loading COMMAND genplusgx_game_loading_test)
set_tests_properties(
  gui.game_loading
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;GENPLUSGX_FORCE_SOFTWARE_VIDEO=1"
    LABELS "gui;integration;loading;fixture"
    TIMEOUT 30
)

qt_add_executable(
  genplusgx_save_state_workflow_test save_state_workflow_test.cpp
)
set_target_properties(
  genplusgx_save_state_workflow_test PROPERTIES AUTOMOC ON
)
target_link_libraries(
  genplusgx_save_state_workflow_test
  PRIVATE
    genplusgx::ui
    GenesisPlusGX::CoreAdapter
    genplusgx_project_warnings
    genplusgx_test_utilities
    Qt6::Test
    Qt6::Widgets
)
add_test(NAME gui.save_state_workflow COMMAND genplusgx_save_state_workflow_test)
set_tests_properties(
  gui.save_state_workflow
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;GENPLUSGX_FORCE_SOFTWARE_VIDEO=1"
    LABELS "gui;integration;state;persistence;fixture;concurrency"
    TIMEOUT 30
)

qt_add_executable(genplusgx_display_widget_test display_widget_test.cpp)
set_target_properties(genplusgx_display_widget_test PROPERTIES AUTOMOC ON)
target_link_libraries(
  genplusgx_display_widget_test
  PRIVATE
    GenesisPlusGX::VideoWidget
    genplusgx_project_options
    genplusgx_project_warnings
    Qt6::Test
)
add_test(NAME gui.display_widget COMMAND genplusgx_display_widget_test)
set_tests_properties(
  gui.display_widget
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "gui;integration;video;artwork"
)

if(GENPLUSGX_ENABLE_LIBRETRO_SHADERS)
  add_executable(
    genplusgx_libretro_shader_render_test libretro_shader_render_test.cpp
  )
  target_link_libraries(
    genplusgx_libretro_shader_render_test
    PRIVATE
      GenesisPlusGX::VideoWidget
      genplusgx_project_options
      genplusgx_project_warnings
      Qt6::Gui
      Qt6::Test
      Qt6::Widgets
  )
  target_compile_definitions(
    genplusgx_libretro_shader_render_test
    PRIVATE
      GENPLUSGX_SHADER_TEST_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
  )
  add_executable(
    genplusgx_external_rom_acceptance_test external_rom_acceptance_test.cpp
  )
  target_link_libraries(
    genplusgx_external_rom_acceptance_test
    PRIVATE
      GenesisPlusGX::CoreAdapter
      GenesisPlusGX::GameFiles
      GenesisPlusGX::VideoWidget
      genplusgx_project_options
      genplusgx_project_warnings
      Qt6::Gui
      Qt6::Test
      Qt6::Widgets
  )
  target_compile_definitions(
    genplusgx_external_rom_acceptance_test
    PRIVATE
      GENPLUSGX_SHADER_TEST_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
  )
  find_program(GENPLUSGX_XVFB_RUN_EXECUTABLE xvfb-run)
  if(UNIX AND NOT APPLE AND GENPLUSGX_XVFB_RUN_EXECUTABLE)
    add_test(
      NAME gui.libretro_shader_render
      COMMAND "${GENPLUSGX_XVFB_RUN_EXECUTABLE}" -a
        -s "-screen 0 1024x768x24 +extension GLX"
        $<TARGET_FILE:genplusgx_libretro_shader_render_test>
    )
    set(genplusgx_shader_test_environment
      "QT_QPA_PLATFORM=xcb;GENPLUSGX_REQUIRE_OPENGL_SHADER_TEST=1")
  else()
    add_test(
      NAME gui.libretro_shader_render
      COMMAND genplusgx_libretro_shader_render_test
    )
  endif()
  set_tests_properties(
    gui.libretro_shader_render
    PROPERTIES
      SKIP_RETURN_CODE 77
      LABELS "gui;integration;video;shader;artwork;opengl"
      # The complete 108-case matrix runs through host software OpenGL on CI.
      # Keep every case while allowing for shared-runner rendering variance.
      TIMEOUT 90
  )
  if(WIN32)
    set_tests_properties(
      gui.libretro_shader_render PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows"
    )
  elseif(APPLE)
    set_tests_properties(
      gui.libretro_shader_render PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=cocoa"
    )
  else()
    if(NOT GENPLUSGX_XVFB_RUN_EXECUTABLE)
      set(genplusgx_shader_test_environment "QT_QPA_PLATFORM=xcb")
    endif()
  endif()
  if(genplusgx_shader_test_environment)
    set_tests_properties(
      gui.libretro_shader_render
      PROPERTIES ENVIRONMENT "${genplusgx_shader_test_environment}"
    )
  endif()
  if(GENPLUSGX_ENABLE_SANITIZERS)
    set_property(
      TEST gui.libretro_shader_render
      APPEND PROPERTY ENVIRONMENT
        "LSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tests/sanitizers/lsan-opengl.supp:print_suppressions=1"
    )
  endif()
endif()

qt_add_executable(genplusgx_keyboard_input_test keyboard_input_test.cpp)
set_target_properties(genplusgx_keyboard_input_test PROPERTIES AUTOMOC ON)
target_link_libraries(
  genplusgx_keyboard_input_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    GenesisPlusGX::Input
    genplusgx_project_warnings
    genplusgx_test_utilities
    Qt6::Test
    Qt6::Widgets
)
add_test(NAME gui.keyboard_input COMMAND genplusgx_keyboard_input_test)
set_tests_properties(
  gui.keyboard_input
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "gui;unit;integration;input;fixture"
    TIMEOUT 30
)

qt_add_executable(
  genplusgx_input_configuration_dialog_test
  input_configuration_dialog_test.cpp
)
set_target_properties(genplusgx_input_configuration_dialog_test PROPERTIES AUTOMOC ON)
target_link_libraries(
  genplusgx_input_configuration_dialog_test
  PRIVATE
    genplusgx::ui
    genplusgx_project_warnings
    Qt6::Test
    Qt6::Widgets
)
add_test(
  NAME gui.input_configuration
  COMMAND genplusgx_input_configuration_dialog_test
)
set_tests_properties(
  gui.input_configuration
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "gui;unit;input;persistence"
    TIMEOUT 30
)

add_test(NAME gui.desktop_help COMMAND genplusgx_desktop --help)
set_tests_properties(
  gui.desktop_help
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    PASS_REGULAR_EXPRESSION "Native desktop frontend for Genesis Plus GX"
    LABELS "gui;smoke"
)

add_test(NAME gui.desktop_version COMMAND genplusgx_desktop --version)
set_tests_properties(
  gui.desktop_version
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    PASS_REGULAR_EXPRESSION "${PROJECT_VERSION}"
    LABELS "gui;smoke"
)

add_test(
  NAME gui.desktop_startup_smoke
  COMMAND
    "${CMAKE_COMMAND}"
    "-DGENPLUSGX_SMOKE_EXECUTABLE=$<TARGET_FILE:genplusgx_desktop>"
    "-DGENPLUSGX_SMOKE_BASE=${CMAKE_CURRENT_BINARY_DIR}"
    "-DGENPLUSGX_SMOKE_ROOT=${CMAKE_CURRENT_BINARY_DIR}/desktop-smoke-$<CONFIG>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/desktop_startup_smoke.cmake"
)
set_tests_properties(
  gui.desktop_startup_smoke
  PROPERTIES
    LABELS "gui;integration;smoke;lifecycle;filesystem;logging"
    TIMEOUT 40
)

add_test(
  NAME gui.desktop_pseudo_localization_smoke
  COMMAND
    "${CMAKE_COMMAND}"
    "-DGENPLUSGX_SMOKE_EXECUTABLE=$<TARGET_FILE:genplusgx_desktop>"
    "-DGENPLUSGX_SMOKE_BASE=${CMAKE_CURRENT_BINARY_DIR}"
    "-DGENPLUSGX_SMOKE_ROOT=${CMAKE_CURRENT_BINARY_DIR}/desktop-pseudo-smoke-$<CONFIG>"
    "-DGENPLUSGX_SMOKE_PSEUDO_LANGUAGE=ON"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/desktop_startup_smoke.cmake"
)
set_tests_properties(
  gui.desktop_pseudo_localization_smoke
  PROPERTIES
    LABELS "gui;integration;smoke;lifecycle;filesystem;logging;localization"
    TIMEOUT 40
)

add_test(
  NAME gui.desktop_portable_startup_smoke
  COMMAND
    "${CMAKE_COMMAND}"
    "-DGENPLUSGX_SMOKE_EXECUTABLE=$<TARGET_FILE:genplusgx_desktop>"
    "-DGENPLUSGX_SMOKE_BASE=${CMAKE_CURRENT_BINARY_DIR}"
    "-DGENPLUSGX_SMOKE_ROOT=${CMAKE_CURRENT_BINARY_DIR}/portable-smoke-launch-$<CONFIG>/portable-data"
    "-DGENPLUSGX_SMOKE_PORTABLE_EXECUTABLE=${CMAKE_CURRENT_BINARY_DIR}/portable-smoke-launch-$<CONFIG>/genesis-plus-gx-gui"
    "-DGENPLUSGX_SMOKE_PORTABLE=ON"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/desktop_startup_smoke.cmake"
)
set_tests_properties(
  gui.desktop_portable_startup_smoke
  PROPERTIES
    LABELS "gui;integration;smoke;lifecycle;filesystem;logging;portable"
    TIMEOUT 40
)

add_test(
  NAME gui.desktop_portable_failure_smoke
  COMMAND
    "${CMAKE_COMMAND}"
    "-DGENPLUSGX_SMOKE_EXECUTABLE=$<TARGET_FILE:genplusgx_desktop>"
    "-DGENPLUSGX_SMOKE_BASE=${CMAKE_CURRENT_BINARY_DIR}"
    "-DGENPLUSGX_SMOKE_ROOT=${CMAKE_CURRENT_BINARY_DIR}/portable-smoke-blocked-$<CONFIG>/portable-data"
    "-DGENPLUSGX_SMOKE_PORTABLE_EXECUTABLE=${CMAKE_CURRENT_BINARY_DIR}/portable-smoke-blocked-$<CONFIG>/genesis-plus-gx-gui"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/desktop_portable_failure_smoke.cmake"
)
set_tests_properties(
  gui.desktop_portable_failure_smoke
  PROPERTIES
    LABELS "gui;integration;smoke;error-handling;filesystem;portable"
    TIMEOUT 40
)

add_test(
  NAME gui.desktop_startup_error_smoke
  COMMAND
    "${CMAKE_COMMAND}"
    "-DGENPLUSGX_SMOKE_EXECUTABLE=$<TARGET_FILE:genplusgx_desktop>"
    "-DGENPLUSGX_SMOKE_BASE=${CMAKE_CURRENT_BINARY_DIR}"
    "-DGENPLUSGX_SMOKE_ROOT=${CMAKE_CURRENT_BINARY_DIR}/desktop-smoke-error-$<CONFIG>"
    "-DGENPLUSGX_SMOKE_INJECT_CORRUPT_SETTINGS=ON"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/desktop_startup_smoke.cmake"
)
set_tests_properties(
  gui.desktop_startup_error_smoke
  PROPERTIES
    LABELS "gui;integration;smoke;error-handling;filesystem;logging;settings"
    TIMEOUT 40
)
