/****************************************************************************
 * apps/os_feature_tests/cache/cache_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/cache.h>
#include <nuttx/spinlock.h>

#include <arch/chip/bl616cl_cache_test.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CACHE_LINE_SIZE       32u
#define CACHE_RAM_ALIAS       0x40000000u
#define CACHE_TRACE_MAX       32
#define CACHE_TEST_BUFFER_SIZE 128
#define CACHE_NOCACHE_STACK_SIZE 512
#define RISCV_ADDI_A0_ZERO_1  0x00100513u
#define RISCV_ADDI_A0_ZERO_2  0x00200513u
#define RISCV_RET             0x00008067u

#define MHCR_IE               (1u << 0)
#define MHCR_DE               (1u << 1)

#define RESULT_PASS           0
#define RESULT_FAIL           1

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct trace_s
{
  struct bl616cl_cache_test_event_s entries[CACHE_TRACE_MAX];
  size_t count;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

extern uint8_t __bl616cl_cache_xip_start;
extern uint8_t __bl616cl_cache_xip_end;
extern uint8_t __bl616cl_cache_ram_start;
extern uint8_t __bl616cl_cache_ram_end;

/****************************************************************************
 * External Functions
 ****************************************************************************/

void bl_cache_test_invalidate_all(uintptr_t nocache_stack_top);

static uint8_t g_buffer[CACHE_TEST_BUFFER_SIZE]
  __attribute__((aligned(CACHE_LINE_SIZE)));
static uint32_t g_code[CACHE_LINE_SIZE / sizeof(uint32_t)]
  __attribute__((aligned(CACHE_LINE_SIZE)));
static uint8_t g_nocache_stack[CACHE_NOCACHE_STACK_SIZE]
  __attribute__((section(".nocache_noinit_ram"), aligned(CACHE_LINE_SIZE)));

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void trace_hook(const struct bl616cl_cache_test_event_s *event,
                       void *arg)
{
  struct trace_s *trace = arg;

  if (trace->count < CACHE_TRACE_MAX)
    {
      trace->entries[trace->count++] = *event;
    }
}

static void trace_start(struct trace_s *trace, uintptr_t chunk_limit)
{
  memset(trace, 0, sizeof(*trace));
  bl616cl_cache_test_configure(trace_hook, trace, chunk_limit, true);
}

static void trace_stop(void)
{
  bl616cl_cache_test_configure(NULL, NULL, 0, false);
}

static bool trace_entry(const struct trace_s *trace, size_t index,
                        enum bl616cl_cache_test_event_e event,
                        enum bl616cl_cache_test_operation_e operation,
                        uintptr_t addr, uintptr_t size)
{
  return index < trace->count && trace->entries[index].event == event &&
         trace->entries[index].operation == operation &&
         trace->entries[index].addr == addr &&
         trace->entries[index].size == size;
}

static int fail(const char *case_id, const char *reason)
{
  printf("  [%s] FAIL %s\n", case_id, reason);
  return RESULT_FAIL;
}

static int run_case_001(void)
{
  struct trace_s trace;

  printf("[CACHE-001] Public ABI geometry and all-operation routing\n");
  if (up_get_icache_linesize() != 32 || up_get_dcache_linesize() != 32 ||
      up_get_icache_size() != 32768 || up_get_dcache_size() != 16384)
    {
      return fail("CACHE-001", "reported cache geometry mismatch");
    }

  trace_start(&trace, 0);
  up_enable_icache();
  up_disable_icache();
  up_invalidate_icache_all();
  up_enable_dcache();
  up_disable_dcache();
  up_clean_dcache_all();
  up_invalidate_dcache_all();
  up_flush_dcache_all();
  trace_stop();

  if (trace.count != 8 ||
      !trace_entry(&trace, 0, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_ICACHE_ENABLE, 0, 0) ||
      !trace_entry(&trace, 1, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_ICACHE_DISABLE, 0, 0) ||
      !trace_entry(&trace, 2, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_ICACHE_INVALIDATE_ALL, 0, 0) ||
      !trace_entry(&trace, 3, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_DCACHE_ENABLE, 0, 0) ||
      !trace_entry(&trace, 4, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_DCACHE_DISABLE, 0, 0) ||
      !trace_entry(&trace, 5, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_DCACHE_CLEAN_ALL, 0, 0) ||
      !trace_entry(&trace, 6, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_DCACHE_INVALIDATE_ALL, 0, 0) ||
      !trace_entry(&trace, 7, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_DCACHE_FLUSH_ALL, 0, 0))
    {
      return fail("CACHE-001", "all-operation routing mismatch");
    }

  printf("  [CACHE-001] PASS line=32 I=32768 D=16384 and 8 routes\n");
  return RESULT_PASS;
}

