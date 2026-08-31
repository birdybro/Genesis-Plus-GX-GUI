set(GENPLUSGX_CORE_ROOT "${PROJECT_SOURCE_DIR}/core")
set(GENPLUSGX_LIBCHDR_ROOT ${GENPLUSGX_CORE_ROOT}/cd_hw/libchdr)

set(
  GENPLUSGX_CORE_SOURCES
  ${GENPLUSGX_CORE_ROOT}/genesis.c
  ${GENPLUSGX_CORE_ROOT}/io_ctrl.c
  ${GENPLUSGX_CORE_ROOT}/loadrom.c
  ${GENPLUSGX_CORE_ROOT}/mem68k.c
  ${GENPLUSGX_CORE_ROOT}/membnk.c
  ${GENPLUSGX_CORE_ROOT}/memz80.c
  ${GENPLUSGX_CORE_ROOT}/state.c
  ${GENPLUSGX_CORE_ROOT}/system.c
  ${GENPLUSGX_CORE_ROOT}/vdp_ctrl.c
  ${GENPLUSGX_CORE_ROOT}/vdp_render.c
  ${GENPLUSGX_CORE_ROOT}/z80/z80.c
  ${GENPLUSGX_CORE_ROOT}/m68k/m68kcpu.c
  ${GENPLUSGX_CORE_ROOT}/m68k/s68kcpu.c
  ${GENPLUSGX_CORE_ROOT}/ntsc/md_ntsc.c
  ${GENPLUSGX_CORE_ROOT}/ntsc/sms_ntsc.c
  ${GENPLUSGX_CORE_ROOT}/sound/blip_buf.c
  ${GENPLUSGX_CORE_ROOT}/sound/eq.c
  ${GENPLUSGX_CORE_ROOT}/sound/opll.c
  ${GENPLUSGX_CORE_ROOT}/sound/psg.c
  ${GENPLUSGX_CORE_ROOT}/sound/sound.c
  ${GENPLUSGX_CORE_ROOT}/sound/ym2413.c
  ${GENPLUSGX_CORE_ROOT}/sound/ym2612.c
  ${GENPLUSGX_CORE_ROOT}/sound/ym3438.c
  ${GENPLUSGX_CORE_ROOT}/input_hw/activator.c
  ${GENPLUSGX_CORE_ROOT}/input_hw/gamepad.c
  ${GENPLUSGX_CORE_ROOT}/input_hw/graphic_board.c
  ${GENPLUSGX_CORE_ROOT}/input_hw/input.c
  ${GENPLUSGX_CORE_ROOT}/input_hw/lightgun.c
  ${GENPLUSGX_CORE_ROOT}/input_hw/mouse.c
  ${GENPLUSGX_CORE_ROOT}/input_hw/paddle.c
  ${GENPLUSGX_CORE_ROOT}/input_hw/smash.c
  ${GENPLUSGX_CORE_ROOT}/input_hw/sportspad.c
  ${GENPLUSGX_CORE_ROOT}/input_hw/teamplayer.c
  ${GENPLUSGX_CORE_ROOT}/input_hw/terebi_oekaki.c
  ${GENPLUSGX_CORE_ROOT}/input_hw/xe_1ap.c
  ${GENPLUSGX_CORE_ROOT}/cd_hw/cd_cart.c
  ${GENPLUSGX_CORE_ROOT}/cd_hw/cdc.c
  ${GENPLUSGX_CORE_ROOT}/cd_hw/cdd.c
  ${GENPLUSGX_CORE_ROOT}/cd_hw/gfx.c
  ${GENPLUSGX_CORE_ROOT}/cd_hw/pcm.c
  ${GENPLUSGX_CORE_ROOT}/cd_hw/scd.c
  ${GENPLUSGX_CORE_ROOT}/cart_hw/areplay.c
  ${GENPLUSGX_CORE_ROOT}/cart_hw/eeprom_93c.c
  ${GENPLUSGX_CORE_ROOT}/cart_hw/eeprom_i2c.c
  ${GENPLUSGX_CORE_ROOT}/cart_hw/eeprom_spi.c
  ${GENPLUSGX_CORE_ROOT}/cart_hw/flash_cfi.c
  ${GENPLUSGX_CORE_ROOT}/cart_hw/ggenie.c
  ${GENPLUSGX_CORE_ROOT}/cart_hw/md_cart.c
  ${GENPLUSGX_CORE_ROOT}/cart_hw/megasd.c
  ${GENPLUSGX_CORE_ROOT}/cart_hw/sms_cart.c
  ${GENPLUSGX_CORE_ROOT}/cart_hw/sram.c
  ${GENPLUSGX_CORE_ROOT}/cart_hw/yx5200.c
  ${GENPLUSGX_CORE_ROOT}/cart_hw/svp/ssp16.c
  ${GENPLUSGX_CORE_ROOT}/cart_hw/svp/svp.c
  ${GENPLUSGX_LIBCHDR_ROOT}/deps/zlib-1.3.1/crc32.c
)

