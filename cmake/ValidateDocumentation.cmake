cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
  message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(NOT DEFINED EXPECTED_VERSION OR EXPECTED_VERSION STREQUAL "")
  message(FATAL_ERROR "EXPECTED_VERSION is required")
endif()

cmake_path(ABSOLUTE_PATH SOURCE_ROOT NORMALIZE)

set(required_documents
  README.md
  CHANGELOG.md
  CONTRIBUTING.md
  THIRD_PARTY_NOTICES.md
  docs/ACHIEVEMENTS.md
  docs/ARCHITECTURE.md
  docs/BUILDING.md
  docs/CLOUD_SYNC.md
  docs/ONLINE_METADATA.md
  docs/UPDATES.md
  docs/DEVELOPMENT.md
  docs/DEBUG_TOOLS.md
  docs/TESTING.md
  docs/USER_GUIDE.md
  docs/KEYBOARD_SHORTCUTS.md
  docs/INPUT_CONFIGURATION.md
  docs/INPUT_MOVIES.md
  docs/LIBRETRO_SHADERS.md
  docs/LOCALIZATION.md
  docs/ARTWORK_OVERLAYS.md
  docs/DISPLAY_SYNCHRONIZATION.md
  docs/RECORDING.md
  docs/RUN_AHEAD.md
  docs/BIOS.md
  docs/GAME_LIBRARY.md
  docs/SAVE_STATES.md
  docs/STREAMING.md
  docs/PACKAGING.md
  docs/PHYSICAL_MEDIA.md
  docs/PORTABLE_MODE.md
  docs/RELEASES.md
  docs/UPSTREAM_MAINTENANCE.md
  docs/DEVELOPMENT_PLAN.md
  docs/FINAL_TEST_REPORT.md
  docs/REQUIREMENTS_AUDIT.md
  docs/TEST_MATRIX.md
)

foreach(relative_path IN LISTS required_documents)
  set(path "${SOURCE_ROOT}/${relative_path}")
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Required documentation is missing: ${relative_path}")
  endif()
  file(SIZE "${path}" size)
  if(size LESS 64)
    message(FATAL_ERROR "Required documentation is unexpectedly empty: ${relative_path}")
  endif()
endforeach()

function(require_document_text relative_path expected_text)
  file(READ "${SOURCE_ROOT}/${relative_path}" contents)
  string(REGEX REPLACE "[\r\n]+" " " contents "${contents}")
  string(REGEX REPLACE " +" " " contents "${contents}")
  string(FIND "${contents}" "${expected_text}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR
      "${relative_path} does not contain required text: ${expected_text}")
  endif()
endfunction()

foreach(heading IN ITEMS
    "# Genesis Plus GX GUI"
    "## Screenshots"
    "## Supported systems"
    "## Features"
    "## Installing"
    "## User data and saves"
    "## Building from source"
    "## Testing"
    "## Contributing"
    "## License and upstream relationship")
  require_document_text(README.md "${heading}")
endforeach()
require_document_text(README.md "currently version ${EXPECTED_VERSION}")
require_document_text(README.md "official upstream Genesis Plus GX project")
require_document_text(README.md "Windows 10/11 x64")
require_document_text(README.md "macOS Apple Silicon and Intel")
require_document_text(docs/LOCALIZATION.md "unit.translation_catalog")
require_document_text(docs/LOCALIZATION.md "Pseudo-localization (layout testing)")

foreach(component IN ITEMS
    "Qt 6"
    "SDL 3"
    "Genesis Plus GX"
    "librashader"
    "rcheevos"
    "QtKeychain"
    "Monocypher"
    "Retronian GameDB"
    "libchdr"
    "zlib"
    "zstd"
    "Tremor"
    "Nuked OPN2"
    "minimp3"
    "LZMA SDK")
  require_document_text(THIRD_PARTY_NOTICES.md "${component}")
endforeach()

foreach(menu_text IN ITEMS
    "File → Open Game"
    "File → Exit"
    "Emulation → Pause"
    "Emulation → Save State"
    "Fast Forward"
    "Frame Advance"
    "Video → Fullscreen"
    "Audio → Audio Settings"
    "Input → Controller Configuration"
    "Tools → Cheats"
    "Tools → RetroAchievements"
    "Tools → Cloud Synchronization"
    "Tools → TAS and Input Movies"
    "Tools → Local A/V Streaming Output"
    "Tools → Game Information"
    "Tools → BIOS Settings"
    "Tools → Log and Diagnostics"
    "Help → User Guide")
  # Kept outside the older menu inventory above because update checks are a
  # separately gated network feature rather than an emulator control.
  require_document_text(docs/USER_GUIDE.md "${menu_text}")
endforeach()
require_document_text(docs/USER_GUIDE.md "Help → Check for Updates")

set(stale_phrases
  "end-user guide will expand through the remaining milestones"
  "complete source-build guide remains this document"
  "Configurable emulator hotkeys are introduced by the later settings milestone"
)
foreach(relative_path IN ITEMS
    README.md
    docs/USER_GUIDE.md
    docs/KEYBOARD_SHORTCUTS.md)
  file(READ "${SOURCE_ROOT}/${relative_path}" contents)
  foreach(stale_phrase IN LISTS stale_phrases)
    string(FIND "${contents}" "${stale_phrase}" position)
    if(NOT position EQUAL -1)
      message(FATAL_ERROR
        "${relative_path} contains stale milestone language: ${stale_phrase}")
    endif()
  endforeach()
endforeach()

file(GLOB documentation_files LIST_DIRECTORIES FALSE "${SOURCE_ROOT}/docs/*.md")
list(APPEND documentation_files
  "${SOURCE_ROOT}/README.md"
  "${SOURCE_ROOT}/CHANGELOG.md"
  "${SOURCE_ROOT}/CONTRIBUTING.md"
  "${SOURCE_ROOT}/THIRD_PARTY_NOTICES.md"
  "${SOURCE_ROOT}/tests/fixtures/README.md"
  "${SOURCE_ROOT}/docs/screenshots/README.md"
)

set(validated_links 0)
foreach(document IN LISTS documentation_files)
  file(READ "${document}" contents)
  string(REGEX MATCHALL "\\[[^]\n]+\\]\\([^)]+\\)" links "${contents}")
  get_filename_component(document_directory "${document}" DIRECTORY)
  foreach(link IN LISTS links)
    string(REGEX REPLACE "^.*\\]\\(([^)]+)\\)$" "\\1" destination "${link}")
    string(REGEX REPLACE "^<([^>]+)>$" "\\1" destination "${destination}")
    if(destination MATCHES "^#" OR
       destination MATCHES "^[A-Za-z][A-Za-z0-9+.-]*:")
      continue()
    endif()
    string(REGEX REPLACE "#.*$" "" destination "${destination}")
    string(REPLACE "%20" " " destination "${destination}")
    if(destination STREQUAL "")
      continue()
    endif()
    get_filename_component(resolved "${document_directory}/${destination}" ABSOLUTE)
    if(NOT EXISTS "${resolved}")
      file(RELATIVE_PATH source_document "${SOURCE_ROOT}" "${document}")
      message(FATAL_ERROR
        "Broken local documentation link in ${source_document}: ${destination}")
    endif()
    math(EXPR validated_links "${validated_links} + 1")
  endforeach()
endforeach()

if(validated_links LESS 25)
  message(FATAL_ERROR
    "Documentation link audit found only ${validated_links} local links")
endif()

list(LENGTH required_documents required_count)
message(STATUS
  "Validated ${required_count} required documents and ${validated_links} local links")