static int run_case_002(void)
{
  struct trace_s trace;
  uintptr_t ram = (uintptr_t)&__bl616cl_cache_ram_start;
  uintptr_t ram_end = (uintptr_t)&__bl616cl_cache_ram_end;
  uintptr_t xip = (uintptr_t)&__bl616cl_cache_xip_start;

  printf("[CACHE-002] Address domains and aligned full-span validation\n");
  trace_start(&trace, 0);
  up_clean_dcache(ram + 1, ram + 63);
  up_invalidate_icache(xip + 1, xip + 63);
  up_clean_dcache(ram - CACHE_RAM_ALIAS, ram - CACHE_RAM_ALIAS + 32);
  up_flush_dcache(0x20000000, 0x20000020);
  up_clean_dcache(ram_end - 16, ram_end + 16);
  trace_stop();

  if (trace.count != 5 ||
      !trace_entry(&trace, 0, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_DCACHE_CLEAN, ram, 64) ||
      !trace_entry(&trace, 1, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_ICACHE_INVALIDATE, xip, 64) ||
      !trace_entry(&trace, 2, BL616CL_CACHE_TEST_EVENT_REJECT,
                   BL616CL_CACHE_TEST_DCACHE_CLEAN,
                   ram - CACHE_RAM_ALIAS, 32) ||
      !trace_entry(&trace, 3, BL616CL_CACHE_TEST_EVENT_REJECT,
                   BL616CL_CACHE_TEST_DCACHE_FLUSH, 0x20000000, 32) ||
      !trace_entry(&trace, 4, BL616CL_CACHE_TEST_EVENT_REJECT,
                   BL616CL_CACHE_TEST_DCACHE_CLEAN, ram_end - 16, 32))
    {
      return fail("CACHE-002", "domain acceptance or rejection mismatch");
    }

  printf("  [CACHE-002] PASS RAM/XIP accepted; "
         "nocache/MMIO/cross rejected\n");
  return RESULT_PASS;
}

static int run_case_003(void)
{
  struct trace_s trace;
  uintptr_t ram = (uintptr_t)&__bl616cl_cache_ram_start;

  printf("[CACHE-003] Chunk order and zero/reversed/overflow boundaries\n");
  trace_start(&trace, 64);
  up_flush_dcache(ram, ram + 160);
  up_flush_dcache(ram, ram);
  up_flush_dcache(ram + 64, ram + 32);
  up_flush_dcache(UINTPTR_MAX - 15, UINTPTR_MAX);
  trace_stop();

  if (trace.count != 6 ||
      !trace_entry(&trace, 0, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_DCACHE_FLUSH, ram, 64) ||
      !trace_entry(&trace, 1, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_DCACHE_FLUSH, ram + 64, 64) ||
      !trace_entry(&trace, 2, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_DCACHE_FLUSH, ram + 128, 32) ||
      !trace_entry(&trace, 3, BL616CL_CACHE_TEST_EVENT_NOOP,
                   BL616CL_CACHE_TEST_DCACHE_FLUSH, ram, 0) ||
      !trace_entry(&trace, 4, BL616CL_CACHE_TEST_EVENT_NOOP,
                   BL616CL_CACHE_TEST_DCACHE_FLUSH, ram + 64, 0) ||
      !trace_entry(&trace, 5, BL616CL_CACHE_TEST_EVENT_REJECT,
                   BL616CL_CACHE_TEST_DCACHE_FLUSH, UINTPTR_MAX - 15, 15))
    {
      return fail("CACHE-003", "chunk or boundary event mismatch");
    }

  printf("  [CACHE-003] PASS chunks=64,64,32 and invalid ranges no-op\n");
  return RESULT_PASS;
}

