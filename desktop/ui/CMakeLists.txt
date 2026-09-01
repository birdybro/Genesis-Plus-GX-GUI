add_library(genplusgx_ui STATIC
  include/genplusgx/ui/appearance_settings_dialog.h
  include/genplusgx/ui/audio_settings_dialog.h
  include/genplusgx/ui/about_dialog.h
  include/genplusgx/ui/bios_settings_dialog.h
  include/genplusgx/ui/binding_capture_button.h
  include/genplusgx/ui/cheat_manager_dialog.h
  include/genplusgx/ui/dialog_service.h
  include/genplusgx/ui/diagnostics_dialog.h
  include/genplusgx/ui/debug_tools_window.h
  include/genplusgx/ui/game_information_dialog.h
  include/genplusgx/ui/game_library_dialog.h
  include/genplusgx/ui/help_dialog.h
  include/genplusgx/ui/input_configuration_dialog.h
  include/genplusgx/ui/main_window.h
  include/genplusgx/ui/per_game_settings_dialog.h
  include/genplusgx/ui/rewind_settings_dialog.h
  include/genplusgx/ui/screenshot_settings_dialog.h
  include/genplusgx/ui/settings_dialog.h
  include/genplusgx/ui/session_settings_dialog.h
  include/genplusgx/ui/system_settings_dialog.h
  include/genplusgx/ui/theme_controller.h
  include/genplusgx/ui/video_settings_dialog.h
  src/appearance_settings_dialog.cpp
  src/about_dialog.cpp
  src/audio_settings_dialog.cpp
  src/bios_settings_dialog.cpp
  src/binding_capture_button.cpp
  src/cheat_manager_dialog.cpp
  src/dialog_service.cpp
  src/diagnostics_dialog.cpp
  src/debug_tools_window.cpp
  src/game_information_dialog.cpp
  src/game_library_dialog.cpp
  src/help_dialog.cpp
  src/input_configuration_dialog.cpp
  src/main_window.cpp
  src/per_game_settings_dialog.cpp
  src/rewind_settings_dialog.cpp
  src/screenshot_settings_dialog.cpp
  src/settings_dialog.cpp
  src/session_settings_dialog.cpp
  src/system_settings_dialog.cpp
  src/theme_controller.cpp
  src/video_settings_dialog.cpp
)

add_library(genplusgx::ui ALIAS genplusgx_ui)

target_include_directories(
  genplusgx_ui
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(
  genplusgx_ui
  PUBLIC
    Qt6::Widgets
    GenesisPlusGX::Cheats
    GenesisPlusGX::Diagnostics
    GenesisPlusGX::GameFiles
    GenesisPlusGX::Input
    GenesisPlusGX::Library
    GenesisPlusGX::Platform
    GenesisPlusGX::Settings
    GenesisPlusGX::VideoWidget
  PRIVATE
    GenesisPlusGX::CoreAdapter
    genplusgx_project_options
    genplusgx_project_warnings
)

set_target_properties(genplusgx_ui PROPERTIES AUTOMOC ON)
