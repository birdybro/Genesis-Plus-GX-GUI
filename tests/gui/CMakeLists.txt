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
