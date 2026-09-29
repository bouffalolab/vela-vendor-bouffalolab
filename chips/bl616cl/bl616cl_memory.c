/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_memory.c
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

#include <stdint.h>

#include "riscv_internal.h"

#include "bl616cl_memory.h"
#include "bl616cl_sdk.h"
#include "bl616cl_glb.h"
#include "bl616cl_psram.h"
#include "tzc_sec_reg.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

extern uint8_t __LD_CONFIG_EM_SEL;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_psramb_tzc_access_not_lock
 *
 * Description:
 *   Program one PSRAMB TZC region without locking it: set its group in the
 *   control register, write its start and end (1 KiB granularity) and enable
 *   it.
 *
 * Input Parameters:
 *   region - TZC region index
 *   start - Region start address
 *   end - Region end address (exclusive)
 *   group - Access group encoded in two bits for the region
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: bl616cl_em_select
 *
 * Description:
 *   Choose the WRAM/EM (exchange memory) split from the linker-provided
 *   __LD_CONFIG_EM_SEL size: 16 KiB gives WRAM 144 KB, 32 KiB gives WRAM 128
 *   KB, any other value gives WRAM 160 KB with no EM.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

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
 *
 * Description:
 *   Early memory setup called at startup. If PSRAM has not been initialized
 *   yet, open the whole first 64 MiB of PSRAMB in TZC region 0 (not locked),
 *   then select the EM/WRAM split.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_memory_early_init(void)
{
  if (!BL616CL_PSRAM_INIT_DONE)
    {
      bl616cl_psramb_tzc_access_not_lock(0, 0, 64 * 1024 * 1024, 0);
    }

  bl616cl_em_select();
}
