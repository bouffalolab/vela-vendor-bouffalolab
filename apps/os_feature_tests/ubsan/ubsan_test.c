/****************************************************************************
 * apps/vendor/bouffalolab/apps/os_feature_tests/ubsan/ubsan_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

static volatile int32_t g_lhs;
static volatile int32_t g_rhs;
static volatile int32_t g_sink;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void print_usage(const char *progname)
{
  printf("Usage: %s <legal|add-overflow|shift-out-of-bounds>\n", progname);
}

static int32_t __attribute__((noinline)) add_values(void)
{
  return g_lhs + g_rhs;
}

static int32_t __attribute__((noinline)) shift_value(void)
{
  return g_lhs << g_rhs;
}

static int run_legal(void)
{
  g_lhs = 40;
  g_rhs = 2;
  g_sink = add_values();

  if (g_sink != 42)
    {
      printf("UBSAN_TEST RESULT case=legal FAIL value=%" PRId32 "\n",
             g_sink);
      return EXIT_FAILURE;
    }

  printf("UBSAN_TEST value=%" PRId32 "\n", g_sink);
  printf("UBSAN_TEST RESULT case=legal PASS\n");
  return EXIT_SUCCESS;
}

static int run_add_overflow(void)
{
  g_lhs = INT32_MAX;
  g_rhs = 1;
  g_sink = add_values();
  printf("UBSAN_TEST RESULT case=add-overflow FAULT "
         "verification=required\n");
  return EXIT_SUCCESS;
}

static int run_shift_out_of_bounds(void)
{
  g_lhs = 1;
  g_rhs = 32;
  g_sink = shift_value();
  printf("UBSAN_TEST RESULT case=shift-out-of-bounds FAULT "
         "verification=required\n");
  return EXIT_SUCCESS;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  if (argc != 2)
    {
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }

  printf("UBSAN_TEST BEGIN case=%s\n", argv[1]);

  if (strcmp(argv[1], "legal") == 0)
    {
      return run_legal();
    }

  if (strcmp(argv[1], "add-overflow") == 0)
    {
      return run_add_overflow();
    }

  if (strcmp(argv[1], "shift-out-of-bounds") == 0)
    {
      return run_shift_out_of_bounds();
    }

  print_usage(argv[0]);
  return EXIT_FAILURE;
}
