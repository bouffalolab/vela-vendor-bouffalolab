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
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/cache.h>
#include <nuttx/spinlock.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CACHE_LINE_SIZE          32u
#define CACHE_RAM_ALIAS          0x40000000u
#define CACHE_TEST_BUFFER_SIZE   128
#define CACHE_NOCACHE_STACK_SIZE 512
#define RISCV_ADDI_A0_ZERO_1     0x00100513u
#define RISCV_ADDI_A0_ZERO_2     0x00200513u
#define RISCV_RET                0x00008067u

#define CACHE_CSR_MHCR           0x7c1
#define MHCR_IE                  (1u << 0)
#define MHCR_DE                  (1u << 1)

#define RESULT_PASS              0
#define RESULT_FAIL              1

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint8_t g_buffer[CACHE_TEST_BUFFER_SIZE]
  __attribute__((aligned(CACHE_LINE_SIZE)));
static uint32_t g_code[CACHE_LINE_SIZE / sizeof(uint32_t)]
  __attribute__((aligned(CACHE_LINE_SIZE)));
static uint8_t g_nocache_stack[CACHE_NOCACHE_STACK_SIZE]
  __attribute__((section(".nocache_noinit_ram"), aligned(CACHE_LINE_SIZE)));

/****************************************************************************
 * External Functions
 ****************************************************************************/

void bl_cache_test_invalidate_all(uintptr_t nocache_stack_top);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t cache_mhcr(void)
{
  uint32_t value;

  __asm__ __volatile__("csrr %0, %1" : "=r"(value) : "i"(CACHE_CSR_MHCR));
  return value;
}

static int fail(const char *case_id, const char *reason)
{
  printf("  [%s] FAIL %s\n", case_id, reason);
  return RESULT_FAIL;
}

static int run_case_001(void)
{
  printf("[CACHE-001] Public ABI geometry\n");
  if (up_get_icache_linesize() != 32 || up_get_dcache_linesize() != 32 ||
      up_get_icache_size() != 32768 || up_get_dcache_size() != 16384)
    {
      return fail("CACHE-001", "reported cache geometry mismatch");
    }

  printf("  [CACHE-001] PASS line=32 I=32768 D=16384\n");
  return RESULT_PASS;
}

static int run_case_002(void)
{
  int (*fn)(void) = (int (*)(void))g_code;

  printf("[CACHE-002] Coherent RAM code update\n");
  g_code[0] = RISCV_ADDI_A0_ZERO_1;
  g_code[1] = RISCV_RET;
  up_coherent_dcache((uintptr_t)g_code, 2 * sizeof(uint32_t));
  if (fn() != 1)
    {
      return fail("CACHE-002", "initial RAM code execution mismatch");
    }

  g_code[0] = RISCV_ADDI_A0_ZERO_2;
  up_coherent_dcache((uintptr_t)g_code, sizeof(uint32_t));
  if (fn() != 2)
    {
      return fail("CACHE-002", "updated RAM code execution mismatch");
    }

  printf("  [CACHE-002] PASS updated RAM code is visible\n");
  return RESULT_PASS;
}

static int run_case_003(void)
{
  volatile uint8_t *cached = g_buffer;
  volatile uint8_t *nocache;
  irqstate_t flags;
  uint32_t mhcr;
  const char *reason = NULL;

  printf("[CACHE-003] Runtime I/D toggle and dirty persistence\n");
  nocache = (volatile uint8_t *)((uintptr_t)g_buffer - CACHE_RAM_ALIAS);
  flags = enter_critical_section();
  up_enable_icache();
  up_enable_dcache();
  mhcr = cache_mhcr();
  if ((mhcr & (MHCR_IE | MHCR_DE)) != (MHCR_IE | MHCR_DE))
    {
      reason = "initial I/D enabled state missing";
      goto out;
    }

  up_disable_icache();
  mhcr = cache_mhcr();
  if ((mhcr & MHCR_IE) != 0 || (mhcr & MHCR_DE) == 0)
    {
      reason = "I disable changed wrong MHCR bits";
      goto out;
    }

  up_disable_icache();
  up_enable_icache();
  nocache[0] = 0x40;
  up_invalidate_dcache((uintptr_t)g_buffer,
                       (uintptr_t)g_buffer + CACHE_LINE_SIZE);
  cached[0] = 0x61;
  up_enable_dcache();
  up_disable_dcache();
  mhcr = cache_mhcr();
  if ((mhcr & MHCR_IE) == 0 || (mhcr & MHCR_DE) != 0)
    {
      reason = "D disable changed wrong MHCR bits";
      goto out;
    }

  if (nocache[0] != 0x61)
    {
      reason = "D disable lost dirty cached data";
      goto out;
    }

  up_disable_dcache();
  up_enable_dcache();
  mhcr = cache_mhcr();
  if ((mhcr & (MHCR_IE | MHCR_DE)) != (MHCR_IE | MHCR_DE))
    {
      reason = "final I/D enabled state missing";
      goto out;
    }

  if (cached[0] != 0x61)
    {
      reason = "D re-enable changed persisted data";
    }

out:
  up_enable_icache();
  up_enable_dcache();
  leave_critical_section(flags);
  if (reason != NULL)
    {
      return fail("CACHE-003", reason);
    }

  printf("  [CACHE-003] PASS toggles and dirty persistence\n");
  return RESULT_PASS;
}

