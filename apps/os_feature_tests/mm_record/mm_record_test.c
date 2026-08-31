/****************************************************************************
 * apps/os_feature_tests/mm_record/mm_record_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define WORKER_COUNT 2
#define DEFAULT_SIZE0 64
#define DEFAULT_SIZE1 96

#ifdef CONFIG_BL_OS_FEATURE_TESTS_MM_RECORD_REALLOC_STACK
int mm_realloc_stack_test(const char *case_id);
#endif
#ifdef CONFIG_BL_OS_FEATURE_TESTS_MM_RECORD_STACK
int mm_record_stack_test(const char *case_id);
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum request_e
{
  REQUEST_NONE = 0,
  REQUEST_REALLOC,
  REQUEST_DONE
};

struct worker_s
{
  pthread_t thread;
  pid_t tid;
  void *ptr;
  size_t size;
  bool ready;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static struct worker_s g_worker[WORKER_COUNT];
static enum request_e g_request;
static size_t g_request_size;
static pid_t g_controller_tid;
static int g_ready_count;
static int g_request_slot;
static int g_request_result;
static bool g_active;
static bool g_ready;
static bool g_stop;
static unsigned int g_generation;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void print_usage(const char *progname)
{
  printf("Usage:\n");
  printf("  %s start [size0 size1] &\n", progname);
  printf("  %s status\n", progname);
  printf("  %s realloc <slot> <size>\n", progname);
  printf("  %s free\n", progname);
#ifdef CONFIG_BL_OS_FEATURE_TESTS_MM_RECORD_REALLOC_STACK
  printf("  %s realloc_stack [R01|R02|R03|R04|R05|R06]\n", progname);
#endif
#ifdef CONFIG_BL_OS_FEATURE_TESTS_MM_RECORD_STACK
  printf("  %s stack_test [M02-001..M02-013|all]\n", progname);
#endif
}

static int parse_size(const char *arg, size_t *value)
{
  char *end;
  unsigned long parsed;

  errno = 0;
  parsed = strtoul(arg, &end, 0);
  if (errno != 0 || *arg == '\0' || *end != '\0' || parsed == 0 ||
      parsed > SIZE_MAX)
    {
      return -EINVAL;
    }

  *value = (size_t)parsed;
  return 0;
}

static int parse_slot(const char *arg, int *slot)
{
  char *end;
  long parsed;

  errno = 0;
  parsed = strtol(arg, &end, 0);
  if (errno != 0 || *arg == '\0' || *end != '\0' ||
      (parsed != 0 && parsed != 1))
    {
      return -EINVAL;
    }

  *slot = (int)parsed;
  return 0;
}

static void print_status_locked(const char *state)
{
  printf("MM_RECORD_TEST %s controller=%d ", state, g_controller_tid);
  printf("worker0=%d ptr0=%p size0=%zu ", g_worker[0].tid,
         g_worker[0].ptr, g_worker[0].size);
  printf("worker1=%d ptr1=%p size1=%zu\n", g_worker[1].tid,
         g_worker[1].ptr, g_worker[1].size);
}

static void *worker_main(void *arg)
{
  struct worker_s *worker = arg;
  void *ptr;

  ptr = malloc(worker->size);

  pthread_mutex_lock(&g_lock);
  worker->tid = gettid();
  worker->ptr = ptr;
  worker->ready = true;
  g_ready_count++;
  pthread_cond_broadcast(&g_cond);

  while (!g_stop)
    {
      pthread_cond_wait(&g_cond, &g_lock);
    }

  ptr = worker->ptr;
  worker->ptr = NULL;
  pthread_mutex_unlock(&g_lock);

  free(ptr);
  return NULL;
}

static int create_workers(void)
{
  pthread_attr_t attr;
  int created = 0;
  int ret;
  int i;

  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      fprintf(stderr, "pthread_attr_init failed: %d\n", ret);
      return -ret;
    }

  ret = pthread_attr_setstacksize(
    &attr, CONFIG_BL_OS_FEATURE_TESTS_MM_RECORD_WORKER_STACKSIZE);
  if (ret != 0)
    {
      fprintf(stderr, "pthread_attr_setstacksize failed: %d\n", ret);
      pthread_attr_destroy(&attr);
      return -ret;
    }

  for (i = 0; i < WORKER_COUNT; i++)
    {
      ret = pthread_create(&g_worker[i].thread, &attr, worker_main,
                           &g_worker[i]);
      if (ret != 0)
        {
          fprintf(stderr, "pthread_create[%d] failed: %d\n", i, ret);
          break;
        }

      created++;
    }

  pthread_attr_destroy(&attr);

  if (created != WORKER_COUNT)
    {
      pthread_mutex_lock(&g_lock);
      g_stop = true;
      pthread_cond_broadcast(&g_cond);
      pthread_mutex_unlock(&g_lock);

      for (i = 0; i < created; i++)
        {
          pthread_join(g_worker[i].thread, NULL);
        }

      return -ret;
    }

  return 0;
}

static void process_realloc_locked(void)
{
  struct worker_s *worker = &g_worker[g_request_slot];
  void *ptr;

  ptr = realloc(worker->ptr, g_request_size);
  if (ptr == NULL)
    {
      g_request_result = -ENOMEM;
    }
  else
    {
      worker->ptr = ptr;
      worker->size = g_request_size;
      g_request_result = 0;
    }

  g_request = REQUEST_DONE;
  pthread_cond_broadcast(&g_cond);
}

static int controller_main(size_t size0, size_t size1)
{
  int ret;
  int i;

  pthread_mutex_lock(&g_lock);
  if (g_active || g_request != REQUEST_NONE)
    {
      pthread_mutex_unlock(&g_lock);
      fprintf(stderr, "MM_RECORD_TEST already active\n");
      return -EBUSY;
    }

  memset(g_worker, 0, sizeof(g_worker));
  g_worker[0].size = size0;
  g_worker[1].size = size1;
  g_request = REQUEST_NONE;
  g_controller_tid = gettid();
  g_ready_count = 0;
  g_active = true;
  g_ready = false;
  g_stop = false;
  g_generation++;
  pthread_mutex_unlock(&g_lock);

  ret = create_workers();
  if (ret < 0)
    {
      pthread_mutex_lock(&g_lock);
      g_active = false;
      g_ready = false;
      g_controller_tid = 0;
      pthread_cond_broadcast(&g_cond);
      pthread_mutex_unlock(&g_lock);
      return ret;
    }

  pthread_mutex_lock(&g_lock);
  while (g_ready_count != WORKER_COUNT)
    {
      pthread_cond_wait(&g_cond, &g_lock);
    }

  if (g_worker[0].ptr == NULL || g_worker[1].ptr == NULL)
    {
      fprintf(stderr, "MM_RECORD_TEST worker allocation failed\n");
      ret = -ENOMEM;
      g_stop = true;
      pthread_cond_broadcast(&g_cond);
    }
  else
    {
      g_ready = true;
      print_status_locked("READY");
    }

  while (!g_stop)
    {
      while (!g_stop && g_request != REQUEST_REALLOC)
        {
          pthread_cond_wait(&g_cond, &g_lock);
        }

      if (g_stop && g_request == REQUEST_REALLOC)
        {
          g_request_result = -ECANCELED;
          g_request = REQUEST_DONE;
          pthread_cond_broadcast(&g_cond);
        }
      else if (g_request == REQUEST_REALLOC)
        {
          process_realloc_locked();
        }
    }

  pthread_mutex_unlock(&g_lock);

  for (i = 0; i < WORKER_COUNT; i++)
    {
      pthread_join(g_worker[i].thread, NULL);
    }

  pthread_mutex_lock(&g_lock);
  print_status_locked("FREED");
  g_active = false;
  g_ready = false;
  g_controller_tid = 0;
  pthread_cond_broadcast(&g_cond);
  pthread_mutex_unlock(&g_lock);
  return ret;
}

static int status_main(void)
{
  pthread_mutex_lock(&g_lock);
  if (!g_active)
    {
      pthread_mutex_unlock(&g_lock);
      fprintf(stderr, "MM_RECORD_TEST not active\n");
      return -ENOENT;
    }

  print_status_locked("STATUS");
  pthread_mutex_unlock(&g_lock);
  return 0;
}

static int realloc_main(const char *slot_arg, const char *size_arg)
{
  size_t size;
  int slot;
  int ret;

  if (parse_slot(slot_arg, &slot) < 0 ||
      parse_size(size_arg, &size) < 0)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_lock);
  if (!g_active || !g_ready || g_stop || g_request != REQUEST_NONE)
    {
      pthread_mutex_unlock(&g_lock);
      return -EBUSY;
    }

  g_request_slot = slot;
  g_request_size = size;
  g_request = REQUEST_REALLOC;
  pthread_cond_broadcast(&g_cond);

  while (g_request != REQUEST_DONE)
    {
      pthread_cond_wait(&g_cond, &g_lock);
    }

  ret = g_request_result;
  if (ret == 0)
    {
      print_status_locked("REALLOC");
    }

  g_request = REQUEST_NONE;
  pthread_cond_broadcast(&g_cond);
  pthread_mutex_unlock(&g_lock);
  return ret;
}

static int free_main(void)
{
  unsigned int generation;

  pthread_mutex_lock(&g_lock);
  if (!g_active)
    {
      pthread_mutex_unlock(&g_lock);
      return -ENOENT;
    }

  generation = g_generation;
  g_stop = true;
  pthread_cond_broadcast(&g_cond);
  while (g_active && g_generation == generation)
    {
      pthread_cond_wait(&g_cond, &g_lock);
    }

  pthread_mutex_unlock(&g_lock);
  printf("MM_RECORD_TEST STOPPED\n");
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  size_t size0 = DEFAULT_SIZE0;
  size_t size1 = DEFAULT_SIZE1;
  int ret;

  if (argc < 2)
    {
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }

  if (strcmp(argv[1], "start") == 0)
    {
      if (argc != 2 && argc != 4)
        {
          print_usage(argv[0]);
          return EXIT_FAILURE;
        }

      if (argc == 4 &&
          (parse_size(argv[2], &size0) < 0 ||
           parse_size(argv[3], &size1) < 0))
        {
          fprintf(stderr, "invalid allocation size\n");
          return EXIT_FAILURE;
        }

      ret = controller_main(size0, size1);
    }
  else if (strcmp(argv[1], "status") == 0 && argc == 2)
    {
      ret = status_main();
    }
  else if (strcmp(argv[1], "realloc") == 0 && argc == 4)
    {
      ret = realloc_main(argv[2], argv[3]);
    }
  else if (strcmp(argv[1], "free") == 0 && argc == 2)
    {
      ret = free_main();
    }
#ifdef CONFIG_BL_OS_FEATURE_TESTS_MM_RECORD_REALLOC_STACK
  else if (strcmp(argv[1], "realloc_stack") == 0 &&
           (argc == 2 || argc == 3))
    {
      ret = mm_realloc_stack_test(argc == 3 ? argv[2] : NULL);
    }
#endif
#ifdef CONFIG_BL_OS_FEATURE_TESTS_MM_RECORD_STACK
  else if (strcmp(argv[1], "stack_test") == 0 &&
           (argc == 2 || argc == 3))
    {
      ret = mm_record_stack_test(argc == 3 ? argv[2] : NULL);
    }
#endif
  else
    {
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }

  if (ret == -EAGAIN)
    {
      fprintf(stderr, "MM_RECORD_TEST PARTIAL external evidence required\n");
      return EXIT_FAILURE;
    }

  if (ret == -ENOTSUP)
    {
      fprintf(stderr, "MM_RECORD_TEST SKIP unsupported configuration\n");
      return EXIT_SUCCESS;
    }

  if (ret < 0)
    {
      fprintf(stderr, "MM_RECORD_TEST failed: %d\n", ret);
      return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}
