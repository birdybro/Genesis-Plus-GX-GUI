#include "desktop_core_achievements.h"

#include "shared.h"

static uint8_t logical_byte(const uint8 *data, uint32_t offset, int word_swapped)
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

uint32_t genplusgx_achievements_console_id(void)
{
  switch (system_hw)
  {
    case SYSTEM_MD:
      return GENPLUSGX_ACHIEVEMENTS_CONSOLE_MEGA_DRIVE;
    case SYSTEM_MCD:
      return GENPLUSGX_ACHIEVEMENTS_CONSOLE_SEGA_CD;
    case SYSTEM_MARKIII:
    case SYSTEM_SMS:
    case SYSTEM_SMS2:
    case SYSTEM_PBC:
    case SYSTEM_GGMS:
      return GENPLUSGX_ACHIEVEMENTS_CONSOLE_MASTER_SYSTEM;
    case SYSTEM_GG:
      return GENPLUSGX_ACHIEVEMENTS_CONSOLE_GAME_GEAR;
    case SYSTEM_SG:
    case SYSTEM_SGII:
    case SYSTEM_SGII_RAM_EXT:
      return GENPLUSGX_ACHIEVEMENTS_CONSOLE_SG1000;
    default:
      return GENPLUSGX_ACHIEVEMENTS_CONSOLE_UNKNOWN;
  }
}

static int sg1000_memory_byte(uint32_t address, uint8_t *value)
{
  uint32_t actual;
  uint8 *mapped;
  uintptr_t mapped_address;
  uintptr_t work_ram_start;
  uintptr_t work_ram_end;
  if ((address < 0x400U) ||
      ((system_hw == SYSTEM_SGII) && (address < 0x800U)) ||
      ((system_hw == SYSTEM_SGII_RAM_EXT) && (address < 0x2000U)))
  {
    *value = work_ram[address];
    return 1;
  }

  if ((address >= 0x2000U) && (address < 0x4000U))
  {
    actual = address;
  }
  else if ((address >= 0x4000U) && (address < 0x6000U))
  {
    actual = 0x8000U + (address - 0x4000U);
  }
  else
  {
    return 0;
  }

  mapped = z80_readmap[actual >> 10] + (actual & 0x3ffU);
  mapped_address = (uintptr_t)mapped;
  work_ram_start = (uintptr_t)(work_ram + 0x2000U);
  work_ram_end = (uintptr_t)(work_ram + 0x4000U);
  if ((mapped_address < work_ram_start) || (mapped_address >= work_ram_end))
  {
    return 0;
  }
  *value = *mapped;
  return 1;
}

static int memory_byte(uint32_t address, uint8_t *value)
{
  int cart_ram_size;
  uint32_t offset;
  switch (system_hw)
  {
    case SYSTEM_MD:
      if (address < 0x10000U)
      {
        *value = logical_byte(work_ram, address, 1);
        return 1;
      }
      if (address < 0x20000U)
      {
        *value = sram.sram[address - 0x10000U];
        return 1;
      }
      return 0;

    case SYSTEM_MCD:
      if (address < 0x10000U)
      {
        *value = logical_byte(work_ram, address, 1);
        return 1;
      }
      if (address < 0x90000U)
      {
        *value = logical_byte(scd.prg_ram, address - 0x10000U, 1);
        return 1;
      }
      if (address < 0xb0000U)
      {
        offset = address - 0x90000U;
        if (scd.regs[0x03U >> 1].byte.l & 0x04U)
        {
          const unsigned int bank =
            (scd.regs[0x03U >> 1].byte.l & 0x01U) ? 1U : 0U;
          *value = logical_byte(scd.word_ram[bank], offset, 1);
        }
        else
        {
          *value = logical_byte(scd.word_ram_2M, offset, 1);
        }
        return 1;
      }
      return 0;

    case SYSTEM_MARKIII:
    case SYSTEM_SMS:
    case SYSTEM_SMS2:
    case SYSTEM_PBC:
    case SYSTEM_GG:
    case SYSTEM_GGMS:
      if (address < 0x2000U)
      {
        *value = work_ram[address];
        return 1;
      }
      cart_ram_size = sms_cart_ram_size();
      if ((address < (uint32_t)(0x2000 + cart_ram_size)) &&
          (address < 0xa000U))
      {
        *value = work_ram[address];
        return 1;
      }
      return 0;

    case SYSTEM_SG:
    case SYSTEM_SGII:
    case SYSTEM_SGII_RAM_EXT:
      return sg1000_memory_byte(address, value);

    default:
      return 0;
  }
}

size_t genplusgx_achievements_read_memory(
  uint32_t address,
  uint8_t *output,
  size_t size)
{
  size_t index;
  if ((output == NULL) || (size == 0U))
  {
    return 0U;
  }
  for (index = 0U; index < size; ++index)
  {
    if ((index > (size_t)(UINT32_MAX - address)) ||
        !memory_byte(address + (uint32_t)index, &output[index]))
    {
      break;
    }
  }
  return index;
}