static int run_case_004(void)
{
  volatile uint8_t *nocache;
  unsigned int i;

  printf("[CACHE-004] Range visibility and partial RX ownership\n");
  nocache = (volatile uint8_t *)((uintptr_t)g_buffer - CACHE_RAM_ALIAS);
  memset(g_buffer, 0x11, sizeof(g_buffer));
  up_clean_dcache((uintptr_t)g_buffer,
                  (uintptr_t)g_buffer + sizeof(g_buffer));
  for (i = 0; i < sizeof(g_buffer); i++)
    {
      if (nocache[i] != 0x11)
        {
          return fail("CACHE-004", "range clean not visible via nocache");
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
          return fail("CACHE-004", "aligned RX visibility mismatch");
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
          return fail("CACHE-004", "partial RX sentinel mismatch");
        }
    }

  memset(g_buffer, 0x55, sizeof(g_buffer));
  up_invalidate_dcache((uintptr_t)&g_buffer[1],
                       (uintptr_t)&g_buffer[127]);
  for (i = 1; i < 127; i++)
    {
      nocache[i] = 0x66;
    }

  for (i = 0; i < sizeof(g_buffer); i++)
    {
      uint8_t expected = i > 0 && i < 127 ? 0x66 : 0x55;
      if (g_buffer[i] != expected)
        {
          return fail("CACHE-004", "three-line partial RX mismatch");
        }
    }

  printf("  [CACHE-004] PASS aligned and CI/I/CI RX visibility\n");
  return RESULT_PASS;
}

static int run_case_005(void)
{
  volatile uint8_t *nocache;
  irqstate_t flags;
  uintptr_t stack_top;

  printf("[CACHE-005] All operations with nocache stack\n");
  nocache = (volatile uint8_t *)((uintptr_t)g_buffer - CACHE_RAM_ALIAS);

  g_buffer[0] = 0x51;
  up_clean_dcache_all();
  if (nocache[0] != 0x51)
    {
      return fail("CACHE-005", "clean-all visibility mismatch");
    }

  g_buffer[0] = 0x52;
  up_flush_dcache((uintptr_t)g_buffer,
                  (uintptr_t)g_buffer + CACHE_LINE_SIZE);
  if (nocache[0] != 0x52)
    {
      return fail("CACHE-005", "flush-range visibility mismatch");
    }

  up_invalidate_icache((uintptr_t)g_code,
                       (uintptr_t)g_code + CACHE_LINE_SIZE);
  up_invalidate_icache_all();
  up_flush_dcache_all();
  if (nocache[0] != 0x52)
    {
      return fail("CACHE-005", "flush-all visibility mismatch");
    }

  up_clean_dcache_all();
  nocache[0] = 0x53;
  stack_top = (uintptr_t)g_nocache_stack + sizeof(g_nocache_stack);
  flags = enter_critical_section();
  bl_cache_test_invalidate_all(stack_top);
  leave_critical_section(flags);
  if (g_buffer[0] != 0x53)
    {
      return fail("CACHE-005",
                  "invalidate-all producer visibility mismatch");
    }

  printf("  [CACHE-005] PASS all operations and nocache stack\n");
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
  int last = 5;
  int passed = 0;
  int failed = 0;
  int i;

  if (strcmp(case_id, "all") != 0)
    {
      int result = run_case(case_id);
      if (result < 0)
        {
          printf("Usage: cache_test [001..005|all]\n");
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
