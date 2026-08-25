add_library(genplusgx_ui STATIC
  include/genplusgx/ui/about_dialog.h
  include/genplusgx/ui/main_window.h
  src/about_dialog.cpp
  src/main_window.cpp
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
    GenesisPlusGX::VideoWidget
  PRIVATE
    genplusgx_project_options
    genplusgx_project_warnings
)

set_target_properties(genplusgx_ui PROPERTIES AUTOMOC ON)
