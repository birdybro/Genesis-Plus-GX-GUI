include(FetchContent)

# Monocypher supplies the small, independently audited Ed25519 verifier used for
# release manifests. Keep the pinned third-party sources outside the project's
# warning-as-error policy and build only the standard Ed25519 implementation.
FetchContent_Declare(
  monocypher_source
  URL
    "https://github.com/LoupVaillant/Monocypher/releases/download/4.0.2/monocypher-4.0.2.tar.gz"
  URL_HASH
    "SHA256=38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR __genplusgx_no_upstream_cmake_project__
)
FetchContent_MakeAvailable(monocypher_source)

add_library(genplusgx_monocypher STATIC
  "${monocypher_source_SOURCE_DIR}/src/monocypher.c"
  "${monocypher_source_SOURCE_DIR}/src/optional/monocypher-ed25519.c"
)
add_library(GenesisPlusGX::Monocypher ALIAS genplusgx_monocypher)
target_include_directories(genplusgx_monocypher SYSTEM PUBLIC
  "${monocypher_source_SOURCE_DIR}/src/optional"
  "${monocypher_source_SOURCE_DIR}/src")
set_target_properties(genplusgx_monocypher PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED YES
  C_EXTENSIONS NO)

set(GENPLUSGX_MONOCYPHER_LICENSE
  "${monocypher_source_SOURCE_DIR}/LICENCE.md"
  CACHE INTERNAL "Pinned Monocypher license for packaging")
