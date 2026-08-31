#include "desktop_core_debug.h"

#include "shared.h"

#include <string.h>

extern uint8 genplusgx_debug_fm_registers[2][0x100];
extern const int *genplusgx_debug_psg_registers(void);

static uint8_t genesis_hardware(void)
{
  return (uint8_t)((system_hw & SYSTEM_PBC) == SYSTEM_MD);
}

static uint8 logical_byte(const uint8 *data, uint32_t offset, int word_swapped)
{
#ifdef LSB_FIRST
  if (word_swapped)
  {
    offset ^= 1U;
  }
#else
  (void)word_swapped;
#endif
  return data[offset];
}

static void set_logical_byte(
  uint8 *data,
  uint32_t offset,
  int word_swapped,
  uint8 value)
{
#ifdef LSB_FIRST
  if (word_swapped)
  {
    offset ^= 1U;
  }
#else
  (void)word_swapped;
#endif
  data[offset] = value;
}

static uint16 native_word(const uint8 *data, uint32_t offset)
{
  uint16 value;
  memcpy(&value, data + offset, sizeof(value));
  return value;
}

static uint8 *region_data(unsigned int region, size_t *size, int *word_swapped)
{
  *word_swapped = 0;
  switch (region)
  {
    case GENPLUSGX_DEBUG_ROM:
      *size = cart.romsize;
      *word_swapped = 1;
      return cart.rom;
    case GENPLUSGX_DEBUG_M68K_RAM:
      if (!genesis_hardware())
      {
        *size = 0;
        return NULL;
      }
      *size = sizeof(work_ram);
      *word_swapped = 1;
      return work_ram;
    case GENPLUSGX_DEBUG_Z80_RAM:
      *size = sizeof(zram);
      return genesis_hardware() ? zram : work_ram;
    case GENPLUSGX_DEBUG_VRAM:
      *size = sizeof(vram);
      *word_swapped = 1;
      return vram;
    case GENPLUSGX_DEBUG_CRAM:
      *size = sizeof(cram);
      return cram;
    case GENPLUSGX_DEBUG_VSRAM:
      *size = sizeof(vsram);
      return vsram;
    case GENPLUSGX_DEBUG_VDP_REGISTERS:
      *size = sizeof(reg);
      return reg;
    default:
      *size = 0;
      return NULL;
  }
}

static void capture_m68k(genplusgx_debug_m68k_registers *output)
{
  int index;
  for (index = 0; index < 8; ++index)
  {
    output->data[index] = m68k_get_reg((m68k_register_t)(M68K_REG_D0 + index));
    output->address[index] =
      m68k_get_reg((m68k_register_t)(M68K_REG_A0 + index));
  }
  output->program_counter = m68k_get_reg(M68K_REG_PC);
  output->status = m68k_get_reg(M68K_REG_SR);
  output->user_stack_pointer = m68k_get_reg(M68K_REG_USP);
  output->interrupt_stack_pointer = m68k_get_reg(M68K_REG_ISP);
}

static void capture_z80(genplusgx_debug_z80_registers *output)
{
  output->af = Z80.af.w.l;
  output->bc = Z80.bc.w.l;
  output->de = Z80.de.w.l;
  output->hl = Z80.hl.w.l;
  output->af_alternate = Z80.af2.w.l;
  output->bc_alternate = Z80.bc2.w.l;
  output->de_alternate = Z80.de2.w.l;
  output->hl_alternate = Z80.hl2.w.l;
  output->ix = Z80.ix.w.l;
  output->iy = Z80.iy.w.l;
  output->stack_pointer = Z80.sp.w.l;
  output->program_counter = Z80.pc.w.l;
  output->interrupt_vector = Z80.i;
  output->refresh = (Z80.r & 0x7fU) | (Z80.r2 & 0x80U);
  output->interrupt_mode = Z80.im;
  output->interrupt_flip_flop_1 = Z80.iff1;
  output->interrupt_flip_flop_2 = Z80.iff2;
  output->halted = Z80.halt;
  output->bank = zbank;
}

