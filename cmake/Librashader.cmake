include(FetchContent)

find_program(GENPLUSGX_CARGO_EXECUTABLE cargo)
find_program(GENPLUSGX_RUSTC_EXECUTABLE rustc)
if(NOT GENPLUSGX_CARGO_EXECUTABLE OR NOT GENPLUSGX_RUSTC_EXECUTABLE)
  message(FATAL_ERROR
    "Libretro shader support requires Cargo and Rust 1.88 or newer")
endif()
execute_process(
  COMMAND "${GENPLUSGX_RUSTC_EXECUTABLE}" --version
  OUTPUT_VARIABLE genplusgx_rustc_version
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE genplusgx_rustc_result
)
if(NOT genplusgx_rustc_result EQUAL 0 OR
   NOT genplusgx_rustc_version MATCHES
     "^rustc ([0-9]+)\\.([0-9]+)\\.([0-9]+)")
  message(FATAL_ERROR "Could not determine the installed Rust version")
endif()
if(CMAKE_MATCH_1 LESS 1 OR
   (CMAKE_MATCH_1 EQUAL 1 AND CMAKE_MATCH_2 LESS 88))
  message(FATAL_ERROR
    "Libretro shader support requires Rust 1.88 or newer; found ${genplusgx_rustc_version}")
endif()
message(STATUS "Rust: ${genplusgx_rustc_version}")

FetchContent_Declare(
  librashader_source
  URL
    "https://github.com/SnowflakePowered/librashader/archive/refs/tags/librashader-v0.12.0.tar.gz"
  URL_HASH
    "SHA256=4bf8cf2489d00848dcabbf2163204093776082da4217d5a5db45e4cbf335cedf"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(librashader_source)

set(genplusgx_librashader_target_directory
  "${CMAKE_BINARY_DIR}/librashader-target")
if(WIN32)
  set(genplusgx_librashader_built_library
    "${genplusgx_librashader_target_directory}/release/librashader_capi.dll")
  set(genplusgx_librashader_install_name "librashader.dll")
elseif(APPLE)
  set(genplusgx_librashader_built_library
    "${genplusgx_librashader_target_directory}/release/liblibrashader_capi.dylib")
  set(genplusgx_librashader_install_name "librashader.dylib")
else()
  set(genplusgx_librashader_built_library
    "${genplusgx_librashader_target_directory}/release/liblibrashader_capi.so")
  set(genplusgx_librashader_install_name "librashader.so")
endif()

add_custom_command(
  OUTPUT "${genplusgx_librashader_built_library}"
  COMMAND
    "${CMAKE_COMMAND}" -E env
      "CARGO_TARGET_DIR=${genplusgx_librashader_target_directory}"
      "RUSTFLAGS=-Aunused-imports"
      "${GENPLUSGX_CARGO_EXECUTABLE}" build --quiet --release --locked
        --manifest-path "${librashader_source_SOURCE_DIR}/Cargo.toml"
        --package librashader-capi
        --no-default-features
        --features runtime-opengl
  WORKING_DIRECTORY "${librashader_source_SOURCE_DIR}"
  COMMENT "Building pinned librashader 0.12.0 OpenGL runtime"
  VERBATIM
)
add_custom_target(
  genplusgx_librashader_runtime ALL
  DEPENDS "${genplusgx_librashader_built_library}"
)

add_library(genplusgx_librashader_headers INTERFACE)
add_library(GenesisPlusGX::Librashader ALIAS genplusgx_librashader_headers)
target_include_directories(
  genplusgx_librashader_headers
  INTERFACE "${librashader_source_SOURCE_DIR}/include"
)
target_compile_definitions(
  genplusgx_librashader_headers
  INTERFACE
    LIBRA_RUNTIME_OPENGL
    GENPLUSGX_LIBRASHADER_BUILD_PATH="${genplusgx_librashader_built_library}"
    GENPLUSGX_BUILTIN_SHADER_SOURCE_DIR="${PROJECT_SOURCE_DIR}/desktop/resources/shaders"
)
add_dependencies(genplusgx_librashader_headers genplusgx_librashader_runtime)

set(
  GENPLUSGX_LIBRASHADER_BUILT_LIBRARY
  "${genplusgx_librashader_built_library}"
  CACHE INTERNAL "Pinned librashader runtime built for packaging"
)
set(
  GENPLUSGX_LIBRASHADER_INSTALL_NAME
  "${genplusgx_librashader_install_name}"
  CACHE INTERNAL "Packaged librashader runtime filename"
)
