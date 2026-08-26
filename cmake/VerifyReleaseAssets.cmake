cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS ASSET_DIRECTORY RELEASE_VERSION)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

file(REAL_PATH "${ASSET_DIRECTORY}" asset_directory)
if(NOT IS_DIRECTORY "${asset_directory}")
  message(FATAL_ERROR "Release asset directory does not exist: ${asset_directory}")
endif()

set(package_prefix "Genesis-Plus-GX-GUI-${RELEASE_VERSION}")
set(archives
  "${package_prefix}-linux-x86_64.tar.gz"
  "${package_prefix}-windows-x86_64.zip"
  "${package_prefix}-macos-arm64.zip"
  "${package_prefix}-macos-arm64.dmg"
  "${package_prefix}-macos-x86_64.zip"
  "${package_prefix}-macos-x86_64.dmg"
)

set(aggregate "")
foreach(archive IN LISTS archives)
  set(archive_path "${asset_directory}/${archive}")
  set(checksum_path "${archive_path}.sha256")
  if(NOT EXISTS "${archive_path}")
    message(FATAL_ERROR "Missing release archive: ${archive}")
  endif()
  if(NOT EXISTS "${checksum_path}")
    message(FATAL_ERROR "Missing release checksum: ${archive}.sha256")
  endif()

  file(STRINGS "${checksum_path}" checksum_lines)
  list(LENGTH checksum_lines checksum_line_count)
  if(NOT checksum_line_count EQUAL 1)
    message(FATAL_ERROR "Checksum file must contain one line: ${archive}.sha256")
  endif()
  list(GET checksum_lines 0 checksum_line)
  if(NOT checksum_line MATCHES "^([0-9A-Fa-f]+)[ \\t]+\\*?(.+)$")
    message(FATAL_ERROR "Malformed checksum file: ${archive}.sha256")
  endif()
  string(TOLOWER "${CMAKE_MATCH_1}" expected_hash)
  set(expected_name "${CMAKE_MATCH_2}")
  string(LENGTH "${expected_hash}" expected_hash_length)
  if(NOT expected_hash_length EQUAL 64)
    message(FATAL_ERROR "Checksum digest must contain 64 hexadecimal characters")
  endif()
  if(NOT expected_name STREQUAL archive)
    message(FATAL_ERROR
      "Checksum filename '${expected_name}' does not match archive '${archive}'")
  endif()

  file(SHA256 "${archive_path}" actual_hash)
  string(TOLOWER "${actual_hash}" actual_hash)
  if(NOT actual_hash STREQUAL expected_hash)
    message(FATAL_ERROR "Checksum mismatch for release archive: ${archive}")
  endif()
  string(APPEND aggregate "${actual_hash}  ${archive}\n")
endforeach()

file(WRITE "${asset_directory}/SHA256SUMS.txt" "${aggregate}")
message(STATUS "Verified ${archives} and wrote SHA256SUMS.txt")
