/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_cache.c
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
#ifdef CONFIG_BL616CL_PSRAM
#include <arch/chip/bl616cl_psram.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* MHCR access and bitfields come from the T-Head core headers; the cache
 * line size comes from lhal bflb_l1c.h. The E907 cache geometry stays
 * local because the SDK headers do not define it.
 */

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

/****************************************************************************
 * Name: bl616cl_cache_align_down
 *
 * Description:
 *   Round a value down to a multiple of BFLB_CACHE_LINE_SIZE.
 *
 * Input Parameters:
 *   value - Address to align.
 *
 * Returned Value:
 *   The value aligned down to a cache line boundary.
 *
 ****************************************************************************/

static inline uintptr_t bl616cl_cache_align_down(uintptr_t value)
{
  return value & ~(BFLB_CACHE_LINE_SIZE - 1u);
}

/****************************************************************************
 * Name: bl616cl_cache_domain_contains
 *
 * Description:
 *   Check that the non-empty range [start, end) lies entirely inside the
 *   range [domain_start, domain_end).
 *
 * Input Parameters:
 *   start - Start of the range to check.
 *   end - End of the range to check (exclusive).
 *   domain_start - Start of the domain.
 *   domain_end - End of the domain (exclusive).
 *
 * Returned Value:
 *   true if the range is non-empty and inside the domain; false otherwise.
 *
 ****************************************************************************/

static bool bl616cl_cache_domain_contains(uintptr_t start, uintptr_t end,
                                          uintptr_t domain_start,
                                          uintptr_t domain_end)
{
  return start >= domain_start && start < domain_end && end > start &&
         end <= domain_end;
}

/****************************************************************************
 * Name: bl616cl_cache_ram_contains
 *
 * Description:
 *   Check that [start, end) lies inside a cacheable RAM domain: PSRAM (when
 *   CONFIG_BL616CL_PSRAM is set, sized by bl616cl_psram_size_get()) or the
 *   linker-defined __bl616cl_cache_ram region.
 *
 * Input Parameters:
 *   start - Start of the range to check.
 *   end - End of the range to check (exclusive).
 *
 * Returned Value:
 *   true if the range is inside one RAM domain; false otherwise.
 *
 ****************************************************************************/

static bool bl616cl_cache_ram_contains(uintptr_t start, uintptr_t end)
{
#ifdef CONFIG_BL616CL_PSRAM
  if (bl616cl_cache_domain_contains(start, end, BL616CL_PSRAM_BASE,
        BL616CL_PSRAM_BASE + bl616cl_psram_size_get()))
    {
      return true;
    }
#endif

  return bl616cl_cache_domain_contains(
    start, end, (uintptr_t)&__bl616cl_cache_ram_start,
    (uintptr_t)&__bl616cl_cache_ram_end);
}

/****************************************************************************
 * Name: bl616cl_cache_icache_contains
 *
 * Description:
 *   Check that [start, end) is valid for an I-cache operation, i.e. inside
 *   the RAM domain or the linker-defined XIP region (__bl616cl_cache_xip_*).
 *
 * Input Parameters:
 *   start - Start of the range to check.
 *   end - End of the range to check (exclusive).
 *
 * Returned Value:
 *   true if the range is inside a RAM or XIP domain; false otherwise.
 *
 ****************************************************************************/

static bool bl616cl_cache_icache_contains(uintptr_t start, uintptr_t end)
{
  return bl616cl_cache_ram_contains(start, end) ||
         bl616cl_cache_domain_contains(
           start, end, (uintptr_t)&__bl616cl_cache_xip_start,
           (uintptr_t)&__bl616cl_cache_xip_end);
}

