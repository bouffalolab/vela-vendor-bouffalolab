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

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <nuttx/cache.h>
#include <arch/barriers.h>

#include "bl616cl_lhal.h"
#include "bflb_l1c.h"
#include "bl616cl_cache.h"
#include "bl616cl_cpu.h"
#include "hardware/bl616cl_core.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* MHCR access and bitfields come from the T-Head core headers; the cache
 * line size comes from lhal bflb_l1c.h. The E907 cache geometry stays
 * local because the SDK headers do not define it. */

#define BL616CL_ICACHE_SIZE     (32u * 1024u)
#define BL616CL_DCACHE_SIZE     (16u * 1024u)
#define BL616CL_CACHE_CHUNK_MAX \
  ((uintptr_t)INT32_MAX & ~(BFLB_CACHE_LINE_SIZE - 1u))

#ifdef CONFIG_BL616CL_CACHE
extern uint8_t __bl616cl_cache_xip_start;
extern uint8_t __bl616cl_cache_xip_end;
extern uint8_t __bl616cl_cache_ram_start;
extern uint8_t __bl616cl_cache_ram_end;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_BL616CL_CACHE
typedef void (*bl616cl_cache_range_op_t)(void *addr, uint32_t size);

static inline uintptr_t bl616cl_cache_align_down(uintptr_t value)
{
  return value & ~(BFLB_CACHE_LINE_SIZE - 1u);
}

static bool bl616cl_cache_domain_contains(uintptr_t start, uintptr_t end,
                                          uintptr_t domain_start,
                                          uintptr_t domain_end)
{
  return start >= domain_start && start < domain_end && end > start &&
         end <= domain_end;
}

static bool bl616cl_cache_ram_contains(uintptr_t start, uintptr_t end)
{
  return bl616cl_cache_domain_contains(
    start, end, (uintptr_t)&__bl616cl_cache_ram_start,
    (uintptr_t)&__bl616cl_cache_ram_end);
}

static bool bl616cl_cache_icache_contains(uintptr_t start, uintptr_t end)
{
  return bl616cl_cache_ram_contains(start, end) ||
         bl616cl_cache_domain_contains(
           start, end, (uintptr_t)&__bl616cl_cache_xip_start,
           (uintptr_t)&__bl616cl_cache_xip_end);
}

static bool bl616cl_cache_prepare_range(
  uintptr_t start, uintptr_t end, bool icache, uintptr_t *aligned_start,
  uintptr_t *aligned_end)
{
  if (end <= start)
    {
      return false;
    }

  if (end > UINTPTR_MAX - (BFLB_CACHE_LINE_SIZE - 1u))
    {
      return false;
    }

  *aligned_start = bl616cl_cache_align_down(start);
  *aligned_end = bl616cl_cache_align_down(
    end + BFLB_CACHE_LINE_SIZE - 1u);

  if ((icache && !bl616cl_cache_icache_contains(*aligned_start,
                                                *aligned_end)) ||
      (!icache && !bl616cl_cache_ram_contains(*aligned_start,
                                              *aligned_end)))
    {
      return false;
    }

  return true;
}

