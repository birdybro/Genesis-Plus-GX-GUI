include(FetchContent)

# Build the small QtKeychain source set directly. The upstream top-level build
# deliberately changes global output directories and consumes BUILD_TESTING;
# keeping the target local avoids either side effect in this parent project.
FetchContent_Declare(
  qtkeychain_source
  URL
    "https://github.com/frankosterfeld/qtkeychain/archive/refs/tags/0.17.0.tar.gz"
  URL_HASH
    "SHA256=3b85c3929034b0a99da777130c34d99f006fcd3a9d56564159399a33fee0e504"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR __genplusgx_no_upstream_cmake_project__
)
FetchContent_MakeAvailable(qtkeychain_source)

set(genplusgx_qtkeychain_sources
  "${qtkeychain_source_SOURCE_DIR}/qtkeychain/keychain.cpp"
  "${qtkeychain_source_SOURCE_DIR}/qtkeychain/keychain.h"
  "${qtkeychain_source_SOURCE_DIR}/qtkeychain/keychain_p.h")
set(genplusgx_qtkeychain_libraries Qt6::Core)

if(WIN32)
  list(APPEND genplusgx_qtkeychain_sources
    "${qtkeychain_source_SOURCE_DIR}/qtkeychain/keychain_win.cpp")
  list(APPEND genplusgx_qtkeychain_libraries crypt32)
elseif(APPLE)
  enable_language(OBJCXX)
  list(APPEND genplusgx_qtkeychain_sources
    "${qtkeychain_source_SOURCE_DIR}/qtkeychain/keychain_apple.mm")
  list(APPEND genplusgx_qtkeychain_libraries
    "-framework Foundation" "-framework Security")
elseif(UNIX)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)
  set(genplusgx_qtkeychain_dbus_sources)
  qt6_add_dbus_interface(genplusgx_qtkeychain_dbus_sources
    "${qtkeychain_source_SOURCE_DIR}/qtkeychain/org.kde.KWallet.xml"
    kwallet_interface KWalletInterface)
  list(APPEND genplusgx_qtkeychain_sources
    ${genplusgx_qtkeychain_dbus_sources}
    "${qtkeychain_source_SOURCE_DIR}/qtkeychain/gnomekeyring.cpp"
    "${qtkeychain_source_SOURCE_DIR}/qtkeychain/gnomekeyring_p.h"
    "${qtkeychain_source_SOURCE_DIR}/qtkeychain/keychain_unix.cpp"
    "${qtkeychain_source_SOURCE_DIR}/qtkeychain/libsecret.cpp"
    "${qtkeychain_source_SOURCE_DIR}/qtkeychain/libsecret_p.h"
    "${qtkeychain_source_SOURCE_DIR}/qtkeychain/plaintextstore.cpp"
    "${qtkeychain_source_SOURCE_DIR}/qtkeychain/plaintextstore_p.h")
  list(APPEND genplusgx_qtkeychain_libraries Qt6::DBus PkgConfig::LIBSECRET)
endif()

add_library(genplusgx_qtkeychain STATIC ${genplusgx_qtkeychain_sources})
add_library(GenesisPlusGX::QtKeychain ALIAS genplusgx_qtkeychain)
target_include_directories(genplusgx_qtkeychain
  SYSTEM PUBLIC "${qtkeychain_source_SOURCE_DIR}"
  PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(genplusgx_qtkeychain
  PUBLIC QTKEYCHAIN_NO_EXPORT)
if(WIN32)
  target_compile_definitions(genplusgx_qtkeychain
    PRIVATE USE_CREDENTIAL_STORE UNICODE _UNICODE)
elseif(UNIX AND NOT APPLE)
  target_compile_definitions(genplusgx_qtkeychain
    PRIVATE HAVE_LIBSECRET KEYCHAIN_DBUS)
endif()
target_link_libraries(genplusgx_qtkeychain
  PUBLIC ${genplusgx_qtkeychain_libraries})
set_target_properties(genplusgx_qtkeychain PROPERTIES AUTOMOC ON)

set(GENPLUSGX_QTKEYCHAIN_LICENSE
  "${qtkeychain_source_SOURCE_DIR}/COPYING"
  CACHE INTERNAL "Pinned QtKeychain license for packaging")
