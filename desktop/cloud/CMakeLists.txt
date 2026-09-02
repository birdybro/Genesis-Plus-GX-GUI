add_library(genplusgx_cloud STATIC
  include/genplusgx/cloud/cloud_credentials.h
  include/genplusgx/cloud/cloud_manifest.h
  include/genplusgx/cloud/cloud_remote.h
  include/genplusgx/cloud/cloud_settings.h
  include/genplusgx/cloud/cloud_sync.h
  include/genplusgx/cloud/cloud_types.h
  src/cloud_credentials.cpp
  src/cloud_manifest.cpp
  src/cloud_remote.cpp
  src/cloud_settings.cpp
  src/cloud_sync.cpp
)
add_library(GenesisPlusGX::Cloud ALIAS genplusgx_cloud)
target_include_directories(genplusgx_cloud
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(genplusgx_cloud
  PUBLIC GenesisPlusGX::Persistence Qt6::Core
  PRIVATE Qt6::Network genplusgx_project_options genplusgx_project_warnings)
if(GENPLUSGX_ENABLE_CLOUD_SYNC)
  target_compile_definitions(genplusgx_cloud PUBLIC GENPLUSGX_HAVE_CLOUD_SYNC=1)
  target_link_libraries(genplusgx_cloud PRIVATE GenesisPlusGX::QtKeychain)
endif()
set_target_properties(genplusgx_cloud PROPERTIES AUTOMOC ON)
