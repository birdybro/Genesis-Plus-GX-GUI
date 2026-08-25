add_executable(genplusgx_core_link_test core_link_test.cpp)
target_link_libraries(
  genplusgx_core_link_test
  PRIVATE GenesisPlusGX::Core genplusgx_project_options genplusgx_project_warnings
)

add_test(NAME core.library_link COMMAND genplusgx_core_link_test)
set_tests_properties(core.library_link PROPERTIES LABELS "core;smoke")