static int run_case_004(void)
{
  struct trace_s trace;
  uintptr_t ram = (uintptr_t)&__bl616cl_cache_ram_start;

  printf("[CACHE-004] Partial D-cache invalidate ownership algorithm\n");
  trace_start(&trace, 0);
  up_invalidate_dcache(ram + 1, ram + 31);
  up_invalidate_dcache(ram + 32, ram + 64);
  up_invalidate_dcache(ram + 1, ram + 64);
  up_invalidate_dcache(ram, ram + 63);
  up_invalidate_dcache(ram + 1, ram + 95);
  trace_stop();

  if (trace.count != 9 ||
      !trace_entry(&trace, 0, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_DCACHE_FLUSH, ram, 32) ||
      !trace_entry(&trace, 1, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_DCACHE_INVALIDATE, ram + 32, 32) ||
      !trace_entry(&trace, 2, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_DCACHE_FLUSH, ram, 32) ||
      !trace_entry(&trace, 3, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_DCACHE_INVALIDATE, ram + 32, 32) ||
      !trace_entry(&trace, 4, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_DCACHE_INVALIDATE, ram, 32) ||
      !trace_entry(&trace, 5, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_DCACHE_FLUSH, ram + 32, 32) ||
      !trace_entry(&trace, 6, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_DCACHE_FLUSH, ram, 32) ||
      !trace_entry(&trace, 7, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_DCACHE_INVALIDATE, ram + 32, 32) ||
      !trace_entry(&trace, 8, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_DCACHE_FLUSH, ram + 64, 32))
    {
      return fail("CACHE-004", "partial-line operation order mismatch");
    }

  printf("  [CACHE-004] PASS CI/single-line and "
         "first-CI/middle-I/last-CI\n");
  return RESULT_PASS;
}

static int run_case_005(void)
{
  struct trace_s trace;
  int (*fn)(void) = (int (*)(void))g_code;
  uintptr_t ram = (uintptr_t)&__bl616cl_cache_ram_start;
  uintptr_t xip = (uintptr_t)&__bl616cl_cache_xip_start;

  printf("[CACHE-005] Coherent D-clean to I-invalidate and len overflow\n");
  trace_start(&trace, 0);
  up_coherent_dcache(ram + 1, 62);
  up_coherent_dcache(xip, 32);
  up_coherent_dcache(ram, 0);
  up_coherent_dcache(UINTPTR_MAX - 15, 32);
  trace_stop();

  if (trace.count != 5 ||
      !trace_entry(&trace, 0, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_DCACHE_CLEAN, ram, 64) ||
      !trace_entry(&trace, 1, BL616CL_CACHE_TEST_EVENT_OPERATION,
                   BL616CL_CACHE_TEST_ICACHE_INVALIDATE, ram, 64) ||
      !trace_entry(&trace, 2, BL616CL_CACHE_TEST_EVENT_REJECT,
                   BL616CL_CACHE_TEST_COHERENT, xip, 32) ||
      !trace_entry(&trace, 3, BL616CL_CACHE_TEST_EVENT_NOOP,
                   BL616CL_CACHE_TEST_COHERENT, ram, 0) ||
      !trace_entry(&trace, 4, BL616CL_CACHE_TEST_EVENT_REJECT,
                   BL616CL_CACHE_TEST_COHERENT, UINTPTR_MAX - 15, 32))
    {
      return fail("CACHE-005", "coherent order or rejection mismatch");
    }

  g_code[0] = RISCV_ADDI_A0_ZERO_1;
  g_code[1] = RISCV_RET;
  up_coherent_dcache((uintptr_t)g_code, 2 * sizeof(uint32_t));
  if (fn() != 1)
    {
      return fail("CACHE-005", "initial RAM code execution mismatch");
    }

  g_code[0] = RISCV_ADDI_A0_ZERO_2;
  up_coherent_dcache((uintptr_t)g_code, sizeof(uint32_t));
  if (fn() != 2)
    {
      return fail("CACHE-005", "updated RAM code execution mismatch");
    }

  printf("  [CACHE-005] PASS D-clean/I-invalidate order and RAM update\n");
  return RESULT_PASS;
}

