add_library(
  genplusgx_netplay_core STATIC
  include/genplusgx/netplay/netplay_bridge.h
  include/genplusgx/netplay/netplay_timeline.h
  include/genplusgx/netplay/netplay_types.h
  src/netplay_bridge.cpp
  src/netplay_timeline.cpp
)
add_library(GenesisPlusGX::NetplayCore ALIAS genplusgx_netplay_core)
target_include_directories(
  genplusgx_netplay_core
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(
  genplusgx_netplay_core
  PUBLIC GenesisPlusGX::FrontendTypes
  PRIVATE genplusgx_project_options genplusgx_project_warnings
)

add_library(
  genplusgx_netplay STATIC
  include/genplusgx/netplay/netplay_protocol.h
  include/genplusgx/netplay/netplay_session.h
  src/netplay_protocol.cpp
  src/netplay_session.cpp
)
add_library(GenesisPlusGX::Netplay ALIAS genplusgx_netplay)
target_include_directories(
  genplusgx_netplay
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(
  genplusgx_netplay
  PUBLIC GenesisPlusGX::NetplayCore Qt6::Core Qt6::Network
  PRIVATE genplusgx_project_options genplusgx_project_warnings
)
set_target_properties(genplusgx_netplay PROPERTIES AUTOMOC ON)
