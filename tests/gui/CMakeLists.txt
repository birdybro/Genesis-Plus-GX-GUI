qt_add_executable(genplusgx_main_window_test
  main_window_test.cpp
)

qt_add_executable(genplusgx_emulation_controls_test emulation_controls_test.cpp)
set_target_properties(genplusgx_emulation_controls_test PROPERTIES AUTOMOC ON)
target_link_libraries(
  genplusgx_emulation_controls_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    genplusgx::ui
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
add_test(NAME gui.settings COMMAND genplusgx_settings_dialog_test)
set_tests_properties(
  gui.settings
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "gui;unit;settings;accessibility;filesystem"
    TIMEOUT 30
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
    GenesisPlusGX::CoreAdapter
    genplusgx::ui
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
    GenesisPlusGX::CoreAdapter
    GenesisPlusGX::Persistence
    genplusgx::ui
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
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "gui;integration;video"
)

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