static int run_case_006(void)
{
  volatile uint8_t *cached = g_buffer;
  volatile uint8_t *nocache;
  irqstate_t flags;
  uint32_t mhcr;

  printf("[CACHE-006] Runtime I/D toggle and independent MHCR state\n");
  trace_stop();
  nocache = (volatile uint8_t *)((uintptr_t)g_buffer - CACHE_RAM_ALIAS);
  flags = enter_critical_section();
  up_enable_icache();
  up_enable_dcache();
  mhcr = bl616cl_cache_test_mhcr();
  if ((mhcr & (MHCR_IE | MHCR_DE)) != (MHCR_IE | MHCR_DE))
    {
      up_enable_icache();
      up_enable_dcache();
      leave_critical_section(flags);
      return fail("CACHE-006", "initial I/D enabled state missing");
    }

  up_disable_icache();
  mhcr = bl616cl_cache_test_mhcr();
  if ((mhcr & MHCR_IE) != 0 || (mhcr & MHCR_DE) == 0)
    {
      up_enable_icache();
      leave_critical_section(flags);
      return fail("CACHE-006", "I disable changed wrong MHCR bits");
    }

  up_disable_icache();
  up_enable_icache();
  nocache[0] = 0x40;
  up_invalidate_dcache((uintptr_t)g_buffer,
                       (uintptr_t)g_buffer + CACHE_LINE_SIZE);
  cached[0] = 0x61;
  up_enable_dcache();
  up_disable_dcache();
  mhcr = bl616cl_cache_test_mhcr();
  if ((mhcr & MHCR_IE) == 0 || (mhcr & MHCR_DE) != 0)
    {
      up_enable_dcache();
      leave_critical_section(flags);
      return fail("CACHE-006", "D disable changed wrong MHCR bits");
    }

  if (nocache[0] != 0x61)
    {
      up_enable_dcache();
      leave_critical_section(flags);
      return fail("CACHE-006", "D disable lost dirty cached data");
    }

  up_disable_dcache();
  up_enable_dcache();
  mhcr = bl616cl_cache_test_mhcr();
  if ((mhcr & (MHCR_IE | MHCR_DE)) != (MHCR_IE | MHCR_DE))
    {
      up_enable_icache();
      up_enable_dcache();
      leave_critical_section(flags);
      return fail("CACHE-006", "final I/D enabled state missing");
    }

  if (cached[0] != 0x61)
    {
      leave_critical_section(flags);
      return fail("CACHE-006", "D re-enable changed persisted data");
    }

  leave_critical_section(flags);
  printf("  [CACHE-006] PASS idempotent toggles, MHCR, dirty persistence\n");
  return RESULT_PASS;
}

