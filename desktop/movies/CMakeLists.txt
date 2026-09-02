add_library(genplusgx_movies STATIC
  include/genplusgx/movies/input_movie.h
  src/input_movie.cpp
)
add_library(GenesisPlusGX::Movies ALIAS genplusgx_movies)
target_include_directories(genplusgx_movies PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(genplusgx_movies
  PUBLIC GenesisPlusGX::FrontendTypes
  PRIVATE genplusgx_project_warnings Qt6::Core)
