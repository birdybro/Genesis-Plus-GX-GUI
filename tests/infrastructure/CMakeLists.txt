add_executable(genplusgx_infrastructure_test infrastructure_test.cpp)
target_link_libraries(
  genplusgx_infrastructure_test
  PRIVATE genplusgx_project_options genplusgx_project_warnings
)

add_test(NAME infrastructure.version COMMAND genplusgx_infrastructure_test)
set_tests_properties(infrastructure.version PROPERTIES LABELS "unit;infrastructure")

if(GENPLUSGX_BUILD_DESKTOP)
  configure_file(
    package_metadata_test.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/package_metadata_test.cmake"
    @ONLY
  )
  add_test(
    NAME infrastructure.package_metadata
    COMMAND
      "${CMAKE_COMMAND}"
      -P "${CMAKE_CURRENT_BINARY_DIR}/package_metadata_test.cmake"
  )
  set_tests_properties(
    infrastructure.package_metadata
    PROPERTIES LABELS "unit;infrastructure;packaging"
  )

  configure_file(
    windows_package_verification_test.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/windows_package_verification_test.cmake"
    @ONLY
  )
  add_test(
    NAME infrastructure.windows_package_verification
    COMMAND
      "${CMAKE_COMMAND}"
      -P "${CMAKE_CURRENT_BINARY_DIR}/windows_package_verification_test.cmake"
  )
  set_tests_properties(
    infrastructure.windows_package_verification
    PROPERTIES LABELS "unit;infrastructure;packaging"
  )

  configure_file(
    macos_translation_package_test.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/macos_translation_package_test.cmake"
    @ONLY
  )
  add_test(
    NAME infrastructure.macos_translation_package
    COMMAND
      "${CMAKE_COMMAND}"
      -P "${CMAKE_CURRENT_BINARY_DIR}/macos_translation_package_test.cmake"
  )
  set_tests_properties(
    infrastructure.macos_translation_package
    PROPERTIES LABELS "unit;infrastructure;packaging;localization"
  )

  add_test(
    NAME infrastructure.localization_cmake_compatibility
    COMMAND
      "${CMAKE_COMMAND}"
      "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
      "-DMINIMUM_QT_VERSION=${GENPLUSGX_MINIMUM_QT_VERSION}"
      -P "${PROJECT_SOURCE_DIR}/cmake/ValidateLocalizationCMake.cmake"
  )
  set_tests_properties(
    infrastructure.localization_cmake_compatibility
    PROPERTIES LABELS "unit;infrastructure;localization"
  )
endif()

add_test(
  NAME infrastructure.release_tag
  COMMAND
    "${CMAKE_COMMAND}"
    "-DRELEASE_TAG=v${PROJECT_VERSION}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/ValidateReleaseTag.cmake"
)
set_tests_properties(
  infrastructure.release_tag
  PROPERTIES LABELS "unit;infrastructure;release"
)

add_test(
  NAME infrastructure.release_tag_rejects_mismatch
  COMMAND
    "${CMAKE_COMMAND}"
    -DRELEASE_TAG=v999.999.999
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/ValidateReleaseTag.cmake"
)
set_tests_properties(
  infrastructure.release_tag_rejects_mismatch
  PROPERTIES
    LABELS "unit;infrastructure;release"
    WILL_FAIL TRUE
)

add_test(
  NAME infrastructure.release_tag_rejects_malformed
  COMMAND
    "${CMAKE_COMMAND}"
    -DRELEASE_TAG=release-0.1
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/ValidateReleaseTag.cmake"
)
set_tests_properties(
  infrastructure.release_tag_rejects_malformed
  PROPERTIES
    LABELS "unit;infrastructure;release"
    WILL_FAIL TRUE
)

configure_file(
  release_assets_test.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/release_assets_test.cmake"
  @ONLY
)
add_test(
  NAME infrastructure.release_assets
  COMMAND
    "${CMAKE_COMMAND}"
    -P "${CMAKE_CURRENT_BINARY_DIR}/release_assets_test.cmake"
)
set_tests_properties(
  infrastructure.release_assets
  PROPERTIES LABELS "unit;infrastructure;packaging;release"
)

configure_file(
  mac_dmg_retry_test.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/mac_dmg_retry_test.cmake"
  @ONLY
)
add_test(
  NAME infrastructure.mac_dmg_retry
  COMMAND
    "${CMAKE_COMMAND}"
    -P "${CMAKE_CURRENT_BINARY_DIR}/mac_dmg_retry_test.cmake"
)
set_tests_properties(
  infrastructure.mac_dmg_retry
  PROPERTIES LABELS "unit;infrastructure;packaging;release"
)

add_test(
  NAME infrastructure.documentation
  COMMAND
    "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
    "-DEXPECTED_VERSION=${PROJECT_VERSION}"
    -P "${PROJECT_SOURCE_DIR}/cmake/ValidateDocumentation.cmake"
)
set_tests_properties(
  infrastructure.documentation
  PROPERTIES LABELS "unit;infrastructure;documentation"
)
