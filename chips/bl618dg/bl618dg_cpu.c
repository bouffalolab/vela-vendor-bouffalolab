/****************************************************************************
 * vendor/bouffalolab/chips/bl618dg/bl618dg_cpu.c
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

#include <nuttx/arch.h>

#include "riscv_internal.h"
#include "hardware/bl618dg_core.h"
#include "hardware/bl618dg.h"
#include "hardware/glb_reg.h"
#include "hardware/sf_ctrl_reg.h"
#include "hardware/tzc_sec_reg.h"

#include "bl618dg_cpu.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Nuclei AP map from the SDK mn startup. The aliases distinguish device,
 * uncached and cached accesses. Unused PSRAM windows stay inaccessible.
 * Keep the catch-all deny entry last, at the lowest priority.
 */

static const pmp_config g_bl618dg_pmp_entry[] =
{
  {
    .protection = PMP_L | PMP_R | PMP_W,
    .base_addr = 0x00010000,
    .order = 16,
  },
  {
    .protection = PMP_L | PMP_R | PMP_W,
    .base_addr = 0x00080000,
    .order = 19,
  },
  {
    .protection = PMP_L | PMP_R | PMP_W,
    .base_addr = 0x18000000,
    .order = 22,
  },
  {
    .protection = PMP_L | PMP_R | PMP_W,
    .base_addr = 0x20000000,
    .order = 28,
  },
  {
    .protection = PMP_L | PMP_R | PMP_W,
    .base_addr = 0x30000000,
    .order = 26,
  },
  {
    .protection = PMP_L | PMP_R | PMP_W | PMP_X,
    .base_addr = 0x40010000,
    .order = 16,
  },
  {
    .protection = PMP_L | PMP_R | PMP_W | PMP_X,
    .base_addr = 0x60000000,
    .order = 25,
  },
  {
    .protection = PMP_L | PMP_R | PMP_W | PMP_X,
    .base_addr = 0x70000000,
    .order = 26,
  },
  {
    .protection = PMP_L | PMP_R | PMP_W | PMP_X,
    .base_addr = 0x80010000,
    .order = 16,
  },
  {
    .protection = PMP_L | PMP_R | PMP_W | PMP_X,
    .base_addr = 0xa0000000,
    .order = 25,
  },
  {
    .protection = PMP_L | PMP_R | PMP_W | PMP_X,
    .base_addr = 0xb0000000,
    .order = 26,
  },
  {
    .protection = PMP_L | PMP_R,
    .base_addr = 0xf0000000,
    .order = 12,
  },
  {
    .protection = PMP_L,
    .base_addr = 0x00000000,
    .order = 32,
  },
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl618dg_cpu_init
 *
 * Description:
 *   Establish the SDK Nuclei AP PMP map and bus access before enabling
 *   caches or loading sections. BootROM/Boot2 provide the Flash mapping;
 *   OpenVela owns the core protection map and the AP runtime. NP and LP
 *   are not started. No initialized RAM data is used here.
 *
 * Returned Value:
 *   Zero on success, -1 if a PMP entry cannot be programmed as required.
 *
 ****************************************************************************/

int bl618dg_cpu_init(void)
{
  unsigned int count = sizeof(g_bl618dg_pmp_entry) /
                       sizeof(g_bl618dg_pmp_entry[0]);
  uint32_t regval;
  uint32_t mask;
  uint32_t address;
  unsigned int slot;
  unsigned int i;

  /* Lower-numbered entries have priority. Remove stale loader entries;
   * refuse to continue if a loader locked an incompatible protection map.
   */

  for (i = 0; i < __PMP_ENTRY_NUM - count; i++)
    {
      __set_PMPxCFG(i, 0);
      if (__get_PMPxCFG(i) != 0)
        {
          return -1;
        }
    }

  for (i = 0; i < count; i++)
    {
      slot = __PMP_ENTRY_NUM - count + i;
      __set_PMPENTRYx(slot, &g_bl618dg_pmp_entry[i]);
      mask = (1u << (g_bl618dg_pmp_entry[i].order - PMP_SHIFT)) - 1;
      address = ((g_bl618dg_pmp_entry[i].base_addr >> PMP_SHIFT) &
                 ~mask) | (mask >> 1);
      if (__get_PMPxCFG(slot) !=
          (g_bl618dg_pmp_entry[i].protection | PMP_A_NAPOT) ||
          __get_PMPADDRx(slot) != address)
        {
          return -1;
        }
    }

  /* PSRAM is unused. Apply the SDK prefetch barrier to its external port
   * without locking it, so a later PSRAM driver can configure the port.
   */

  regval = getreg32(TZC_SEC_BASE + TZC_SEC_TZC_PSRAMB_TZSRG_CTRL_OFFSET);
  regval &= ~3u;
  putreg32(regval, TZC_SEC_BASE + TZC_SEC_TZC_PSRAMB_TZSRG_CTRL_OFFSET);
  putreg32(0x0000ffff,
           TZC_SEC_BASE + TZC_SEC_TZC_PSRAMB_TZSRG_R0_OFFSET);
  putreg32(regval | (1u << 16),
           TZC_SEC_BASE + TZC_SEC_TZC_PSRAMB_TZSRG_CTRL_OFFSET);

  /* Enable bank-2 access as in the AP SystemInit(), before Flash users. */

  modifyreg32(SF_CTRL_BASE + SF_CTRL_2_OFFSET,
              0, SF_CTRL_SF_IF_BK2_MODE | SF_CTRL_SF_IF_BK2_EN);

  /* Flat AP access, with LP held in reset and interrupt forwarding masked. */

  putreg32(0, GLB_BASE + GLB_BMX_CFG7_OFFSET);
  modifyreg32(GLB_BASE + GLB_SWRST_CFG2_OFFSET, 0, 1u << 3);
  putreg32(UINT32_MAX, GLB_BASE + GLB_CORE_CFG18_OFFSET);
  putreg32(UINT32_MAX, GLB_BASE + GLB_CORE_CFG19_OFFSET);
  putreg32(UINT32_MAX, GLB_BASE + GLB_CORE_CFG20_OFFSET);
  putreg32(UINT32_MAX, GLB_BASE + GLB_CORE_CFG21_OFFSET);
  putreg32(UINT32_MAX, GLB_BASE + GLB_CORE_CFG22_OFFSET);
  putreg32(UINT32_MAX, GLB_BASE + GLB_CORE_CFG23_OFFSET);
  return 0;
}