if(GENPLUSGX_ENABLE_TREMOR)
  list(
    APPEND GENPLUSGX_CORE_SOURCES
    ${GENPLUSGX_CORE_ROOT}/sound/tremor/bitwise.c
    ${GENPLUSGX_CORE_ROOT}/sound/tremor/block.c
    ${GENPLUSGX_CORE_ROOT}/sound/tremor/codebook.c
    ${GENPLUSGX_CORE_ROOT}/sound/tremor/floor0.c
    ${GENPLUSGX_CORE_ROOT}/sound/tremor/floor1.c
    ${GENPLUSGX_CORE_ROOT}/sound/tremor/framing.c
    ${GENPLUSGX_CORE_ROOT}/sound/tremor/info.c
    ${GENPLUSGX_CORE_ROOT}/sound/tremor/mapping0.c
    ${GENPLUSGX_CORE_ROOT}/sound/tremor/mdct.c
    ${GENPLUSGX_CORE_ROOT}/sound/tremor/registry.c
    ${GENPLUSGX_CORE_ROOT}/sound/tremor/res012.c
    ${GENPLUSGX_CORE_ROOT}/sound/tremor/sharedbook.c
    ${GENPLUSGX_CORE_ROOT}/sound/tremor/synthesis.c
    ${GENPLUSGX_CORE_ROOT}/sound/tremor/vorbisfile.c
    ${GENPLUSGX_CORE_ROOT}/sound/tremor/window.c
  )
endif()

add_library(genplusgx_core_host OBJECT
  c_api/desktop_core_cheats.c
  c_api/desktop_core_debug.c
  c_api/desktop_core_host.c
)
target_link_libraries(
  genplusgx_core_host
  PRIVATE genplusgx_project_options genplusgx_project_warnings
)
target_include_directories(
  genplusgx_core_host
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/c_api
    ${GENPLUSGX_CORE_ROOT}
    ${GENPLUSGX_CORE_ROOT}/z80
    ${GENPLUSGX_CORE_ROOT}/m68k
    ${GENPLUSGX_CORE_ROOT}/input_hw
    ${GENPLUSGX_CORE_ROOT}/ntsc
    ${GENPLUSGX_CORE_ROOT}/sound
    ${GENPLUSGX_CORE_ROOT}/sound/minimp3
    ${GENPLUSGX_CORE_ROOT}/sound/tremor
    ${GENPLUSGX_CORE_ROOT}/cd_hw
    ${GENPLUSGX_CORE_ROOT}/cart_hw
    ${GENPLUSGX_CORE_ROOT}/cart_hw/svp
)

add_library(genplusgx_core STATIC ${GENPLUSGX_CORE_SOURCES})
add_library(GenesisPlusGX::Core ALIAS genplusgx_core)
target_sources(genplusgx_core PRIVATE $<TARGET_OBJECTS:genplusgx_core_host>)
target_include_directories(
  genplusgx_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/c_api>
)
target_include_directories(
  genplusgx_core
  SYSTEM PUBLIC
    $<BUILD_INTERFACE:${GENPLUSGX_CORE_ROOT}>
    $<BUILD_INTERFACE:${GENPLUSGX_CORE_ROOT}/z80>
    $<BUILD_INTERFACE:${GENPLUSGX_CORE_ROOT}/m68k>
    $<BUILD_INTERFACE:${GENPLUSGX_CORE_ROOT}/ntsc>
    $<BUILD_INTERFACE:${GENPLUSGX_CORE_ROOT}/sound>
    $<BUILD_INTERFACE:${GENPLUSGX_CORE_ROOT}/sound/minimp3>
    $<BUILD_INTERFACE:${GENPLUSGX_CORE_ROOT}/sound/tremor>
    $<BUILD_INTERFACE:${GENPLUSGX_CORE_ROOT}/input_hw>
    $<BUILD_INTERFACE:${GENPLUSGX_CORE_ROOT}/cd_hw>
    $<BUILD_INTERFACE:${GENPLUSGX_CORE_ROOT}/cart_hw>
    $<BUILD_INTERFACE:${GENPLUSGX_CORE_ROOT}/cart_hw/svp>
)
target_compile_definitions(
  genplusgx_core
  PUBLIC
    MAXROMSIZE=33554432
    USE_16BPP_RENDERING
    HAVE_YM3438_CORE
    HAVE_OPLL_CORE
    ENABLE_SUB_68K_ADDRESS_ERROR_EXCEPTIONS
)