/****************************************************************************
 * Name: bl616cl_cache_prepare_range
 *
 * Description:
 *   Align [start, end) outward to cache line boundaries and validate it. The
 *   range is rejected if it is empty, if aligning the end would overflow, or
 *   if it is outside the I-cache domain (icache) or RAM domain (D-cache).
 *
 * Input Parameters:
 *   start - Start address of the range.
 *   end - End address of the range (exclusive).
 *   icache - true to validate against the I-cache domain, false for D-cache.
 *   aligned_start - Location to return the line-aligned start.
 *   aligned_end - Location to return the line-aligned end.
 *
 * Returned Value:
 *   true if the range is valid and the outputs are set; false if the
 *   operation must be skipped.
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: bl616cl_cache_range_operation
 *
 * Description:
 *   Apply an lhal cache range operation to [start, end), splitting it into
 *   chunks no larger than BL616CL_CACHE_CHUNK_MAX so that each size fits the
 *   lhal 32-bit size argument.
 *
 * Input Parameters:
 *   start - Line-aligned start address.
 *   end - Line-aligned end address (exclusive).
 *   operation - lhal range operation to apply to each chunk.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

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
 *
 * Description:
 *   Enable the D-cache and I-cache with csi_dcache_enable() and
 *   csi_icache_enable(). Called from __bl616cl_start() after
 *   bl616cl_pmp_init() and before bl616cl_section_load().
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void bl616cl_cache_early_init(void)
{
  csi_dcache_enable();
  csi_icache_enable();
}

/****************************************************************************
 * Name: bl616cl_cache_after_load
 *
 * Description:
 *   Make the caches coherent with memory written while loading sections:
 *   clean the whole D-cache (__DCACHE_CALL) and invalidate the whole I-cache
 *   (__ICACHE_IALL), with barriers around each step. Called from
 *   __bl616cl_start() after bl616cl_section_load().
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
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
/****************************************************************************
 * Name: up_get_icache_linesize
 *
 * Description:
 *   Implement the NuttX up_get_icache_linesize() interface.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The I-cache line size in bytes (BFLB_CACHE_LINE_SIZE).
 *
 ****************************************************************************/

size_t up_get_icache_linesize(void)
{
  return BFLB_CACHE_LINE_SIZE;
}

/****************************************************************************
 * Name: up_get_icache_size
 *
 * Description:
 *   Implement the NuttX up_get_icache_size() interface.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The I-cache size in bytes (BL616CL_ICACHE_SIZE, 32 KiB).
 *
 ****************************************************************************/

size_t up_get_icache_size(void)
{
  return BL616CL_ICACHE_SIZE;
}

/****************************************************************************
 * Name: up_enable_icache
 *
 * Description:
 *   Implement the NuttX up_enable_icache() interface. The I-cache is enabled
 *   only if the MHCR IE bit is clear.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void up_enable_icache(void)
{
  if ((__get_MHCR() & CACHE_MHCR_IE_Msk) == 0)
    {
      bflb_l1c_icache_enable();
    }
}

/****************************************************************************
 * Name: up_disable_icache
 *
 * Description:
 *   Implement the NuttX up_disable_icache() interface. The I-cache is
 *   disabled only if the MHCR IE bit is set.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void up_disable_icache(void)
{
  if ((__get_MHCR() & CACHE_MHCR_IE_Msk) != 0)
    {
      bflb_l1c_icache_disable();
    }
}

/****************************************************************************
 * Name: up_invalidate_icache
 *
 * Description:
 *   Implement the NuttX up_invalidate_icache() interface. The range is
 *   aligned outward to cache lines; a range that is empty or outside the RAM
 *   and XIP domains is ignored.
 *
 * Input Parameters:
 *   start - Start address of the range.
 *   end - End address of the range (exclusive).
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: up_invalidate_icache_all
 *
 * Description:
 *   Implement the NuttX up_invalidate_icache_all() interface by invalidating
 *   the whole I-cache.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void up_invalidate_icache_all(void)
{
  bflb_l1c_icache_invalid_all();
}

/****************************************************************************
 * Name: up_get_dcache_linesize
 *
 * Description:
 *   Implement the NuttX up_get_dcache_linesize() interface.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The D-cache line size in bytes (BFLB_CACHE_LINE_SIZE).
 *
 ****************************************************************************/

