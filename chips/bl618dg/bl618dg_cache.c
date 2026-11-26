/****************************************************************************
 * vendor/bouffalolab/chips/bl618dg/bl618dg_cache.c
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
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/cache.h>
#include <nuttx/irq.h>

#include "hardware/bl618dg_core.h"
#include "bflb_l1c.h"
#include "bl618dg_cache.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL618DG_CACHE_CHUNK_MAX \
  ((uintptr_t)INT32_MAX & ~(CACHE_LINE_SIZE - 1u))

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_BL618DG_CACHE
extern uint8_t __bl618dg_cache_xip_start;
extern uint8_t __bl618dg_cache_xip_end;
extern uint8_t __bl618dg_cache_ram_start;
extern uint8_t __bl618dg_cache_ram_end;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_BL618DG_CACHE
typedef void (*bl618dg_cache_range_op_t)(void *addr, uint32_t size);

/****************************************************************************
 * Name: bl618dg_cache_align_down
 *
 * Description:
 *   Round a value down to a multiple of CACHE_LINE_SIZE.
 *
 * Input Parameters:
 *   value - Address to align.
 *
 * Returned Value:
 *   The value aligned down to a cache line boundary.
 *
 ****************************************************************************/

static inline uintptr_t bl618dg_cache_align_down(uintptr_t value)
{
  return value & ~(CACHE_LINE_SIZE - 1u);
}

/****************************************************************************
 * Name: bl618dg_cache_domain_contains
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

static bool bl618dg_cache_domain_contains(uintptr_t start, uintptr_t end,
                                          uintptr_t domain_start,
                                          uintptr_t domain_end)
{
  return start >= domain_start && start < domain_end && end > start &&
         end <= domain_end;
}

/****************************************************************************
 * Name: bl618dg_cache_ram_contains
 *
 * Description:
 *   Check that [start, end) lies inside the linker-defined AP OCRAM
 *   region (__bl618dg_cache_ram_*).
 *
 * Input Parameters:
 *   start - Start of the range to check.
 *   end - End of the range to check (exclusive).
 *
 * Returned Value:
 *   true if the range is inside one RAM domain; false otherwise.
 *
 ****************************************************************************/

static bool bl618dg_cache_ram_contains(uintptr_t start, uintptr_t end)
{
  return bl618dg_cache_domain_contains(
    start, end, (uintptr_t)&__bl618dg_cache_ram_start,
    (uintptr_t)&__bl618dg_cache_ram_end);
}

/****************************************************************************
 * Name: bl618dg_cache_icache_contains
 *
 * Description:
 *   Check that [start, end) lies inside the RAM domain or the linker-
 *   defined XIP window (__bl618dg_cache_xip_*).
 *
 * Input Parameters:
 *   start - Start of the range to check.
 *   end - End of the range to check (exclusive).
 *
 * Returned Value:
 *   true if the range is inside a RAM or XIP domain; false otherwise.
 *
 ****************************************************************************/

static bool bl618dg_cache_icache_contains(uintptr_t start, uintptr_t end)
{
  return bl618dg_cache_ram_contains(start, end) ||
         bl618dg_cache_domain_contains(
           start, end, (uintptr_t)&__bl618dg_cache_xip_start,
           (uintptr_t)&__bl618dg_cache_xip_end);
}