include(TestBigEndian)
test_big_endian(GENPLUSGX_BIG_ENDIAN)
if(NOT GENPLUSGX_BIG_ENDIAN)
  target_compile_definitions(genplusgx_core PUBLIC LSB_FIRST)
  target_compile_definitions(genplusgx_core_host PRIVATE LSB_FIRST)
endif()

if(GENPLUSGX_ENABLE_TREMOR)
  target_compile_definitions(genplusgx_core PUBLIC USE_LIBTREMOR)
endif()

if(GENPLUSGX_ENABLE_CHD)
  add_library(
    genplusgx_chd STATIC
    ${GENPLUSGX_LIBCHDR_ROOT}/src/libchdr_bitstream.c
    ${GENPLUSGX_LIBCHDR_ROOT}/src/libchdr_cdrom.c
    ${GENPLUSGX_LIBCHDR_ROOT}/src/libchdr_chd.c
    ${GENPLUSGX_LIBCHDR_ROOT}/src/libchdr_flac.c
    ${GENPLUSGX_LIBCHDR_ROOT}/src/libchdr_huffman.c
    ${GENPLUSGX_LIBCHDR_ROOT}/deps/lzma-24.05/src/LzFind.c
    ${GENPLUSGX_LIBCHDR_ROOT}/deps/lzma-24.05/src/LzmaDec.c
    ${GENPLUSGX_LIBCHDR_ROOT}/deps/lzma-24.05/src/LzmaEnc.c
    ${GENPLUSGX_LIBCHDR_ROOT}/deps/lzma-24.05/src/CpuArch.c
    ${GENPLUSGX_LIBCHDR_ROOT}/deps/zstd-1.5.6/lib/decompress/zstd_decompress.c
    ${GENPLUSGX_LIBCHDR_ROOT}/deps/zstd-1.5.6/lib/decompress/huf_decompress.c
    ${GENPLUSGX_LIBCHDR_ROOT}/deps/zstd-1.5.6/lib/decompress/zstd_decompress_block.c
    ${GENPLUSGX_LIBCHDR_ROOT}/deps/zstd-1.5.6/lib/decompress/zstd_ddict.c
    ${GENPLUSGX_LIBCHDR_ROOT}/deps/zstd-1.5.6/lib/common/entropy_common.c
    ${GENPLUSGX_LIBCHDR_ROOT}/deps/zstd-1.5.6/lib/common/error_private.c
    ${GENPLUSGX_LIBCHDR_ROOT}/deps/zstd-1.5.6/lib/common/fse_decompress.c
    ${GENPLUSGX_LIBCHDR_ROOT}/deps/zstd-1.5.6/lib/common/xxhash.c
    ${GENPLUSGX_LIBCHDR_ROOT}/deps/zstd-1.5.6/lib/common/zstd_common.c
    ${GENPLUSGX_LIBCHDR_ROOT}/deps/zlib-1.3.1/adler32.c
    ${GENPLUSGX_LIBCHDR_ROOT}/deps/zlib-1.3.1/inffast.c
    ${GENPLUSGX_LIBCHDR_ROOT}/deps/zlib-1.3.1/inflate.c
    ${GENPLUSGX_LIBCHDR_ROOT}/deps/zlib-1.3.1/inftrees.c
    ${GENPLUSGX_LIBCHDR_ROOT}/deps/zlib-1.3.1/zutil.c
  )
  target_include_directories(
    genplusgx_chd
    PRIVATE
      ${GENPLUSGX_LIBCHDR_ROOT}/src
      ${GENPLUSGX_LIBCHDR_ROOT}/include
      ${GENPLUSGX_LIBCHDR_ROOT}/deps/lzma-24.05/include
      ${GENPLUSGX_LIBCHDR_ROOT}/deps/zlib-1.3.1
      ${GENPLUSGX_LIBCHDR_ROOT}/deps/zstd-1.5.6/lib
      ${GENPLUSGX_LIBCHDR_ROOT}/deps/zstd-1.5.6/lib/common
  )
  target_compile_definitions(genplusgx_chd PRIVATE Z7_ST ZSTD_DISABLE_ASM)
  if(MSVC)
    target_compile_definitions(genplusgx_chd PRIVATE _CRT_SECURE_NO_WARNINGS)
  elseif(NOT APPLE)
    # Apple's sysctl headers require BSD integer aliases hidden by strict POSIX
    # feature selection. The macOS SDK already supplies the APIs libchdr needs.
    target_compile_definitions(genplusgx_chd PRIVATE _POSIX_C_SOURCE=200809L)
  endif()
  set_target_properties(
    genplusgx_chd PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
  )
  target_compile_definitions(genplusgx_core PUBLIC USE_LIBCHDR)
  target_include_directories(
    genplusgx_core SYSTEM PUBLIC ${GENPLUSGX_LIBCHDR_ROOT}/include
  )
  target_link_libraries(genplusgx_core PRIVATE genplusgx_chd)
