qt_add_executable(genplusgx_main_window_test
  main_window_test.cpp
)

set_target_properties(genplusgx_main_window_test PROPERTIES AUTOMOC ON)

target_link_libraries(
  genplusgx_main_window_test
  PRIVATE
    genplusgx::ui
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
