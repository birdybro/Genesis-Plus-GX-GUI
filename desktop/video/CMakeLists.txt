add_library(genplusgx_video_exchange STATIC
  src/frame_exchange.cpp
)
add_library(GenesisPlusGX::VideoExchange ALIAS genplusgx_video_exchange)
target_include_directories(
  genplusgx_video_exchange
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(
  genplusgx_video_exchange
  PUBLIC GenesisPlusGX::FrontendTypes
  PRIVATE genplusgx_project_warnings
)

add_library(genplusgx_video_widget STATIC
  include/genplusgx/video/display_widget.h
  src/display_widget.cpp
)
add_library(GenesisPlusGX::VideoWidget ALIAS genplusgx_video_widget)
target_include_directories(
  genplusgx_video_widget
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(
  genplusgx_video_widget
  PUBLIC GenesisPlusGX::VideoExchange Qt6::Widgets
  PRIVATE genplusgx_project_options genplusgx_project_warnings
)