endif()

if(MSVC)
  # Upstream uses GCC's __inline__ spelling through its INLINE macro. Keep the
  # compatibility adaptation target-local so core sources remain mergeable.
  target_compile_definitions(
    genplusgx_core PRIVATE _CRT_SECURE_NO_WARNINGS __inline__=__inline
  )
  target_compile_definitions(
    genplusgx_core_host PRIVATE _CRT_SECURE_NO_WARNINGS
  )
  # The inherited Musashi state embeds MSVC's deliberately aligned jmp_buf.
  # C4324 reports that intentional ABI padding when included by the host shim.
  target_compile_options(genplusgx_core_host PRIVATE /wd4324)
endif()

if(APPLE AND CMAKE_C_COMPILER_ID MATCHES "Clang")
  # Keep inherited core/vendor diagnostics separate from the frontend warning gate.
  # -fno-common also prevents Apple's linker from inferring oversized alignment for
  # large tentative core globals on arm64.
  target_compile_options(
    genplusgx_core
    PRIVATE
      $<$<COMPILE_LANGUAGE:C>:-fno-common>
      $<$<COMPILE_LANGUAGE:C>:-Wno-absolute-value>
      $<$<COMPILE_LANGUAGE:C>:-Wno-typedef-redefinition>
  )
  if(TARGET genplusgx_chd)
    # Bundled LZMA and zlib headers both use the conventional Byte typedef.
    target_compile_options(
      genplusgx_chd
      PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wno-typedef-redefinition>
    )
  endif()
endif()

if(UNIX)
  target_compile_definitions(genplusgx_core PRIVATE HAVE_ALLOCA_H)
  target_link_libraries(genplusgx_core PRIVATE m)
endif()

set_target_properties(
  genplusgx_core
  PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    OUTPUT_NAME genplusgx_core
)

add_library(genplusgx_game_files STATIC src/game_file.cpp)
add_library(GenesisPlusGX::GameFiles ALIAS genplusgx_game_files)
target_link_libraries(
  genplusgx_game_files
  PUBLIC GenesisPlusGX::FrontendTypes
  PRIVATE genplusgx_project_warnings
)
if(GENPLUSGX_ENABLE_CHD)
  target_compile_definitions(genplusgx_game_files PUBLIC GENPLUSGX_HAVE_CHD=1)
endif()

add_library(
  genplusgx_core_adapter STATIC
  include/genplusgx/core_debug.h
  include/genplusgx/core_firmware_settings.h
  include/genplusgx/core_input_settings.h
  src/core_debug.cpp
  src/core_adapter.cpp
  src/emulation_worker.cpp
)
add_library(GenesisPlusGX::CoreAdapter ALIAS genplusgx_core_adapter)
target_link_libraries(
  genplusgx_core_adapter
  PUBLIC
    GenesisPlusGX::Audio
    GenesisPlusGX::Core
    GenesisPlusGX::FrontendTypes
    GenesisPlusGX::Timing
    GenesisPlusGX::VideoExchange
  PRIVATE
    GenesisPlusGX::GameFiles
    genplusgx_project_warnings
)
set_target_properties(
  genplusgx_core_adapter
  PROPERTIES OUTPUT_NAME genplusgx_core_adapter
)