static void bl616cl_cache_range_operation(
  uintptr_t start, uintptr_t end, bl616cl_cache_range_op_t operation)
{
  uintptr_t chunk;

  while (start < end)
    {
      chunk = end - start;
      if (chunk > BL616CL_CACHE_CHUNK_MAX)
        {
          chunk = BL616CL_CACHE_CHUNK_MAX;
        }

      operation((void *)start, (uint32_t)chunk);

      start += chunk;
    }
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_cache_early_init
 ****************************************************************************/

void bl616cl_cache_early_init(void)
{
  uint32_t value;

  UP_DSB();
  UP_ISB();
  __DCACHE_IALL();

  value = __get_MHCR();
  value |= CACHE_MHCR_DE_Msk | CACHE_MHCR_WB_Msk | CACHE_MHCR_WA_Msk |
           CACHE_MHCR_RS_Msk | CACHE_MHCR_BPE_Msk | CACHE_MHCR_L0BTB_Msk;
  __set_MHCR(value);

  UP_DSB();
  UP_ISB();

  UP_DSB();
  UP_ISB();
  __ICACHE_IALL();

  value = __get_MHCR();
  value |= CACHE_MHCR_IE_Msk;
  __set_MHCR(value);

  UP_DSB();
  UP_ISB();
}

/****************************************************************************
 * Name: bl616cl_cache_after_load
 ****************************************************************************/

void bl616cl_cache_after_load(void)
{
  UP_DSB();
  __DCACHE_CALL();
  UP_DSB();

  UP_DSB();
  UP_ISB();
  __ICACHE_IALL();
  UP_DSB();
  UP_ISB();
}

#ifdef CONFIG_BL616CL_CACHE
size_t up_get_icache_linesize(void)
{
  return BFLB_CACHE_LINE_SIZE;
}

size_t up_get_icache_size(void)
{
  return BL616CL_ICACHE_SIZE;
}

void up_enable_icache(void)
{
  if ((__get_MHCR() & CACHE_MHCR_IE_Msk) == 0)
    {
      bflb_l1c_icache_enable();
    }
}

void up_disable_icache(void)
{
  if ((__get_MHCR() & CACHE_MHCR_IE_Msk) != 0)
    {
      bflb_l1c_icache_disable();
    }
}

void up_invalidate_icache(uintptr_t start, uintptr_t end)
{
  uintptr_t aligned_start;
  uintptr_t aligned_end;

  if (bl616cl_cache_prepare_range(start, end, true, &aligned_start,
                                  &aligned_end))
    {
      bl616cl_cache_range_operation(aligned_start, aligned_end,
                                    bflb_l1c_icache_invalid_range);
    }
}

void up_invalidate_icache_all(void)
{
  bflb_l1c_icache_invalid_all();
}

size_t up_get_dcache_linesize(void)
{
  return BFLB_CACHE_LINE_SIZE;
}

size_t up_get_dcache_size(void)
{
  return BL616CL_DCACHE_SIZE;
}

void up_enable_dcache(void)
{
  if ((__get_MHCR() & CACHE_MHCR_DE_Msk) == 0)
    {
      bflb_l1c_dcache_enable();
    }
}

void up_disable_dcache(void)
{
  if ((__get_MHCR() & CACHE_MHCR_DE_Msk) != 0)
    {
      bflb_l1c_dcache_disable();
    }
}

void up_clean_dcache(uintptr_t start, uintptr_t end)
{
  uintptr_t aligned_start;
  uintptr_t aligned_end;

  if (bl616cl_cache_prepare_range(start, end, false, &aligned_start,
                                  &aligned_end))
    {
      bl616cl_cache_range_operation(aligned_start, aligned_end,
                                    bflb_l1c_dcache_clean_range);
    }
}

void up_clean_dcache_all(void)
{
  bflb_l1c_dcache_clean_all();
}

void up_invalidate_dcache(uintptr_t start, uintptr_t end)
{
  uintptr_t aligned_start;
  uintptr_t aligned_end;
  uintptr_t first_full;
  uintptr_t last_full_end;

  if (!bl616cl_cache_prepare_range(start, end, false, &aligned_start,
                                   &aligned_end))
    {
      return;
    }

  if (aligned_end - aligned_start == BFLB_CACHE_LINE_SIZE)
    {
      bl616cl_cache_range_op_t operation;

      operation = start == aligned_start && end == aligned_end ?
                    bflb_l1c_dcache_invalidate_range :
                    bflb_l1c_dcache_clean_invalidate_range;
      bl616cl_cache_range_operation(aligned_start, aligned_end, operation);
      return;
    }

  first_full = aligned_start;
  if (start != aligned_start)
    {
      bl616cl_cache_range_operation(
        aligned_start, aligned_start + BFLB_CACHE_LINE_SIZE,
        bflb_l1c_dcache_clean_invalidate_range);
      first_full += BFLB_CACHE_LINE_SIZE;
    }

  last_full_end = aligned_end;
  if (end != aligned_end)
    {
      last_full_end -= BFLB_CACHE_LINE_SIZE;
    }

  if (first_full < last_full_end)
    {
      bl616cl_cache_range_operation(first_full, last_full_end,
                                    bflb_l1c_dcache_invalidate_range);
    }

  if (end != aligned_end)
    {
      bl616cl_cache_range_operation(
        last_full_end, aligned_end, bflb_l1c_dcache_clean_invalidate_range);
    }
}

void up_invalidate_dcache_all(void)
{
  bflb_l1c_dcache_invalidate_all();
}

void up_flush_dcache(uintptr_t start, uintptr_t end)
{
  uintptr_t aligned_start;
  uintptr_t aligned_end;

  if (bl616cl_cache_prepare_range(start, end, false, &aligned_start,
                                  &aligned_end))
    {
      bl616cl_cache_range_operation(
        aligned_start, aligned_end,
        bflb_l1c_dcache_clean_invalidate_range);
    }
}

void up_flush_dcache_all(void)
{
  bflb_l1c_dcache_clean_invalidate_all();
}

void up_coherent_dcache(uintptr_t addr, size_t len)
{
  uintptr_t aligned_start;
  uintptr_t aligned_end;
  uintptr_t end;

  if (len == 0)
    {
      return;
    }

  if (len > UINTPTR_MAX - addr)
    {
      return;
    }

  end = addr + len;
  if (!bl616cl_cache_prepare_range(addr, end, false, &aligned_start,
                                   &aligned_end))
    {
      return;
    }

  bl616cl_cache_range_operation(aligned_start, aligned_end,
                                bflb_l1c_dcache_clean_range);
  bl616cl_cache_range_operation(aligned_start, aligned_end,
                                bflb_l1c_icache_invalid_range);
}
#endif
