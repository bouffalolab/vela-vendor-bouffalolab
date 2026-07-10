/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_cache.c
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

#include "bl616cl_cache.h"
#include "bl616cl_cpu.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_CSR_MHCR             0x7c1

#define BL616CL_MHCR_IE              (1u << 0)
#define BL616CL_MHCR_DE              (1u << 1)
#define BL616CL_MHCR_WB              (1u << 2)
#define BL616CL_MHCR_WA              (1u << 3)
#define BL616CL_MHCR_RS              (1u << 4)
#define BL616CL_MHCR_BPE             (1u << 5)
#define BL616CL_MHCR_L0BTB           (1u << 12)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline void bl616cl_dsb(void)
{
  __asm__ __volatile__(".word 0x0ff0000f" : : : "memory");
}

static inline void bl616cl_isb(void)
{
  __asm__ __volatile__(".word 0x0000100f" : : : "memory");
}

static inline void bl616cl_icache_invalidate_all(void)
{
  __asm__ __volatile__(".word 0x0100000b" : : : "memory");
}

static inline void bl616cl_dcache_invalidate_all(void)
{
  __asm__ __volatile__(".word 0x0020000b" : : : "memory");
}

static inline void bl616cl_dcache_clean_all(void)
{
  __asm__ __volatile__(".word 0x0010000b" : : : "memory");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_cache_early_init
 ****************************************************************************/

void bl616cl_cache_early_init(void)
{
  uint32_t value;

  bl616cl_dsb();
  bl616cl_isb();
  bl616cl_dcache_invalidate_all();

  value = bl616cl_csr_read(BL616CL_CSR_MHCR);
  value |= BL616CL_MHCR_DE | BL616CL_MHCR_WB | BL616CL_MHCR_WA |
           BL616CL_MHCR_RS | BL616CL_MHCR_BPE | BL616CL_MHCR_L0BTB;
  bl616cl_csr_write(BL616CL_CSR_MHCR, value);

  bl616cl_dsb();
  bl616cl_isb();

  bl616cl_dsb();
  bl616cl_isb();
  bl616cl_icache_invalidate_all();

  value = bl616cl_csr_read(BL616CL_CSR_MHCR);
  value |= BL616CL_MHCR_IE;
  bl616cl_csr_write(BL616CL_CSR_MHCR, value);

  bl616cl_dsb();
  bl616cl_isb();
}

/****************************************************************************
 * Name: bl616cl_cache_after_load
 ****************************************************************************/

void bl616cl_cache_after_load(void)
{
  bl616cl_dsb();
  bl616cl_dcache_clean_all();
  bl616cl_dsb();

  bl616cl_dsb();
  bl616cl_isb();
  bl616cl_icache_invalidate_all();
  bl616cl_dsb();
  bl616cl_isb();
}
