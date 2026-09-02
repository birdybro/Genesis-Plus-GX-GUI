#ifndef GENPLUSGX_DESKTOP_CORE_DEBUG_H
#define GENPLUSGX_DESKTOP_CORE_DEBUG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
  GENPLUSGX_DEBUG_ROM = 0,
  GENPLUSGX_DEBUG_M68K_RAM = 1,
  GENPLUSGX_DEBUG_Z80_RAM = 2,
  GENPLUSGX_DEBUG_VRAM = 3,
  GENPLUSGX_DEBUG_CRAM = 4,
  GENPLUSGX_DEBUG_VSRAM = 5,
  GENPLUSGX_DEBUG_VDP_REGISTERS = 6,
};

enum
{
  GENPLUSGX_DEBUG_CPU_M68K = 0,
  GENPLUSGX_DEBUG_CPU_Z80 = 1,
};

enum
{
  GENPLUSGX_DEBUG_TRACE_M68K = 1U << GENPLUSGX_DEBUG_CPU_M68K,
  GENPLUSGX_DEBUG_TRACE_Z80 = 1U << GENPLUSGX_DEBUG_CPU_Z80,
  GENPLUSGX_DEBUG_TRACE_ALL = GENPLUSGX_DEBUG_TRACE_M68K |
    GENPLUSGX_DEBUG_TRACE_Z80,
  GENPLUSGX_DEBUG_TRACE_CAPACITY = 4096,
};

typedef struct
{
  uint32_t data[8];
  uint32_t address[8];
  uint32_t program_counter;
  uint32_t status;
  uint32_t user_stack_pointer;
  uint32_t interrupt_stack_pointer;
} genplusgx_debug_m68k_registers;

typedef struct
{
  uint16_t af, bc, de, hl;
  uint16_t af_alternate, bc_alternate, de_alternate, hl_alternate;
  uint16_t ix, iy, stack_pointer, program_counter;
  uint8_t interrupt_vector, refresh, interrupt_mode;
  uint8_t interrupt_flip_flop_1, interrupt_flip_flop_2, halted;
  uint32_t bank;
} genplusgx_debug_z80_registers;

typedef struct
{
  genplusgx_debug_m68k_registers m68k;
  genplusgx_debug_z80_registers z80;
  uint32_t hardware;
  uint32_t rom_size;
  uint8_t m68k_active;
  uint8_t vdp_registers[0x20];
  uint16_t vdp_status;
  uint32_t dma_length;
  uint32_t dma_source;
  uint8_t dma_type;
  uint16_t horizontal_counter;
  uint16_t vertical_counter;
  uint8_t pal;
  uint8_t interlaced;
  uint8_t odd_field;
  uint8_t vram[0x10000];
  uint16_t cram[0x40];
  uint16_t vsram[0x40];
  uint8_t sprite_table[0x400];
  uint8_t fm_registers[2][0x100];
  int32_t psg_registers[8];
  uint16_t input_buttons[8];
  int16_t input_analog[8][2];
  uint8_t m68k_ram[0x10000];
  uint8_t z80_ram[0x2000];
} genplusgx_debug_snapshot;

typedef struct
{
  uint32_t m68k;
  uint16_t z80;
  uint8_t m68k_active;
} genplusgx_debug_program_counters;

typedef struct
{
  uint64_t sequence;
  uint32_t address;
  uint32_t cycles;
  uint8_t cpu;
} genplusgx_debug_trace_entry;

typedef struct
{
  uint32_t before_address;
  uint32_t after_address;
  uint32_t cycles;
  uint8_t cpu;
} genplusgx_debug_step_result;

int genplusgx_debug_capture(genplusgx_debug_snapshot *output);
int genplusgx_debug_get_program_counters(
  genplusgx_debug_program_counters *output);
size_t genplusgx_debug_region_size(unsigned int region);
int genplusgx_debug_read_region(
  unsigned int region,
  uint32_t offset,
  uint8_t *output,
  size_t size);
int genplusgx_debug_write_region(
  unsigned int region,
  uint32_t offset,
  const uint8_t *data,
  size_t size);
int genplusgx_debug_set_m68k_registers(
  const genplusgx_debug_m68k_registers *registers);
int genplusgx_debug_set_z80_registers(
  const genplusgx_debug_z80_registers *registers);
int genplusgx_debug_set_vdp_register(unsigned int index, uint8_t value);
int genplusgx_debug_step_instruction(
  unsigned int cpu,
  genplusgx_debug_step_result *output);
int genplusgx_debug_configure_trace(unsigned int cpu_mask, int clear);
size_t genplusgx_debug_take_trace(
  genplusgx_debug_trace_entry *output,
  size_t capacity,
  uint64_t *dropped);
void genplusgx_debug_reset_trace(void);

#ifdef __cplusplus
}
#endif

#endif
