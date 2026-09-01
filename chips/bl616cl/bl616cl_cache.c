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

#include "bflb_l1c.h"
#include "bl616cl_cache.h"
#include "bl616cl_cpu.h"

#ifdef CONFIG_BL616CL_CACHE_TEST
#  include "bl616cl_cache_test.h"
#endif

#if defined(CONFIG_BL616CL_CACHE) && !defined(CONFIG_BL616CL_CACHE_TEST)
enum bl616cl_cache_test_operation_e
{
  BL616CL_CACHE_TEST_ICACHE_ENABLE,
  BL616CL_CACHE_TEST_ICACHE_DISABLE,
  BL616CL_CACHE_TEST_ICACHE_INVALIDATE,
  BL616CL_CACHE_TEST_ICACHE_INVALIDATE_ALL,
  BL616CL_CACHE_TEST_DCACHE_ENABLE,
  BL616CL_CACHE_TEST_DCACHE_DISABLE,
  BL616CL_CACHE_TEST_DCACHE_CLEAN,
  BL616CL_CACHE_TEST_DCACHE_INVALIDATE,
  BL616CL_CACHE_TEST_DCACHE_FLUSH,
  BL616CL_CACHE_TEST_DCACHE_CLEAN_ALL,
  BL616CL_CACHE_TEST_DCACHE_INVALIDATE_ALL,
  BL616CL_CACHE_TEST_DCACHE_FLUSH_ALL,
  BL616CL_CACHE_TEST_COHERENT
};
#endif

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

#define BL616CL_CACHE_LINE_SIZE      32u
#define BL616CL_ICACHE_SIZE          (32u * 1024u)
#define BL616CL_DCACHE_SIZE          (16u * 1024u)
#define BL616CL_CACHE_CHUNK_MAX      \
  ((uintptr_t)INT32_MAX & ~(BL616CL_CACHE_LINE_SIZE - 1u))

#ifdef CONFIG_BL616CL_CACHE
extern uint8_t __bl616cl_cache_xip_start;
extern uint8_t __bl616cl_cache_xip_end;
extern uint8_t __bl616cl_cache_ram_start;
extern uint8_t __bl616cl_cache_ram_end;
#endif

#ifdef CONFIG_BL616CL_CACHE_TEST
static bl616cl_cache_test_hook_t g_cache_test_hook;
static void *g_cache_test_arg;
static uintptr_t g_cache_test_chunk_limit;
static bool g_cache_test_bypass;
#endif

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

