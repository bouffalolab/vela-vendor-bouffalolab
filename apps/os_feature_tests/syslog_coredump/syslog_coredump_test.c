/****************************************************************************
 * apps/vendor/bouffalolab/apps/os_feature_tests/syslog_coredump/syslog_coredump_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nuttx/kthread.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define COREDUMP_THREAD_NAME "d05_coredump"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void print_usage(const char *progname)
{
  printf("Usage: %s <safe|fatal>\n", progname);
}

static int coredump_fatal_thread(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  printf("SYSLOG_COREDUMP_TEST FATAL trigger pid=%d thread=%s\n",
         getpid(), COREDUMP_THREAD_NAME);
  fflush(stdout);

  PANIC();
  return EXIT_FAILURE;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  pid_t pid;

  if (argc != 2)
    {
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }

  if (strcmp(argv[1], "safe") == 0)
    {
      printf("SYSLOG_COREDUMP_TEST SAFE begin\n");
      printf("SYSLOG_COREDUMP_TEST SAFE returned\n");
      return EXIT_SUCCESS;
    }

  if (strcmp(argv[1], "fatal") != 0)
    {
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }

  printf("SYSLOG_COREDUMP_TEST FATAL create\n");
  fflush(stdout);

  pid = kthread_create(COREDUMP_THREAD_NAME,
                       CONFIG_BL_OS_FEATURE_TESTS_SYSLOG_COREDUMP_PRIORITY,
                       CONFIG_BL_OS_FEATURE_TESTS_SYSLOG_COREDUMP_STACKSIZE,
                       coredump_fatal_thread, NULL);
  if (pid < 0)
    {
      fprintf(stderr, "SYSLOG_COREDUMP_TEST FATAL create failed: %d\n",
              errno);
      return EXIT_FAILURE;
    }

  printf("SYSLOG_COREDUMP_TEST FATAL created pid=%d\n", pid);
  fflush(stdout);
  sched_yield();

  printf("SYSLOG_COREDUMP_TEST FATAL returned unexpectedly\n");
  return EXIT_FAILURE;
}
