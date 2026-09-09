/****************************************************************************
 * apps/vendor/bouffalolab/apps/os_feature_tests/lazy_fpu/lazy_fpu_test.c
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
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arch/irq.h>
#include <nuttx/clock.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define WORKER_COUNT           2
#define FPU_STATE_COUNT        13
#define SIGNAL_FPU_STATE_COUNT 33
#define SIGNAL_READY_POLLS     1000
#define SIGNAL_TIMER_NS        10000000
#define SIGNAL_WAIT_LOOPS      100000000

#define SIGNAL_WAIT_DONE       0
#define SIGNAL_WAIT_ABORT      1
#define SIGNAL_WAIT_TIMEOUT    2

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct worker_s
{
  pthread_t thread;
  uint32_t output[FPU_STATE_COUNT];
  int id;
  int result;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

int bl_lazy_fpu_register_test(const uint32_t *input, uint32_t *output,
                              unsigned int iterations);
int bl_lazy_fpu_signal_wait(const uint32_t *input, uint32_t *output,
                            volatile sig_atomic_t *ready,
                            volatile sig_atomic_t *done,
                            volatile sig_atomic_t *abort,
                            unsigned int wait_loops);
void bl_lazy_fpu_signal_clobber(const uint32_t *input, uint32_t fcsr);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const uint32_t g_patterns[WORKER_COUNT][FPU_STATE_COUNT] =
{
  { 0x3f800001, 0x3f900002, 0x3fa00003, 0x3fb00004,
    0x3fc00005, 0x3fd00006, 0x3fe00007, 0x3ff00008,
    0x40000009, 0x4008000a, 0x4010000b, 0x4018000c,
    0x00000020 },
  { 0x41000011, 0x41100012, 0x41200013, 0x41300014,
    0x41400015, 0x41500016, 0x41600017, 0x41700018,
    0x41800019, 0x4190001a, 0x41a0001b, 0x41b0001c,
    0x00000040 }
};

static const uint32_t g_signal_original[SIGNAL_FPU_STATE_COUNT] =
{
  0x3f800101, 0x3f800202, 0x3f800303, 0x3f800404,
  0x3f800505, 0x3f800606, 0x3f800707, 0x3f800808,
  0x3f800909, 0x3f800a0a, 0x3f800b0b, 0x3f800c0c,
  0x3f800d0d, 0x3f800e0e, 0x3f800f0f, 0x3f801010,
  0x3f801111, 0x3f801212, 0x3f801313, 0x3f801414,
  0x3f801515, 0x3f801616, 0x3f801717, 0x3f801818,
  0x3f801919, 0x3f801a1a, 0x3f801b1b, 0x3f801c1c,
  0x3f801d1d, 0x3f801e1e, 0x3f801f1f, 0x3f802020,
  0x00000040
};

static const uint32_t g_signal_clobber[20] =
{
  0x42000101, 0x42000202, 0x42000303, 0x42000404,
  0x42000505, 0x42000606, 0x42000707, 0x42000808,
  0x42000a0a, 0x42000b0b, 0x42000c0c, 0x42000d0d,
  0x42000e0e, 0x42000f0f, 0x42001010, 0x42001111,
  0x42001c1c, 0x42001d1d, 0x42001e1e, 0x42001f1f
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static struct worker_s g_workers[WORKER_COUNT];
static pthread_t g_signal_thread;
static uint32_t g_signal_output[SIGNAL_FPU_STATE_COUNT];
static int g_ready_count;
static bool g_start;
static volatile sig_atomic_t g_signal_count;
static volatile sig_atomic_t g_signal_ready;
static volatile sig_atomic_t g_signal_done;
static volatile sig_atomic_t g_signal_abort;
static volatile sig_atomic_t g_signal_worker_tid;
static volatile sig_atomic_t g_signal_handler_tid;
static int g_signal_worker_result;

static const char *signal_wait_name(int result)
{
  switch (result)
    {
      case SIGNAL_WAIT_DONE:
        return "done";
      case SIGNAL_WAIT_ABORT:
        return "abort";
      default:
        return "timeout";
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void bl_lazy_fpu_test_yield(void)
{
  usleep(CONFIG_BL_OS_FEATURE_TESTS_LAZY_FPU_SLEEP_US);
}

static void running_signal_handler(int signo)
{
  (void)signo;
  g_signal_handler_tid = gettid();
  g_signal_count++;
  g_signal_done = 1;
}

static void noncurrent_signal_handler(int signo)
{
  (void)signo;
  bl_lazy_fpu_signal_clobber(g_signal_clobber, 0x00000020);
  bl_lazy_fpu_test_yield();
  bl_lazy_fpu_test_yield();
  bl_lazy_fpu_test_yield();
  g_signal_count++;
  g_signal_done = 1;
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void *worker_main(void *arg)
{
  struct worker_s *worker = arg;

  pthread_mutex_lock(&g_lock);
  g_ready_count++;
  pthread_cond_broadcast(&g_cond);
  while (!g_start)
    {
      pthread_cond_wait(&g_cond, &g_lock);
    }

  pthread_mutex_unlock(&g_lock);

  worker->result = bl_lazy_fpu_register_test(
    g_patterns[worker->id], worker->output,
    CONFIG_BL_OS_FEATURE_TESTS_LAZY_FPU_ITERATIONS);
  if (worker->result == 0 &&
      memcmp(worker->output, g_patterns[worker->id],
             sizeof(worker->output)) != 0)
    {
      worker->result = -1;
    }

  return NULL;
}

static void *signal_worker_main(void *arg)
{
  (void)arg;
  g_signal_worker_result =
    bl_lazy_fpu_signal_wait(g_signal_original, g_signal_output,
                            &g_signal_ready, &g_signal_done,
                            &g_signal_abort, SIGNAL_WAIT_LOOPS);
  return NULL;
}

static void *running_signal_worker_main(void *arg)
{
  struct itimerspec value;
  struct sigevent event;
  timer_t timerid;

  (void)arg;
  memset(&event, 0, sizeof(event));
  g_signal_worker_tid = gettid();
  event.sigev_notify = SIGEV_SIGNAL | SIGEV_THREAD_ID;
  event.sigev_signo = SIGUSR2;
  event.sigev_notify_thread_id = g_signal_worker_tid;

  if (timer_create(CLOCK_REALTIME, &event, &timerid) < 0)
    {
      g_signal_worker_result = -errno;
      return NULL;
    }

  memset(&value, 0, sizeof(value));
  value.it_value.tv_nsec = SIGNAL_TIMER_NS;
  if (timer_settime(timerid, 0, &value, NULL) < 0)
    {
      g_signal_worker_result = -errno;
      timer_delete(timerid);
      return NULL;
    }

  g_signal_worker_result =
    bl_lazy_fpu_signal_wait(g_signal_original, g_signal_output,
                            &g_signal_ready, &g_signal_done,
                            &g_signal_abort, SIGNAL_WAIT_LOOPS);
  timer_delete(timerid);
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
      return ret;
    }

  ret = pthread_attr_setstacksize(
    &attr, CONFIG_BL_OS_FEATURE_TESTS_LAZY_FPU_WORKER_STACKSIZE);
  if (ret != 0)
    {
      pthread_attr_destroy(&attr);
      return ret;
    }

  for (i = 0; i < WORKER_COUNT; i++)
    {
      g_workers[i].id = i;
      ret = pthread_create(&g_workers[i].thread, &attr, worker_main,
                           &g_workers[i]);
      if (ret != 0)
        {
          break;
        }

      created++;
    }

  pthread_attr_destroy(&attr);

  if (created != WORKER_COUNT)
    {
      pthread_mutex_lock(&g_lock);
      g_start = true;
      pthread_cond_broadcast(&g_cond);
      pthread_mutex_unlock(&g_lock);

      for (i = 0; i < created; i++)
        {
          pthread_join(g_workers[i].thread, NULL);
        }

      return ret;
    }

  return 0;
}

static int run_running_signal_test(void)
{
  int errors = 0;
  int ret;
  int i;

  g_signal_count = 0;
  g_signal_ready = 0;
  g_signal_done = 0;
  g_signal_abort = 0;
  g_signal_worker_tid = -1;
  g_signal_handler_tid = -1;
  g_signal_worker_result = 0;
  memset(g_signal_output, 0, sizeof(g_signal_output));

  ret = pthread_create(&g_signal_thread, NULL,
                       running_signal_worker_main, NULL);
  if (ret != 0)
    {
      return ret;
    }

  ret = pthread_join(g_signal_thread, NULL);
  if (ret != 0 || g_signal_worker_result != SIGNAL_WAIT_DONE)
    {
      fprintf(stderr, "LAZY_FPU_TEST running signal failed: join=%d "
              "worker=%d\n", ret, g_signal_worker_result);
      return -1;
    }

  for (i = 0; i < SIGNAL_FPU_STATE_COUNT; i++)
    {
      if (g_signal_output[i] != g_signal_original[i])
        {
          printf("LAZY_FPU_TEST RUNNING REG index=%d expected=%08lx "
                 "actual=%08lx\n",
                 i,
                 (unsigned long)g_signal_original[i],
                 (unsigned long)g_signal_output[i]);
          errors++;
        }
    }

  printf("LAZY_FPU_TEST SIGNAL target=running handler=integer wait=%s "
         "tid_match=%d count=%d fprs=32 fcsr=%08lx errors=%d\n",
         signal_wait_name(g_signal_worker_result),
         g_signal_worker_tid == g_signal_handler_tid,
         (int)g_signal_count,
         (unsigned long)g_signal_output[SIGNAL_FPU_STATE_COUNT - 1],
         errors);

  return errors == 0 && g_signal_count == 1 &&
         g_signal_worker_tid == g_signal_handler_tid ? 0 : -1;
}

static int run_noncurrent_signal_test(void)
{
  struct sched_param param;
  pthread_attr_t attr;
  int errors = 0;
  int ret;
  int i;

  g_signal_count = 0;
  g_signal_ready = 0;
  g_signal_done = 0;
  g_signal_abort = 0;
  g_signal_worker_result = 0;
  memset(&param, 0, sizeof(param));
  memset(g_signal_output, 0, sizeof(g_signal_output));

  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      return ret;
    }

  ret = pthread_attr_setstacksize(
    &attr, CONFIG_BL_OS_FEATURE_TESTS_LAZY_FPU_WORKER_STACKSIZE);
  if (ret == 0)
    {
      ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }

  if (ret == 0)
    {
      param.sched_priority =
        CONFIG_BL_OS_FEATURE_TESTS_LAZY_FPU_PRIORITY - 1;
      ret = pthread_attr_setschedparam(&attr, &param);
    }

  if (ret == 0)
    {
      ret = pthread_create(&g_signal_thread, &attr,
                           signal_worker_main, NULL);
    }

  pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      return ret;
    }

  for (i = 0; i < SIGNAL_READY_POLLS && !g_signal_ready; i++)
    {
      usleep(CONFIG_BL_OS_FEATURE_TESTS_LAZY_FPU_SLEEP_US);
    }

  if (!g_signal_ready)
    {
      fprintf(stderr, "LAZY_FPU_TEST signal worker timeout\n");
      g_signal_abort = 1;
      errors++;
    }
  else
    {
      ret = pthread_kill(g_signal_thread, SIGUSR1);
      if (ret != 0)
        {
          fprintf(stderr, "LAZY_FPU_TEST pthread_kill failed: %d\n", ret);
          g_signal_abort = 1;
          errors++;
        }
    }

  ret = pthread_join(g_signal_thread, NULL);
  if (ret != 0 || g_signal_worker_result != SIGNAL_WAIT_DONE)
    {
      fprintf(stderr, "LAZY_FPU_TEST signal failed: join=%d worker=%d\n",
              ret, g_signal_worker_result);
      errors++;
    }

  for (i = 0; i < SIGNAL_FPU_STATE_COUNT; i++)
    {
      if (g_signal_output[i] != g_signal_original[i])
        {
          printf("LAZY_FPU_TEST SIGNAL REG index=%d expected=%08lx "
                 "actual=%08lx\n",
                 i,
                 (unsigned long)g_signal_original[i],
                 (unsigned long)g_signal_output[i]);
          errors++;
        }
    }

  printf("LAZY_FPU_TEST SIGNAL target=noncurrent handler=fpu wait=%s "
         "count=%d fprs=32 fcsr=%08lx errors=%d\n",
         signal_wait_name(g_signal_worker_result),
         (int)g_signal_count,
         (unsigned long)g_signal_output[SIGNAL_FPU_STATE_COUNT - 1],
         errors);

  return errors == 0 && g_signal_count == 1 ? 0 : -1;
}

static void print_context_layout(void)
{
#ifdef CONFIG_ARCH_LAZYFPU
  const char *mode = "lazy";
#else
  const char *mode = "eager";
#endif

  printf("LAZY_FPU_TEST MODE=%s int=%u fpu=%u frame=%u save=%u\n",
         mode, (unsigned int)INT_XCPT_SIZE, (unsigned int)FPU_XCPT_SIZE,
         (unsigned int)XCPTCONTEXT_SIZE,
         (unsigned int)SAVEUSERCONTEXT_SIZE);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  struct sigaction noncurrent_action;
  struct sigaction running_action;
  clock_t start_ticks;
  clock_t end_ticks;
  int ret;
  int i;

  if (argc != 1)
    {
      fprintf(stderr, "Usage: %s\n", argv[0]);
      return EXIT_FAILURE;
    }

  memset(g_workers, 0, sizeof(g_workers));
  memset(&noncurrent_action, 0, sizeof(noncurrent_action));
  noncurrent_action.sa_handler = noncurrent_signal_handler;
  sigemptyset(&noncurrent_action.sa_mask);
  if (sigaction(SIGUSR1, &noncurrent_action, NULL) < 0)
    {
      perror("LAZY_FPU_TEST SIGUSR1 sigaction");
      return EXIT_FAILURE;
    }

  memset(&running_action, 0, sizeof(running_action));
  running_action.sa_handler = running_signal_handler;
  sigemptyset(&running_action.sa_mask);
  if (sigaction(SIGUSR2, &running_action, NULL) < 0)
    {
      perror("LAZY_FPU_TEST SIGUSR2 sigaction");
      return EXIT_FAILURE;
    }

  g_ready_count = 0;
  g_start = false;
  printf("LAZY_FPU_TEST BEGIN iterations=%u sleep_us=%u\n",
         CONFIG_BL_OS_FEATURE_TESTS_LAZY_FPU_ITERATIONS,
         CONFIG_BL_OS_FEATURE_TESTS_LAZY_FPU_SLEEP_US);
  print_context_layout();

  ret = create_workers();
  if (ret != 0)
    {
      fprintf(stderr, "LAZY_FPU_TEST create failed: %d\n", ret);
      return EXIT_FAILURE;
    }

  pthread_mutex_lock(&g_lock);
  while (g_ready_count != WORKER_COUNT)
    {
      pthread_cond_wait(&g_cond, &g_lock);
    }

  start_ticks = clock_systime_ticks();
  g_start = true;
  pthread_cond_broadcast(&g_cond);
  pthread_mutex_unlock(&g_lock);

  ret = 0;
  for (i = 0; i < WORKER_COUNT; i++)
    {
      int join_ret = pthread_join(g_workers[i].thread, NULL);

      printf("LAZY_FPU_TEST TASK id=%d join=%d errors=%d\n", i + 1,
             join_ret, g_workers[i].result != 0);
      if (join_ret != 0 || g_workers[i].result != 0)
        {
          ret = -1;
        }
    }

  if (run_running_signal_test() != 0)
    {
      ret = -1;
    }

  if (run_noncurrent_signal_test() != 0)
    {
      ret = -1;
    }

  end_ticks = clock_systime_ticks();
  printf("LAZY_FPU_TEST TIMER ticks=%lu\n",
         (unsigned long)(end_ticks - start_ticks));

  if (end_ticks == start_ticks)
    {
      fprintf(stderr, "LAZY_FPU_TEST timer IRQ coverage missing\n");
      ret = -1;
    }

  printf("LAZY_FPU_TEST RESULT %s\n", ret == 0 ? "PASS" : "FAIL");
  return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
