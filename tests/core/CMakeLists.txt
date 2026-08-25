add_executable(genplusgx_core_link_test core_link_test.cpp)
target_link_libraries(
  genplusgx_core_link_test
  PRIVATE GenesisPlusGX::Core genplusgx_project_options genplusgx_project_warnings
)

add_test(NAME core.library_link COMMAND genplusgx_core_link_test)
set_tests_properties(core.library_link PROPERTIES LABELS "core;smoke")

add_executable(genplusgx_core_synthetic_rom_test synthetic_rom_test.cpp)
target_link_libraries(
  genplusgx_core_synthetic_rom_test
  PRIVATE
    GenesisPlusGX::Core
    genplusgx_project_options
    genplusgx_project_warnings
    genplusgx_test_utilities
)

add_test(NAME core.synthetic_genesis_rom COMMAND genplusgx_core_synthetic_rom_test)
set_tests_properties(
  core.synthetic_genesis_rom PROPERTIES LABELS "core;integration;fixture"
)

add_executable(genplusgx_core_lifecycle_test core_lifecycle_test.cpp)
target_link_libraries(
  genplusgx_core_lifecycle_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    genplusgx_project_options
    genplusgx_project_warnings
    genplusgx_test_utilities
)

add_test(NAME core.lifecycle COMMAND genplusgx_core_lifecycle_test)
set_tests_properties(
  core.lifecycle PROPERTIES LABELS "core;integration;lifecycle" TIMEOUT 30
)

add_executable(genplusgx_core_video_test core_video_test.cpp)
target_link_libraries(
  genplusgx_core_video_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    genplusgx_project_options
    genplusgx_project_warnings
    genplusgx_test_utilities
)

add_test(NAME core.video_frame COMMAND genplusgx_core_video_test)
set_tests_properties(
  core.video_frame PROPERTIES LABELS "core;integration;video;fixture" TIMEOUT 30
)

add_executable(genplusgx_core_audio_test core_audio_test.cpp)
target_link_libraries(
  genplusgx_core_audio_test
  PRIVATE
    GenesisPlusGX::CoreAdapter
    genplusgx_project_options
    genplusgx_project_warnings
    genplusgx_test_utilities
)

add_test(NAME core.audio_samples COMMAND genplusgx_core_audio_test)
set_tests_properties(
  core.audio_samples PROPERTIES LABELS "core;integration;audio;fixture" TIMEOUT 30
)