size_t up_get_dcache_linesize(void)
{
  return BFLB_CACHE_LINE_SIZE;
}

/****************************************************************************
 * Name: up_get_dcache_size
 *
 * Description:
 *   Implement the NuttX up_get_dcache_size() interface.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The D-cache size in bytes (BL616CL_DCACHE_SIZE, 16 KiB).
 *
 ****************************************************************************/

size_t up_get_dcache_size(void)
{
  return BL616CL_DCACHE_SIZE;
}

/****************************************************************************
 * Name: up_enable_dcache
 *
 * Description:
 *   Implement the NuttX up_enable_dcache() interface. The D-cache is enabled
 *   only if the MHCR DE bit is clear.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void up_enable_dcache(void)
{
  if ((__get_MHCR() & CACHE_MHCR_DE_Msk) == 0)
    {
      bflb_l1c_dcache_enable();
    }
}

/****************************************************************************
 * Name: up_disable_dcache
 *
 * Description:
 *   Implement the NuttX up_disable_dcache() interface. The D-cache is
 *   disabled only if the MHCR DE bit is set.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void up_disable_dcache(void)
{
  if ((__get_MHCR() & CACHE_MHCR_DE_Msk) != 0)
    {
      bflb_l1c_dcache_disable();
    }
}

/****************************************************************************
 * Name: up_clean_dcache
 *
 * Description:
 *   Implement the NuttX up_clean_dcache() interface. Dirty lines in the range
 *   are written back to memory. The range is aligned outward to cache lines;
 *   a range that is empty or outside the RAM domain is ignored.
 *
 * Input Parameters:
 *   start - Start address of the range.
 *   end - End address of the range (exclusive).
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: up_clean_dcache_all
 *
 * Description:
 *   Implement the NuttX up_clean_dcache_all() interface by cleaning the whole
 *   D-cache.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void up_clean_dcache_all(void)
{
  bflb_l1c_dcache_clean_all();
}

/****************************************************************************
 * Name: up_invalidate_dcache
 *
 * Description:
 *   Implement the NuttX up_invalidate_dcache() interface. Lines only partly
 *   covered by the range are cleaned and invalidated so that neighboring data
 *   is not lost; fully covered lines are invalidated. A range that is empty
 *   or outside the RAM domain is ignored.
 *
 * Input Parameters:
 *   start - Start address of the range.
 *   end - End address of the range (exclusive).
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: up_invalidate_dcache_all
 *
 * Description:
 *   Implement the NuttX up_invalidate_dcache_all() interface by invalidating
 *   the whole D-cache.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void up_invalidate_dcache_all(void)
{
  bflb_l1c_dcache_invalidate_all();
}

/****************************************************************************
 * Name: up_flush_dcache
 *
 * Description:
 *   Implement the NuttX up_flush_dcache() interface by cleaning and
 *   invalidating the range. The range is aligned outward to cache lines; a
 *   range that is empty or outside the RAM domain is ignored.
 *
 * Input Parameters:
 *   start - Start address of the range.
 *   end - End address of the range (exclusive).
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: up_flush_dcache_all
 *
 * Description:
 *   Implement the NuttX up_flush_dcache_all() interface by cleaning and
 *   invalidating the whole D-cache.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void up_flush_dcache_all(void)
{
  bflb_l1c_dcache_clean_invalidate_all();
}

/****************************************************************************
 * Name: up_coherent_dcache
 *
 * Description:
 *   Implement the NuttX up_coherent_dcache() interface. Clean the D-cache and
 *   then invalidate the I-cache for the range so that newly written code
 *   becomes visible to instruction fetch. A zero length, an overflowing range
 *   or a range outside the RAM domain is ignored.
 *
 * Input Parameters:
 *   addr - Start address of the range.
 *   len - Length of the range in bytes.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

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
