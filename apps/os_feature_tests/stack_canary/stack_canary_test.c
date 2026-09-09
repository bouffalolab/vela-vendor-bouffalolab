/****************************************************************************
 * apps/vendor/bouffalolab/apps/os_feature_tests/stack_canary/stack_canary_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TEST_BUFFER_SIZE 32

/****************************************************************************
 * Private Data
 ****************************************************************************/

static volatile size_t g_write_offset;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void print_usage(const char *progname)
{
  printf("Usage: %s <safe|corrupt>\n", progname);
}

static int __attribute__((noinline)) run_canary_test(void)
{
  volatile unsigned char buffer[TEST_BUFFER_SIZE];
  size_t offset = g_write_offset;

  memset((void *)buffer, 0x5a, sizeof(buffer));
  printf("STACK_CANARY_TEST write offset=%zu size=%zu\n",
         offset, sizeof(buffer));
  buffer[offset] ^= 0xff;
  return buffer[0] == 0x5a ? EXIT_SUCCESS : EXIT_FAILURE;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  bool corrupt;

  if (argc != 2)
    {
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }

  if (strcmp(argv[1], "safe") == 0)
    {
      corrupt = false;
    }
  else if (strcmp(argv[1], "corrupt") == 0)
    {
      corrupt = true;
    }
  else
    {
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }

  g_write_offset = corrupt ? TEST_BUFFER_SIZE : TEST_BUFFER_SIZE - 1;
  printf("STACK_CANARY_TEST %s begin\n", corrupt ? "CORRUPT" : "SAFE");
  if (run_canary_test() != EXIT_SUCCESS)
    {
      printf("STACK_CANARY_TEST safe path failed\n");
      return EXIT_FAILURE;
    }

  printf("STACK_CANARY_TEST %s returned\n", corrupt ? "CORRUPT" : "SAFE");
  return EXIT_SUCCESS;
}