static int run_case_007(void)
{
  volatile uint8_t *nocache;
  unsigned int i;

  printf("[CACHE-007] Cached/nocache range visibility and partial RX\n");
  trace_stop();
  nocache = (volatile uint8_t *)((uintptr_t)g_buffer - CACHE_RAM_ALIAS);
  memset(g_buffer, 0x11, sizeof(g_buffer));
  up_clean_dcache((uintptr_t)g_buffer,
                  (uintptr_t)g_buffer + sizeof(g_buffer));
  for (i = 0; i < sizeof(g_buffer); i++)
    {
      if (nocache[i] != 0x11)
        {
          return fail("CACHE-007", "range clean not visible via nocache");
        }
    }

  up_invalidate_dcache((uintptr_t)&g_buffer[32],
                       (uintptr_t)&g_buffer[96]);
  for (i = 32; i < 96; i++)
    {
      nocache[i] = 0x22;
    }

  for (i = 0; i < sizeof(g_buffer); i++)
    {
      uint8_t expected = i >= 32 && i < 96 ? 0x22 : 0x11;
      if (g_buffer[i] != expected)
        {
          return fail("CACHE-007", "aligned RX visibility mismatch");
        }
    }

  memset(g_buffer, 0x33, sizeof(g_buffer));
  up_invalidate_dcache((uintptr_t)&g_buffer[33],
                       (uintptr_t)&g_buffer[63]);
  for (i = 33; i < 63; i++)
    {
      nocache[i] = 0x44;
    }

  for (i = 0; i < 64; i++)
    {
      uint8_t expected = i >= 33 && i < 63 ? 0x44 : 0x33;
      if (g_buffer[i] != expected)
        {
          return fail("CACHE-007", "partial RX sentinel mismatch");
        }
    }

  printf("  [CACHE-007] PASS clean visibility and pre-DMA invalidate\n");
  return RESULT_PASS;
}

static int run_case_008(void)
{
  volatile uint8_t *nocache;
  irqstate_t flags;
  uintptr_t stack_top;

  printf("[CACHE-008] Clean/flush/invalidate all with nocache stack\n");
  trace_stop();
  nocache = (volatile uint8_t *)((uintptr_t)g_buffer - CACHE_RAM_ALIAS);

  g_buffer[0] = 0x51;
  up_clean_dcache_all();
  if (nocache[0] != 0x51)
    {
      return fail("CACHE-008", "clean-all visibility mismatch");
    }

  g_buffer[0] = 0x52;
  up_flush_dcache_all();
  if (nocache[0] != 0x52)
    {
      return fail("CACHE-008", "flush-all visibility mismatch");
    }

  up_clean_dcache_all();
  nocache[0] = 0x53;
  stack_top = (uintptr_t)g_nocache_stack + sizeof(g_nocache_stack);
  flags = enter_critical_section();
  bl_cache_test_invalidate_all(stack_top);
  leave_critical_section(flags);
  if (g_buffer[0] != 0x53)
    {
      return fail("CACHE-008",
                  "invalidate-all producer visibility mismatch");
    }

  printf("  [CACHE-008] PASS all operations and nocache-stack invalidate\n");
  return RESULT_PASS;
}

static int run_case(const char *case_id)
{
  if (strcmp(case_id, "001") == 0)
    {
      return run_case_001();
    }
  else if (strcmp(case_id, "002") == 0)
    {
      return run_case_002();
    }
  else if (strcmp(case_id, "003") == 0)
    {
      return run_case_003();
    }
  else if (strcmp(case_id, "004") == 0)
    {
      return run_case_004();
    }
  else if (strcmp(case_id, "005") == 0)
    {
      return run_case_005();
    }
  else if (strcmp(case_id, "006") == 0)
    {
      return run_case_006();
    }
  else if (strcmp(case_id, "007") == 0)
    {
      return run_case_007();
    }
  else if (strcmp(case_id, "008") == 0)
    {
      return run_case_008();
    }

  return -EINVAL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  const char *case_id = argc > 1 ? argv[1] : "all";
  char current[4];
  int first = 1;
  int last = 8;
  int passed = 0;
  int failed = 0;
  int i;

  if (strcmp(case_id, "all") != 0)
    {
      int result = run_case(case_id);
      if (result < 0)
        {
          printf("Usage: cache_test [001..008|all]\n");
          return RESULT_FAIL;
        }

      return result;
    }

  printf("BL616CL OpenVela Cache Tests\n");
  for (i = first; i <= last; i++)
    {
      int result;

      snprintf(current, sizeof(current), "%03d", i);
      result = run_case(current);
      if (result == RESULT_PASS)
        {
          passed++;
        }
      else
        {
          failed++;
        }
    }

  printf("Cache Summary: pass=%d fail=%d -> %s\n", passed, failed,
         failed != 0 ? "FAIL" : "PASS");
  return failed != 0 ? RESULT_FAIL : RESULT_PASS;
}
