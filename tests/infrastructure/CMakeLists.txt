add_executable(genplusgx_infrastructure_test infrastructure_test.cpp)
target_link_libraries(
  genplusgx_infrastructure_test
  PRIVATE genplusgx_project_options genplusgx_project_warnings
)

add_test(NAME infrastructure.version COMMAND genplusgx_infrastructure_test)
set_tests_properties(infrastructure.version PROPERTIES LABELS "unit;infrastructure")

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