#ifdef CONFIG_BL616CL_CACHE
static inline uintptr_t bl616cl_cache_align_down(uintptr_t value)
{
  return value & ~(BL616CL_CACHE_LINE_SIZE - 1u);
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

#ifdef CONFIG_BL616CL_CACHE_TEST
static void bl616cl_cache_test_emit(enum bl616cl_cache_test_event_e event,
                                    enum bl616cl_cache_test_operation_e op,
                                    uintptr_t addr, uintptr_t size)
{
  struct bl616cl_cache_test_event_s entry;

  if (g_cache_test_hook != NULL)
    {
      entry.event = event;
      entry.operation = op;
      entry.addr = addr;
      entry.size = size;
      g_cache_test_hook(&entry, g_cache_test_arg);
    }
}

static bool bl616cl_cache_test_bypass(void)
{
  return g_cache_test_bypass;
}

static uintptr_t bl616cl_cache_chunk_limit(void)
{
  return g_cache_test_chunk_limit != 0 ? g_cache_test_chunk_limit :
                                        BL616CL_CACHE_CHUNK_MAX;
}
#else
#  define bl616cl_cache_test_emit(event, op, addr, size)
#  define bl616cl_cache_test_bypass() false
#  define bl616cl_cache_chunk_limit() BL616CL_CACHE_CHUNK_MAX
#endif

static bool bl616cl_cache_prepare_range(
  enum bl616cl_cache_test_operation_e op, uintptr_t start, uintptr_t end,
  bool icache, uintptr_t *aligned_start, uintptr_t *aligned_end)
{
  if (end <= start)
    {
      bl616cl_cache_test_emit(BL616CL_CACHE_TEST_EVENT_NOOP, op, start, 0);
      return false;
    }

  if (end > UINTPTR_MAX - (BL616CL_CACHE_LINE_SIZE - 1u))
    {
      bl616cl_cache_test_emit(BL616CL_CACHE_TEST_EVENT_REJECT, op, start,
                              end - start);
      return false;
    }

  *aligned_start = bl616cl_cache_align_down(start);
  *aligned_end = bl616cl_cache_align_down(
    end + BL616CL_CACHE_LINE_SIZE - 1u);

  if ((icache && !bl616cl_cache_icache_contains(*aligned_start,
                                                *aligned_end)) ||
      (!icache && !bl616cl_cache_ram_contains(*aligned_start,
                                              *aligned_end)))
    {
      bl616cl_cache_test_emit(BL616CL_CACHE_TEST_EVENT_REJECT, op, start,
                              end - start);
      return false;
    }

  return true;
}

static void bl616cl_cache_range_operation(
  enum bl616cl_cache_test_operation_e op, uintptr_t start, uintptr_t end)
{
  uintptr_t chunk_limit = bl616cl_cache_chunk_limit();
  uintptr_t chunk;

  while (start < end)
    {
      chunk = end - start;
      if (chunk > chunk_limit)
        {
          chunk = chunk_limit;
        }

      bl616cl_cache_test_emit(BL616CL_CACHE_TEST_EVENT_OPERATION, op, start,
                              chunk);
      if (!bl616cl_cache_test_bypass())
        {
          switch (op)
            {
              case BL616CL_CACHE_TEST_ICACHE_INVALIDATE:
                bflb_l1c_icache_invalid_range((void *)start,
                                              (uint32_t)chunk);
                break;

              case BL616CL_CACHE_TEST_DCACHE_CLEAN:
                bflb_l1c_dcache_clean_range((void *)start, (uint32_t)chunk);
                break;

              case BL616CL_CACHE_TEST_DCACHE_INVALIDATE:
                bflb_l1c_dcache_invalidate_range((void *)start,
                                                 (uint32_t)chunk);
                break;

              case BL616CL_CACHE_TEST_DCACHE_FLUSH:
                bflb_l1c_dcache_clean_invalidate_range((void *)start,
                                                       (uint32_t)chunk);
                break;

              default:
                break;
            }
        }

      start += chunk;
    }
}

static void bl616cl_cache_all_operation(
  enum bl616cl_cache_test_operation_e op)
{
  bl616cl_cache_test_emit(BL616CL_CACHE_TEST_EVENT_OPERATION, op, 0, 0);
  if (bl616cl_cache_test_bypass())
    {
      return;
    }

  switch (op)
    {
      case BL616CL_CACHE_TEST_ICACHE_ENABLE:
        if ((bl616cl_csr_read(BL616CL_CSR_MHCR) & BL616CL_MHCR_IE) == 0)
          {
            bflb_l1c_icache_enable();
          }
        break;

      case BL616CL_CACHE_TEST_ICACHE_DISABLE:
        if ((bl616cl_csr_read(BL616CL_CSR_MHCR) & BL616CL_MHCR_IE) != 0)
          {
            bflb_l1c_icache_disable();
          }
        break;

      case BL616CL_CACHE_TEST_ICACHE_INVALIDATE_ALL:
        bflb_l1c_icache_invalid_all();
        break;

      case BL616CL_CACHE_TEST_DCACHE_ENABLE:
        if ((bl616cl_csr_read(BL616CL_CSR_MHCR) & BL616CL_MHCR_DE) == 0)
          {
            bflb_l1c_dcache_enable();
          }
        break;

      case BL616CL_CACHE_TEST_DCACHE_DISABLE:
        if ((bl616cl_csr_read(BL616CL_CSR_MHCR) & BL616CL_MHCR_DE) != 0)
          {
            bflb_l1c_dcache_disable();
          }
        break;

      case BL616CL_CACHE_TEST_DCACHE_CLEAN_ALL:
        bflb_l1c_dcache_clean_all();
        break;

      case BL616CL_CACHE_TEST_DCACHE_INVALIDATE_ALL:
        bflb_l1c_dcache_invalidate_all();
        break;

      case BL616CL_CACHE_TEST_DCACHE_FLUSH_ALL:
        bflb_l1c_dcache_clean_invalidate_all();
        break;

      default:
        break;
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

#ifdef CONFIG_BL616CL_CACHE
size_t up_get_icache_linesize(void)
{
  return BL616CL_CACHE_LINE_SIZE;
}

size_t up_get_icache_size(void)
{
  return BL616CL_ICACHE_SIZE;
}

void up_enable_icache(void)
{
  bl616cl_cache_all_operation(BL616CL_CACHE_TEST_ICACHE_ENABLE);
}

void up_disable_icache(void)
{
  bl616cl_cache_all_operation(BL616CL_CACHE_TEST_ICACHE_DISABLE);
}

void up_invalidate_icache(uintptr_t start, uintptr_t end)
{
  uintptr_t aligned_start;
  uintptr_t aligned_end;

  if (bl616cl_cache_prepare_range(BL616CL_CACHE_TEST_ICACHE_INVALIDATE,
                                  start, end, true, &aligned_start,
                                  &aligned_end))
    {
      bl616cl_cache_range_operation(BL616CL_CACHE_TEST_ICACHE_INVALIDATE,
                                    aligned_start, aligned_end);
    }
}

void up_invalidate_icache_all(void)
{
  bl616cl_cache_all_operation(BL616CL_CACHE_TEST_ICACHE_INVALIDATE_ALL);
}

size_t up_get_dcache_linesize(void)
{
  return BL616CL_CACHE_LINE_SIZE;
}

size_t up_get_dcache_size(void)
{
  return BL616CL_DCACHE_SIZE;
}

void up_enable_dcache(void)
{
  bl616cl_cache_all_operation(BL616CL_CACHE_TEST_DCACHE_ENABLE);
}

void up_disable_dcache(void)
{
  bl616cl_cache_all_operation(BL616CL_CACHE_TEST_DCACHE_DISABLE);
}

void up_clean_dcache(uintptr_t start, uintptr_t end)
{
  uintptr_t aligned_start;
  uintptr_t aligned_end;

  if (bl616cl_cache_prepare_range(BL616CL_CACHE_TEST_DCACHE_CLEAN, start,
                                  end, false, &aligned_start, &aligned_end))
    {
      bl616cl_cache_range_operation(BL616CL_CACHE_TEST_DCACHE_CLEAN,
                                    aligned_start, aligned_end);
    }
}

void up_clean_dcache_all(void)
{
  bl616cl_cache_all_operation(BL616CL_CACHE_TEST_DCACHE_CLEAN_ALL);
}

void up_invalidate_dcache(uintptr_t start, uintptr_t end)
{
  uintptr_t aligned_start;
  uintptr_t aligned_end;
  uintptr_t first_full;
  uintptr_t last_full_end;

  if (!bl616cl_cache_prepare_range(BL616CL_CACHE_TEST_DCACHE_INVALIDATE,
                                   start, end, false, &aligned_start,
                                   &aligned_end))
    {
      return;
    }

  if (aligned_end - aligned_start == BL616CL_CACHE_LINE_SIZE)
    {
      enum bl616cl_cache_test_operation_e op;

      op = start == aligned_start && end == aligned_end ?
           BL616CL_CACHE_TEST_DCACHE_INVALIDATE :
           BL616CL_CACHE_TEST_DCACHE_FLUSH;
      bl616cl_cache_range_operation(op,
                                    aligned_start, aligned_end);
      return;
    }

  first_full = aligned_start;
  if (start != aligned_start)
    {
      bl616cl_cache_range_operation(BL616CL_CACHE_TEST_DCACHE_FLUSH,
                                    aligned_start,
                                    aligned_start + BL616CL_CACHE_LINE_SIZE);
      first_full += BL616CL_CACHE_LINE_SIZE;
    }

  last_full_end = aligned_end;
  if (end != aligned_end)
    {
      last_full_end -= BL616CL_CACHE_LINE_SIZE;
    }

  if (first_full < last_full_end)
    {
      bl616cl_cache_range_operation(BL616CL_CACHE_TEST_DCACHE_INVALIDATE,
                                    first_full, last_full_end);
    }

  if (end != aligned_end)
    {
      bl616cl_cache_range_operation(BL616CL_CACHE_TEST_DCACHE_FLUSH,
                                    last_full_end, aligned_end);
    }
}

void up_invalidate_dcache_all(void)
{
  bl616cl_cache_all_operation(BL616CL_CACHE_TEST_DCACHE_INVALIDATE_ALL);
}

void up_flush_dcache(uintptr_t start, uintptr_t end)
{
  uintptr_t aligned_start;
  uintptr_t aligned_end;

  if (bl616cl_cache_prepare_range(BL616CL_CACHE_TEST_DCACHE_FLUSH, start,
                                  end, false, &aligned_start, &aligned_end))
    {
      bl616cl_cache_range_operation(BL616CL_CACHE_TEST_DCACHE_FLUSH,
                                    aligned_start, aligned_end);
    }
}

void up_flush_dcache_all(void)
{
  bl616cl_cache_all_operation(BL616CL_CACHE_TEST_DCACHE_FLUSH_ALL);
}

void up_coherent_dcache(uintptr_t addr, size_t len)
{
  uintptr_t aligned_start;
  uintptr_t aligned_end;
  uintptr_t end;

  if (len == 0)
    {
      bl616cl_cache_test_emit(BL616CL_CACHE_TEST_EVENT_NOOP,
                              BL616CL_CACHE_TEST_COHERENT, addr, 0);
      return;
    }

  if (len > UINTPTR_MAX - addr)
    {
      bl616cl_cache_test_emit(BL616CL_CACHE_TEST_EVENT_REJECT,
                              BL616CL_CACHE_TEST_COHERENT, addr, len);
      return;
    }

  end = addr + len;
  if (!bl616cl_cache_prepare_range(BL616CL_CACHE_TEST_COHERENT, addr, end,
                                   false, &aligned_start, &aligned_end))
    {
      return;
    }

  bl616cl_cache_range_operation(BL616CL_CACHE_TEST_DCACHE_CLEAN,
                                aligned_start, aligned_end);
  bl616cl_cache_range_operation(BL616CL_CACHE_TEST_ICACHE_INVALIDATE,
                                aligned_start, aligned_end);
}
#endif

#ifdef CONFIG_BL616CL_CACHE_TEST
void bl616cl_cache_test_configure(bl616cl_cache_test_hook_t hook, void *arg,
                                  uintptr_t chunk_limit, bool bypass)
{
  chunk_limit &= ~(BL616CL_CACHE_LINE_SIZE - 1u);
  if (chunk_limit > BL616CL_CACHE_CHUNK_MAX)
    {
      chunk_limit = BL616CL_CACHE_CHUNK_MAX;
    }

  g_cache_test_hook = hook;
  g_cache_test_arg = arg;
  g_cache_test_chunk_limit = chunk_limit;
  g_cache_test_bypass = bypass;
}

uint32_t bl616cl_cache_test_mhcr(void)
{
  return bl616cl_csr_read(BL616CL_CSR_MHCR);
}
#endif