/****************************************************************************
 * Name: bl618dg_cache_prepare_range
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

static bool bl618dg_cache_prepare_range(
  uintptr_t start, uintptr_t end, bool icache, uintptr_t *aligned_start,
  uintptr_t *aligned_end)
{
  if (end <= start)
    {
      return false;
    }

  if (end > UINTPTR_MAX - (CACHE_LINE_SIZE - 1u))
    {
      return false;
    }

  *aligned_start = bl618dg_cache_align_down(start);
  *aligned_end = bl618dg_cache_align_down(
    end + CACHE_LINE_SIZE - 1u);

  if ((icache && !bl618dg_cache_icache_contains(*aligned_start,
                                                *aligned_end)) ||
      (!icache && !bl618dg_cache_ram_contains(*aligned_start,
                                              *aligned_end)))
    {
      return false;
    }

  return true;
}

/****************************************************************************
 * Name: bl618dg_cache_range_operation
 *
 * Description:
 *   Apply an LHAL cache operation to [start, end) in chunks no larger than
 *   BL618DG_CACHE_CHUNK_MAX.
 *
 * Input Parameters:
 *   start - Line-aligned start address.
 *   end - Line-aligned end address (exclusive).
 *   operation - LHAL range operation to apply to each chunk.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl618dg_cache_range_operation(
  uintptr_t start, uintptr_t end, bl618dg_cache_range_op_t operation)
{
  uintptr_t chunk;

  while (start < end)
    {
      chunk = end - start;
      if (chunk > BL618DG_CACHE_CHUNK_MAX)
        {
          chunk = BL618DG_CACHE_CHUNK_MAX;
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
 * Name: bl618dg_cache_early_init / bl618dg_cache_after_load
 *
 * Description:
 *   Follow the Nuclei SDK cache sequence. Early helpers are inline and do
 *   not call code awaiting relocation. Write RAM data back before invalidating
 *   instruction lines, then fence instruction fetch before running RAM code.
 *
 ****************************************************************************/

void bl618dg_cache_early_init(void)
{
  EnableICache();
  EnableDCache();
}

void bl618dg_cache_after_load(void)
{
  MFlushDCache();
  MInvalICache();
  __FENCE_I();
}

#ifdef CONFIG_BL618DG_CACHE
/****************************************************************************
 * Name: up_get_icache_linesize / up_get_icache_size
 *
 * Description:
 *   Read the implemented cache geometry from the Nuclei configuration CSR.
 *
 ****************************************************************************/

size_t up_get_icache_linesize(void)
{
  CacheInfo_Type info;

  GetICacheInfo(&info);
  return info.linesize;
}

size_t up_get_icache_size(void)
{
  CacheInfo_Type info;

  GetICacheInfo(&info);
  return info.size;
}

/****************************************************************************
 * Name: up_enable_icache / up_disable_icache
 *
 * Description:
 *   Change instruction-cache enable state through the LHAL API.
 *
 ****************************************************************************/

void up_enable_icache(void)
{
  bflb_l1c_icache_enable();
}

void up_disable_icache(void)
{
  bflb_l1c_icache_disable();
}

/****************************************************************************
 * Name: up_invalidate_icache / up_invalidate_icache_all
 *
 * Description:
 *   Invalidate instruction lines and synchronize subsequent instruction fetch.
 *
 ****************************************************************************/

void up_invalidate_icache(uintptr_t start, uintptr_t end)
{
  uintptr_t aligned_start;
  uintptr_t aligned_end;

  if (bl618dg_cache_prepare_range(start, end, true, &aligned_start,
                                  &aligned_end))
    {
      bl618dg_cache_range_operation(aligned_start, aligned_end,
                                    bflb_l1c_icache_invalid_range);
      __FENCE_I();
    }
}

void up_invalidate_icache_all(void)
{
  bflb_l1c_icache_invalid_all();
  __FENCE_I();
}

/****************************************************************************
 * Name: up_get_dcache_linesize / up_get_dcache_size
 *
 * Description:
 *   Read the data-cache geometry rather than assuming the CL cache layout.
 *
 ****************************************************************************/

size_t up_get_dcache_linesize(void)
{
  CacheInfo_Type info;

  GetDCacheInfo(&info);
  return info.linesize;
}

size_t up_get_dcache_size(void)
{
  CacheInfo_Type info;

  GetDCacheInfo(&info);
  return info.size;
}

/****************************************************************************
 * Name: up_enable_dcache / up_disable_dcache
 *
 * Description:
 *   Enable caching, or write back and invalidate before disabling it.
 *   Keep the disable sequence atomic against interrupt-side writes.
 *
 ****************************************************************************/

void up_enable_dcache(void)
{
  bflb_l1c_dcache_enable();
}

void up_disable_dcache(void)
{
  irqstate_t flags = up_irq_save();

  bflb_l1c_dcache_disable();
  up_irq_restore(flags);
}

