add_library(genplusgx_input STATIC src/keyboard_input.cpp)
add_library(GenesisPlusGX::Input ALIAS genplusgx_input)
target_include_directories(
  genplusgx_input PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
  genplusgx_input
  PUBLIC GenesisPlusGX::FrontendTypes Qt6::Core Qt6::Gui
  PRIVATE genplusgx_project_warnings
)
set_target_properties(genplusgx_input PROPERTIES OUTPUT_NAME genplusgx_input)
