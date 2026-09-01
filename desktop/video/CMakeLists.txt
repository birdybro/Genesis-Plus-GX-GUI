add_library(genplusgx_video_exchange STATIC
  include/genplusgx/video/presentation.h
  include/genplusgx/video/shader_configuration.h
  src/presentation.cpp
  src/frame_exchange.cpp
  src/shader_configuration.cpp
  src/video_geometry.cpp
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
  include/genplusgx/video/libretro_shader_runtime.h
  src/display_widget.cpp
  src/libretro_shader_runtime.cpp
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
  PUBLIC GenesisPlusGX::VideoExchange Qt6::Widgets Qt6::OpenGLWidgets
  PRIVATE genplusgx_project_options genplusgx_project_warnings
)
if(GENPLUSGX_ENABLE_LIBRETRO_SHADERS)
  target_link_libraries(
    genplusgx_video_widget
    PRIVATE GenesisPlusGX::Librashader
  )
  target_compile_definitions(
    genplusgx_video_widget
    PUBLIC GENPLUSGX_HAS_LIBRETRO_SHADERS=1
  )
else()
  target_compile_definitions(
    genplusgx_video_widget
    PUBLIC GENPLUSGX_HAS_LIBRETRO_SHADERS=0
  )
endif()