/****************************************************************************
 * Name: up_clean_dcache / up_clean_dcache_all
 *
 * Description:
 *   Write dirty data back without discarding its cache lines.
 *
 ****************************************************************************/

void up_clean_dcache(uintptr_t start, uintptr_t end)
{
  uintptr_t aligned_start;
  uintptr_t aligned_end;

  if (bl618dg_cache_prepare_range(start, end, false, &aligned_start,
                                  &aligned_end))
    {
      bl618dg_cache_range_operation(aligned_start, aligned_end,
                                    bflb_l1c_dcache_clean_range);
    }
}

void up_clean_dcache_all(void)
{
  bflb_l1c_dcache_clean_all();
}

/****************************************************************************
 * Name: up_invalidate_dcache / up_invalidate_dcache_all
 *
 * Description:
 *   Preserve dirty bytes outside a partial boundary line before invalidating
 *   it. Complete lines inside the requested range are invalidated directly.
 *
 ****************************************************************************/

void up_invalidate_dcache(uintptr_t start, uintptr_t end)
{
  uintptr_t aligned_start;
  uintptr_t aligned_end;
  uintptr_t first_full;
  uintptr_t last_full_end;

  if (!bl618dg_cache_prepare_range(start, end, false, &aligned_start,
                                   &aligned_end))
    {
      return;
    }

  if (aligned_end - aligned_start == CACHE_LINE_SIZE)
    {
      bl618dg_cache_range_op_t operation;

      operation = start == aligned_start && end == aligned_end ?
                    bflb_l1c_dcache_invalidate_range :
                    bflb_l1c_dcache_clean_invalidate_range;
      bl618dg_cache_range_operation(aligned_start, aligned_end, operation);
      return;
    }

  first_full = aligned_start;
  if (start != aligned_start)
    {
      bl618dg_cache_range_operation(
        aligned_start, aligned_start + CACHE_LINE_SIZE,
        bflb_l1c_dcache_clean_invalidate_range);
      first_full += CACHE_LINE_SIZE;
    }

  last_full_end = aligned_end;
  if (end != aligned_end)
    {
      last_full_end -= CACHE_LINE_SIZE;
    }

  if (first_full < last_full_end)
    {
      bl618dg_cache_range_operation(first_full, last_full_end,
                                    bflb_l1c_dcache_invalidate_range);
    }

  if (end != aligned_end)
    {
      bl618dg_cache_range_operation(
        last_full_end, aligned_end, bflb_l1c_dcache_clean_invalidate_range);
    }
}

void up_invalidate_dcache_all(void)
{
  bflb_l1c_dcache_invalidate_all();
}

/****************************************************************************
 * Name: up_flush_dcache / up_flush_dcache_all
 *
 * Description:
 *   Write dirty data back and invalidate the corresponding cache lines.
 *
 ****************************************************************************/

void up_flush_dcache(uintptr_t start, uintptr_t end)
{
  uintptr_t aligned_start;
  uintptr_t aligned_end;

  if (bl618dg_cache_prepare_range(start, end, false, &aligned_start,
                                  &aligned_end))
    {
      bl618dg_cache_range_operation(
        aligned_start, aligned_end,
        bflb_l1c_dcache_clean_invalidate_range);
    }
}

void up_flush_dcache_all(void)
{
  bflb_l1c_dcache_clean_invalidate_all();
}

/****************************************************************************
 * Name: up_coherent_dcache
 *
 * Description:
 *   Clean the D-cache and then invalidate the I-cache for the range so
 *   that newly written RAM instructions become visible to instruction
 *   fetch. A zero length, an overflowing range or a range outside the
 *   RAM domain is ignored.
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
  if (!bl618dg_cache_prepare_range(addr, end, false, &aligned_start,
                                   &aligned_end))
    {
      return;
    }

  bl618dg_cache_range_operation(aligned_start, aligned_end,
                                bflb_l1c_dcache_clean_range);
  bl618dg_cache_range_operation(aligned_start, aligned_end,
                                bflb_l1c_icache_invalid_range);
  __FENCE_I();
}
#endif
