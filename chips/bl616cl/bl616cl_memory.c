/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_memory.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include "riscv_internal.h"

#include "bl616cl_memory.h"
#include "bl616cl_sdk.h"
#include "bl616cl_glb.h"
#include "tzc_sec_reg.h"
#include "hardware/sf_ctrl_reg.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_SECTION_SENTINEL           0xffffffff

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bl616cl_mem_load_section_s
{
  uint32_t *start;
  uint32_t *end;
  uint32_t *load;
};

struct bl616cl_mem_section_s
{
  uint32_t *start;
  uint32_t *end;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

extern struct bl616cl_mem_load_section_s __mem_copy_sections[];
extern struct bl616cl_mem_section_s __mem_setz_sections[];
extern uint8_t __LD_CONFIG_EM_SEL;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool bl616cl_psram_init_done(void)
{
  int i;

  for (i = 0; i < 12; i++)
    {
      if ((getreg32(GLB_BASE + GLB_GPIO_CFG46_OFFSET + (i * 4)) &
           GLB_REG_GPIO_0_IE_MSK) == 0)
        {
          return false;
        }
    }

  return true;
}

static void bl616cl_psramb_tzc_access_not_lock(uint8_t region,
                                               uint32_t start,
                                               uint32_t end,
                                               uint8_t group)
{
  uint32_t regval;
  uint32_t offset;

  offset = TZC_SEC_TZC_PSRAMB_TZSRG_R0_OFFSET +
           (region * sizeof(uint32_t));

  regval = getreg32(TZC_SEC_BASE +
                   TZC_SEC_TZC_PSRAMB_TZSRG_CTRL_OFFSET);

  /* Preserve the upstream startup's two-bit group encoding. The generated
   * ID_EN field describes four bits and is not an equivalent replacement.
   */

  regval &= ~(3u << (region * 2));
  regval |= (uint32_t)group << (region * 2);
  putreg32(regval, TZC_SEC_BASE +
           TZC_SEC_TZC_PSRAMB_TZSRG_CTRL_OFFSET);

  regval = ((((end >> 10) - 1) << TZC_SEC_TZC_PSRAMB_TZSRG_R0_END_POS) &
            TZC_SEC_TZC_PSRAMB_TZSRG_R0_END_MSK) |
           ((start >> 10) << TZC_SEC_TZC_PSRAMB_TZSRG_R0_START_POS);
  putreg32(regval, TZC_SEC_BASE + offset);

  regval = getreg32(TZC_SEC_BASE +
                   TZC_SEC_TZC_PSRAMB_TZSRG_CTRL_OFFSET);
  regval |= TZC_SEC_TZC_PSRAMB_TZSRG_R0_EN_MSK << region;
  putreg32(regval, TZC_SEC_BASE +
           TZC_SEC_TZC_PSRAMB_TZSRG_CTRL_OFFSET);
}

static void bl616cl_em_select(void)
{
  uintptr_t em_size;
  uint32_t em_sel;

  em_size = (uintptr_t)&__LD_CONFIG_EM_SEL;
  switch (em_size)
    {
      case 16 * 1024:
        em_sel = GLB_WRAM144KB_EM16KB;
        break;

      case 32 * 1024:
        em_sel = GLB_WRAM128KB_EM32KB;
        break;

      default:
        em_sel = GLB_WRAM160KB_EM0KB;
        break;
    }

  GLB_Set_EM_Sel(em_sel);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_memory_early_init
 ****************************************************************************/

void bl616cl_memory_early_init(void)
{
  if (!bl616cl_psram_init_done())
    {
      bl616cl_psramb_tzc_access_not_lock(0, 0, 64 * 1024 * 1024, 0);
    }

  bl616cl_em_select();
}

/****************************************************************************
 * Name: bl616cl_flash_early_init
 ****************************************************************************/

void bl616cl_flash_early_init(void)
{
  uint32_t regval;

  regval = getreg32(SF_CTRL_BASE + SF_CTRL_2_OFFSET);
  regval |= SF_CTRL_SF_IF_BK2_EN | SF_CTRL_SF_IF_BK2_MODE;
  putreg32(regval, SF_CTRL_BASE + SF_CTRL_2_OFFSET);
}

/****************************************************************************
 * Name: bl616cl_section_load
 ****************************************************************************/

void bl616cl_section_load(void)
{
  struct bl616cl_mem_load_section_s *copy = __mem_copy_sections;
  struct bl616cl_mem_section_s *setz = __mem_setz_sections;
  uint32_t *src;
  uint32_t *dest;
  int i;

  for (i = 0; (uintptr_t)copy[i].start != BL616CL_SECTION_SENTINEL; i++)
    {
      if (copy[i].start == NULL || copy[i].end == NULL ||
          copy[i].load == NULL)
        {
          continue;
        }

      src = copy[i].load;
      dest = copy[i].start;
      while (dest < copy[i].end)
        {
          *dest++ = *src++;
        }
    }

  for (i = 0; (uintptr_t)setz[i].start != BL616CL_SECTION_SENTINEL; i++)
    {
      if (setz[i].start == NULL || setz[i].end == NULL)
        {
          continue;
        }

      dest = setz[i].start;
      while (dest < setz[i].end)
        {
          *dest++ = 0;
        }
    }
}
