add_library(genplusgx_localization_api INTERFACE)
add_library(GenesisPlusGX::LocalizationApi ALIAS genplusgx_localization_api)
target_include_directories(
  genplusgx_localization_api
  INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(genplusgx_localization_api INTERFACE Qt6::Core)

add_library(genplusgx_localization STATIC
  include/genplusgx/localization/localization.h
  src/localization.cpp
)
add_library(GenesisPlusGX::Localization ALIAS genplusgx_localization)

target_link_libraries(
  genplusgx_localization
  PUBLIC GenesisPlusGX::LocalizationApi
  PRIVATE genplusgx_project_options genplusgx_project_warnings
)
