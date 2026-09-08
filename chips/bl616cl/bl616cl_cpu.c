/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_cpu.c
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

#include "rv_pmp.h"

#include "bl616cl_cpu.h"
#include "hardware/bl616cl_core.h"
#ifdef CONFIG_BL616CL_PSRAM
#include "include/bl616cl_psram.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_MXSTATUS_MM           (1u << 15)
#define BL616CL_MXSTATUS_THEADISAEE   (1u << 22)
#define BL616CL_MEXSTATUS_SPUSHEN     (1u << 16)
#define BL616CL_MEXSTATUS_SPSWAPEN    (1u << 17)

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const pmp_config_entry_t g_bl616cl_pmp_entry[] =
{
#ifdef CONFIG_BL616CL_PSRAM
  [7] =
  {
    .entry_flag = ENTRY_FLAG_ADDR_NAPOT | ENTRY_FLAG_M_MODE_L |
                  ENTRY_FLAG_PERM_R | ENTRY_FLAG_PERM_W,
    .entry_pa_base = BL616CL_PSRAM_NOCACHE_BASE,
    .entry_pa_length = PMP_REG_SZ_16M,
  },
#endif
  [8] =
  {
    .entry_flag = ENTRY_FLAG_ADDR_NAPOT | ENTRY_FLAG_M_MODE_L |
                  ENTRY_FLAG_PERM_R | ENTRY_FLAG_PERM_W |
                  ENTRY_FLAG_PERM_X,
    .entry_pa_base = 0x20000000,
    .entry_pa_length = PMP_REG_SZ_256M,
  },
  [9] =
  {
    .entry_flag = ENTRY_FLAG_ADDR_NAPOT | ENTRY_FLAG_M_MODE_L |
                  ENTRY_FLAG_PERM_R | ENTRY_FLAG_PERM_W |
                  ENTRY_FLAG_PERM_X,
    .entry_pa_base = 0x60000000,
    .entry_pa_length = PMP_REG_SZ_32M,
  },
  [10] =
  {
    .entry_flag = ENTRY_FLAG_ADDR_NAPOT | ENTRY_FLAG_M_MODE_L |
                  ENTRY_FLAG_PERM_R | ENTRY_FLAG_PERM_X,
    .entry_pa_base = 0x90000000,
    .entry_pa_length = PMP_REG_SZ_256K,
  },
  [11] =
  {
    .entry_flag = ENTRY_FLAG_ADDR_NAPOT | ENTRY_FLAG_M_MODE_L |
                  ENTRY_FLAG_PERM_R | ENTRY_FLAG_PERM_X,
    .entry_pa_base = 0x80000000,
    .entry_pa_length = PMP_REG_SZ_64M,
  },
#ifdef CONFIG_BL616CL_PSRAM
  [12] =
  {
    .entry_flag = ENTRY_FLAG_ADDR_NAPOT | ENTRY_FLAG_M_MODE_L |
                  ENTRY_FLAG_PERM_R | ENTRY_FLAG_PERM_W |
                  ENTRY_FLAG_PERM_X,
    .entry_pa_base = BL616CL_PSRAM_BASE,
    .entry_pa_length = PMP_REG_SZ_16M,
  },
#endif
  [13] =
  {
    .entry_flag = ENTRY_FLAG_ADDR_NAPOT | ENTRY_FLAG_M_MODE_L |
                  ENTRY_FLAG_PERM_R | ENTRY_FLAG_PERM_W,
    .entry_pa_base = 0xe0000000,
    .entry_pa_length = PMP_REG_SZ_256M,
  },
  [14] =
  {
    .entry_flag = ENTRY_FLAG_ADDR_NAPOT | ENTRY_FLAG_M_MODE_L |
                  ENTRY_FLAG_PERM_R,
    .entry_pa_base = 0xf0000000,
    .entry_pa_length = PMP_REG_SZ_128B,
  },
  [15] =
  {
    .entry_flag = ENTRY_FLAG_ADDR_NAPOT | ENTRY_FLAG_M_MODE_L,
    .entry_pa_base = 0x00000000,
    .entry_pa_length = PMP_REG_SZ_4G,
  },
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_thead_cpu_init
 ****************************************************************************/

void bl616cl_thead_cpu_init(void)
{
  uint32_t value;

  value = __get_MXSTATUS();
  value |= BL616CL_MXSTATUS_THEADISAEE | BL616CL_MXSTATUS_MM;
  __set_MXSTATUS(value);

  value = __get_MHCR();
  value |= CACHE_MHCR_RS_Msk;
  __set_MHCR(value);

  value = __get_MEXSTATUS();
  value &= ~(BL616CL_MEXSTATUS_SPUSHEN | BL616CL_MEXSTATUS_SPSWAPEN);
  __set_MEXSTATUS(value);
}

/****************************************************************************
 * Name: bl616cl_pmp_init
 ****************************************************************************/

void bl616cl_pmp_init(void)
{
#if !defined(CONFIG_PMP_NO_INIT)
  rvpmp_init(g_bl616cl_pmp_entry,
             sizeof(g_bl616cl_pmp_entry) / sizeof(g_bl616cl_pmp_entry[0]));
#endif
}