int genplusgx_debug_capture(genplusgx_debug_snapshot *output)
{
  size_t index;
  const int *psg;
  if (output == NULL)
  {
    return 0;
  }
  memset(output, 0, sizeof(*output));
  if (genesis_hardware())
  {
    capture_m68k(&output->m68k);
  }
  capture_z80(&output->z80);
  output->hardware = system_hw;
  output->rom_size = cart.romsize;
  output->m68k_active = genesis_hardware();
  memcpy(output->vdp_registers, reg, sizeof(reg));
  output->vdp_status = status;
  output->dma_length = dma_length;
  output->dma_source =
    (((uint32)reg[21] | ((uint32)reg[22] << 8) |
      (((uint32)reg[23] & 0x7fU) << 16)) << 1) & 0xffffffU;
  output->dma_type = dma_type;
  output->horizontal_counter = h_counter;
  output->vertical_counter = v_counter;
  output->pal = vdp_pal;
  output->interlaced = interlaced;
  output->odd_field = odd_frame;
  for (index = 0; index < sizeof(output->vram); ++index)
  {
    output->vram[index] = logical_byte(vram, (uint32_t)index, 1);
  }
  for (index = 0; index < 0x40U; ++index)
  {
    output->cram[index] = native_word(cram, (uint32_t)(index * 2U));
    output->vsram[index] = native_word(vsram, (uint32_t)(index * 2U));
  }
  for (index = 0; index < sizeof(output->sprite_table); ++index)
  {
    output->sprite_table[index] = logical_byte(sat, (uint32_t)index, 1);
  }
  memcpy(output->fm_registers,
    genplusgx_debug_fm_registers, sizeof(output->fm_registers));
  psg = genplusgx_debug_psg_registers();
  for (index = 0; index < 8U; ++index)
  {
    output->psg_registers[index] = psg[index];
    output->input_buttons[index] = input.pad[index];
    output->input_analog[index][0] = input.analog[index][0];
    output->input_analog[index][1] = input.analog[index][1];
  }
  if (genesis_hardware())
  {
    for (index = 0; index < sizeof(output->m68k_ram); ++index)
    {
      output->m68k_ram[index] = logical_byte(work_ram, (uint32_t)index, 1);
    }
  }
  memcpy(output->z80_ram,
    genesis_hardware() ? zram : work_ram, sizeof(output->z80_ram));
  return 1;
}

int genplusgx_debug_get_program_counters(
  genplusgx_debug_program_counters *output)
{
  if (output == NULL)
  {
    return 0;
  }
  output->m68k = m68k_get_reg(M68K_REG_PC);
  output->z80 = Z80.pc.w.l;
  output->m68k_active = genesis_hardware();
  return 1;
}

size_t genplusgx_debug_region_size(unsigned int region)
{
  size_t size;
  int word_swapped;
  (void)region_data(region, &size, &word_swapped);
  return size;
}

int genplusgx_debug_read_region(
  unsigned int region,
  uint32_t offset,
  uint8_t *output,
  size_t size)
{
  size_t index;
  size_t region_size;
  int word_swapped;
  uint8 *data = region_data(region, &region_size, &word_swapped);
  if ((data == NULL) || ((size != 0U) && (output == NULL)) ||
      (offset > region_size) || (size > (region_size - offset)))
  {
    return 0;
  }
  for (index = 0; index < size; ++index)
  {
    output[index] = logical_byte(data, offset + (uint32_t)index, word_swapped);
  }
  return 1;
}

int genplusgx_debug_write_region(
  unsigned int region,
  uint32_t offset,
  const uint8_t *input_data,
  size_t size)
{
  size_t index;
  size_t region_size;
  int word_swapped;
  uint8 *data = region_data(region, &region_size, &word_swapped);
  if ((data == NULL) || ((size != 0U) && (input_data == NULL)) ||
      (offset > region_size) || (size > (region_size - offset)))
  {
    return 0;
  }
  for (index = 0; index < size; ++index)
  {
    set_logical_byte(
      data, offset + (uint32_t)index, word_swapped, input_data[index]);
  }
  return 1;
}

int genplusgx_debug_set_m68k_registers(
  const genplusgx_debug_m68k_registers *registers)
{
  int index;
  if (registers == NULL)
  {
    return 0;
  }
  for (index = 0; index < 8; ++index)
  {
    m68k_set_reg((m68k_register_t)(M68K_REG_D0 + index), registers->data[index]);
    m68k_set_reg(
      (m68k_register_t)(M68K_REG_A0 + index), registers->address[index]);
  }
  m68k_set_reg(M68K_REG_PC, registers->program_counter);
  m68k_set_reg(M68K_REG_SR, registers->status);
  m68k_set_reg(M68K_REG_USP, registers->user_stack_pointer);
  m68k_set_reg(M68K_REG_ISP, registers->interrupt_stack_pointer);
  return 1;
}

int genplusgx_debug_set_z80_registers(
  const genplusgx_debug_z80_registers *registers)
{
  if (registers == NULL)
  {
    return 0;
  }
  Z80.af.w.l = registers->af;
  Z80.bc.w.l = registers->bc;
  Z80.de.w.l = registers->de;
  Z80.hl.w.l = registers->hl;
  Z80.af2.w.l = registers->af_alternate;
  Z80.bc2.w.l = registers->bc_alternate;
  Z80.de2.w.l = registers->de_alternate;
  Z80.hl2.w.l = registers->hl_alternate;
  Z80.ix.w.l = registers->ix;
  Z80.iy.w.l = registers->iy;
  Z80.sp.w.l = registers->stack_pointer;
  Z80.pc.w.l = registers->program_counter;
  Z80.i = registers->interrupt_vector;
  Z80.r = registers->refresh & 0x7fU;
  Z80.r2 = registers->refresh & 0x80U;
  Z80.im = registers->interrupt_mode;
  Z80.iff1 = registers->interrupt_flip_flop_1 != 0;
  Z80.iff2 = registers->interrupt_flip_flop_2 != 0;
  Z80.halt = registers->halted != 0;
  zbank = registers->bank & 0xff8000U;
  return 1;
}

int genplusgx_debug_set_vdp_register(unsigned int index, uint8_t value)
{
  if (index >= sizeof(reg))
  {
    return 0;
  }
  reg[index] = value;
  return 1;
}
