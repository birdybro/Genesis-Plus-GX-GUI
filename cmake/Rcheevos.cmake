include(FetchContent)

# rcheevos is RetroAchievements' official, MIT-licensed client/runtime. Keep it
# isolated from the inherited emulator warning policy and pin the exact archive
# used by every supported host.
FetchContent_Declare(
  rcheevos_source
  URL
    "https://github.com/RetroAchievements/rcheevos/archive/refs/tags/v12.4.0.tar.gz"
  URL_HASH
    "SHA256=7fb1a43b8edfe727219d054ed868cc985bca54f331f9c2410f818dd3143df5d3"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR __genplusgx_no_upstream_cmake_project__
)
FetchContent_MakeAvailable(rcheevos_source)

file(GLOB genplusgx_rcheevos_root_sources CONFIGURE_DEPENDS
  "${rcheevos_source_SOURCE_DIR}/src/*.c")
file(GLOB genplusgx_rcheevos_runtime_sources CONFIGURE_DEPENDS
  "${rcheevos_source_SOURCE_DIR}/src/rcheevos/*.c")
file(GLOB genplusgx_rcheevos_api_sources CONFIGURE_DEPENDS
  "${rcheevos_source_SOURCE_DIR}/src/rapi/*.c")
file(GLOB genplusgx_rcheevos_hash_sources CONFIGURE_DEPENDS
  "${rcheevos_source_SOURCE_DIR}/src/rhash/*.c")
list(REMOVE_ITEM genplusgx_rcheevos_root_sources
  "${rcheevos_source_SOURCE_DIR}/src/rc_client_external.c"
  "${rcheevos_source_SOURCE_DIR}/src/rc_libretro.c")

add_library(genplusgx_rcheevos STATIC
  ${genplusgx_rcheevos_root_sources}
  ${genplusgx_rcheevos_runtime_sources}
  ${genplusgx_rcheevos_api_sources}
  ${genplusgx_rcheevos_hash_sources}
)
add_library(GenesisPlusGX::Rcheevos ALIAS genplusgx_rcheevos)
target_include_directories(genplusgx_rcheevos
  SYSTEM PUBLIC "${rcheevos_source_SOURCE_DIR}/include"
  PRIVATE "${rcheevos_source_SOURCE_DIR}/src")
target_compile_definitions(genplusgx_rcheevos
  PUBLIC RC_CLIENT_SUPPORTS_HASH
  PRIVATE RC_DISABLE_LUA)
set_target_properties(genplusgx_rcheevos PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED YES
  # The pinned upstream sources use the POSIX strcasecmp/strdup interfaces.
  # Keep GNU/POSIX extensions local to this third-party target.
  C_EXTENSIONS YES)

find_package(Threads REQUIRED)
target_link_libraries(genplusgx_rcheevos PRIVATE Threads::Threads)

set(GENPLUSGX_RCHEEVOS_LICENSE
  "${rcheevos_source_SOURCE_DIR}/LICENSE"
  CACHE INTERNAL "Pinned rcheevos license for packaging")
