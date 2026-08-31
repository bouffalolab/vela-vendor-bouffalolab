/****************************************************************************
 * apps/os_feature_tests/mm_record/mm_record_stack_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/mm/mm.h>
#include <nuttx/tls.h>

#include <mm.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define STACK_TEST_ARENA_SIZE 16384
#define STACK_TEST_MAX_RECORDS 8
#define STACK_TEST_MAX_DEPTH CONFIG_LIBC_BACKTRACE_DEPTH
#define STACK_TEST_PROFILE_OPS 1000
#define STACK_TEST_SWEEP_MAX 128
#define STACK_TEST_WORKERS 2

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum stack_action_e
{
  STACK_ACTION_NONE = 0,
  STACK_ACTION_ALLOC,
  STACK_ACTION_REALLOC,
  STACK_ACTION_FREE,
  STACK_ACTION_STOP
};

struct stack_record_s
{
  void *user;
  void *stack;
  pid_t pid;
  unsigned long seqno;
  int depth;
  void *frames[STACK_TEST_MAX_DEPTH];
};

struct stack_snapshot_s
{
  struct stack_record_s record[STACK_TEST_MAX_RECORDS];
  int count;
};

struct stack_fixture_s
{
  struct mm_heap_s *heap;
  struct tls_info_s *tls;
  uint32_t tls_flags;
  bool default_backtrace;
};

struct stack_worker_s
{
  pthread_t thread;
  struct mm_heap_s *heap;
  pthread_mutex_t *lock;
  pthread_cond_t *cond;
  int index;
  pid_t tid;
  enum stack_action_e action;
  size_t size;
  void *ptr;
  int result;
  bool ready;
  bool done;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint8_t g_stack_arena[STACK_TEST_ARENA_SIZE]
  __attribute__((aligned(MM_ALIGN)));

static pthread_mutex_t g_stack_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_stack_cond = PTHREAD_COND_INITIALIZER;
static volatile unsigned int g_stack_guard;
static unsigned int g_stack_test_invocations;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

int mm_realloc_stack_test(const char *case_id);

static int stack_fail(const char *case_id, const char *reason)
{
  printf("MM_RECORD_STACK %s FAIL %s\n", case_id, reason);
  return -1;
}

static uint64_t stack_elapsed_ns(const struct timespec *start,
                                 const struct timespec *end)
{
  return (uint64_t)(end->tv_sec - start->tv_sec) * 1000000000ull +
         end->tv_nsec - start->tv_nsec;
}

static noinline_function void *stack_alloc0(struct mm_heap_s *heap,
                                             size_t size)
{
  void *ptr = mm_malloc(heap, size);

  g_stack_guard += ptr != NULL;
  return ptr;
}

static noinline_function void *stack_alloc1(struct mm_heap_s *heap,
                                             size_t size)
{
  void *ptr = mm_malloc(heap, size);

  g_stack_guard += ptr != NULL;
  return ptr;
}

static noinline_function void *stack_alloc_same(struct mm_heap_s *heap)
{
  void *ptr = mm_malloc(heap, 48);

  g_stack_guard += ptr != NULL;
  return ptr;
}

#if CONFIG_LIBC_BACKTRACE_INIT_SIZE == 4
#define STACK_UNIQUE_ALLOC(n) \
  static noinline_function void *stack_unique_alloc##n( \
    struct mm_heap_s *heap) \
  { \
    void *ptr = mm_malloc(heap, 32 + n); \
    g_stack_guard += n + 1; \
    return ptr; \
  }

STACK_UNIQUE_ALLOC(0)
STACK_UNIQUE_ALLOC(1)
STACK_UNIQUE_ALLOC(2)
STACK_UNIQUE_ALLOC(3)
#endif

static noinline_function void *stack_realloc(struct mm_heap_s *heap,
                                              void *ptr, size_t size)
{
  void *result = mm_realloc(heap, ptr, size);

  g_stack_guard += result != NULL;
  return result;
}

nosanitize_address static void
stack_snapshot_node(struct mm_allocnode_s *node, void *arg)
{
  struct stack_snapshot_s *snapshot = arg;
  struct stack_record_s *record;
  void **frames;
  int depth;

  if (snapshot->count >= STACK_TEST_MAX_RECORDS ||
      !MM_NODE_IS_ALLOC(node) ||
      MM_SIZEOF_NODE(node) == MM_SIZEOF_ALLOCNODE)
    {
      return;
    }

  record = &snapshot->record[snapshot->count++];
  memset(record, 0, sizeof(*record));
  record->user = (uint8_t *)node + MM_SIZEOF_ALLOCNODE;
#ifdef CONFIG_MM_RECORD_PID
  record->pid = node->pid;
#endif
#ifdef CONFIG_MM_RECORD_SEQNO
  record->seqno = node->seqno;
#endif
#ifdef CONFIG_MM_RECORD_STACK
  record->stack = node->stack;
  frames = backtrace_get(node->stack, &depth);
  if (frames != NULL && depth > 0 && depth <= STACK_TEST_MAX_DEPTH)
    {
      record->depth = depth;
      memcpy(record->frames, frames, depth * sizeof(void *));
    }
#else
  frames = NULL;
  depth = 0;
#endif
  UNUSED(frames);
  UNUSED(depth);
}

static void stack_snapshot(struct mm_heap_s *heap,
                           struct stack_snapshot_s *snapshot)
{
  memset(snapshot, 0, sizeof(*snapshot));
  mm_foreach(heap, stack_snapshot_node, snapshot);
}

static struct stack_record_s *stack_find(struct stack_snapshot_s *snapshot,
                                         void *user)
{
  int i;

  for (i = 0; i < snapshot->count; i++)
    {
      if (snapshot->record[i].user == user)
        {
          return &snapshot->record[i];
        }
    }

  return NULL;
}

static bool stack_same(const struct stack_record_s *left,
                       const struct stack_record_s *right)
{
  return left != NULL && right != NULL && left->stack == right->stack &&
         left->depth == right->depth && left->depth > 0 &&
         memcmp(left->frames, right->frames,
                left->depth * sizeof(void *)) == 0;
}

static void stack_print_target(const char *case_id, int worker,
                               const struct stack_record_s *record)
{
  int i;

  printf("MM_RECORD_STACK %s TARGET", case_id);
  if (worker >= 0)
    {
      printf(" worker=%d", worker);
    }

  printf(" depth=%d trace=", record->depth);
  for (i = 0; i < record->depth; i++)
    {
      printf("%s%p", i == 0 ? "" : " ", record->frames[i]);
    }

  printf("\n");
}

static void stack_pool_dump(const char *case_id, const char *phase,
                            int expected_delta)
{
  printf("MM_RECORD_STACK %s MARK %s", case_id, phase);
  if (expected_delta >= 0)
    {
      printf(" expected_delta=%d", expected_delta);
    }

  printf("\n");
  sched_lock();
  backtrace_dump();
  sched_unlock();
}

static int stack_fixture_init(struct stack_fixture_s *fixture,
                              const char *name, bool kasan)
{
  struct mm_heap_config_s config;

  memset(fixture, 0, sizeof(*fixture));
  memset(g_stack_arena, 0, sizeof(g_stack_arena));
  fixture->tls = tls_get_info();
  if (fixture->tls != NULL)
    {
      fixture->tls_flags = fixture->tls->tl_flags;
      fixture->tls->tl_flags &= ~TLS_FLAG_HEAP_DUMP;
    }

  memset(&config, 0, sizeof(config));
  config.name = name;
  config.start = g_stack_arena;
  config.size = sizeof(g_stack_arena);
  config.nokasan = !kasan;
  config.allocheap = false;
  mm_initialize_heap(&config, &fixture->heap);
  if (fixture->heap == NULL || fixture->heap->mm_procfs == NULL)
    {
      /* A NULL procfs entry cannot be torn down safely until ST028 lands. */

      if (fixture->tls != NULL)
        {
          fixture->tls->tl_flags = fixture->tls_flags;
          fixture->tls = NULL;
        }

      return -ENOMEM;
    }

  fixture->default_backtrace = fixture->heap->mm_procfs->backtrace;
  return 0;
}

static int stack_memdump_command(const char *command)
{
  char buffer[32];
  size_t length;
  ssize_t written;
  int fd;

  length = strlen(command);
  if (length >= sizeof(buffer))
    {
      return -E2BIG;
    }

  memcpy(buffer, command, length + 1);
  fd = open("/proc/memdump", O_WRONLY);
  if (fd < 0)
    {
      return -errno;
    }

  written = write(fd, buffer, length);
  if (written < 0)
    {
      int error = errno;
      close(fd);
      return -error;
    }

  close(fd);
  return written == (ssize_t)length ? 0 : -EIO;
}

static int stack_memdump_tid(pid_t tid, bool enable)
{
  char command[32];

  snprintf(command, sizeof(command), "%d%s", tid,
           enable ? "on" : "off");
  return stack_memdump_command(command);
}

static void stack_fixture_uninit(struct stack_fixture_s *fixture)
{
  if (fixture->heap != NULL)
    {
      fixture->heap->mm_procfs->backtrace = fixture->default_backtrace;
      mm_uninitialize(fixture->heap);
      fixture->heap = NULL;
    }

  if (fixture->tls != NULL)
    {
      fixture->tls->tl_flags = fixture->tls_flags;
    }
}

static void *stack_worker_alloc(struct stack_worker_s *worker)
{
  if (worker->index == 0)
    {
      return stack_alloc0(worker->heap, worker->size);
    }

  return stack_alloc1(worker->heap, worker->size);
}

static void *stack_worker_main(void *arg)
{
  struct stack_worker_s *worker = arg;
  enum stack_action_e action;
  size_t size;
  void *ptr;

  worker->tid = gettid();
  pthread_mutex_lock(worker->lock);
  worker->ready = true;
  pthread_cond_broadcast(worker->cond);

  while (worker->action != STACK_ACTION_STOP)
    {
      while (worker->action == STACK_ACTION_NONE)
        {
          pthread_cond_wait(worker->cond, worker->lock);
        }

      if (worker->action == STACK_ACTION_STOP)
        {
          break;
        }

      action = worker->action;
      size = worker->size;
      worker->action = STACK_ACTION_NONE;
      pthread_mutex_unlock(worker->lock);

      if (action == STACK_ACTION_ALLOC)
        {
          ptr = stack_worker_alloc(worker);
          worker->ptr = ptr;
          worker->result = ptr == NULL ? -ENOMEM : 0;
        }
      else if (action == STACK_ACTION_REALLOC)
        {
          ptr = stack_realloc(worker->heap, worker->ptr, size);
          if (ptr != NULL)
            {
              worker->ptr = ptr;
              worker->result = 0;
            }
          else
            {
              worker->result = -ENOMEM;
            }
        }
      else if (action == STACK_ACTION_FREE)
        {
          mm_free(worker->heap, worker->ptr);
          worker->ptr = NULL;
          worker->result = 0;
        }

      pthread_mutex_lock(worker->lock);
      worker->done = true;
      pthread_cond_broadcast(worker->cond);
    }

  ptr = worker->ptr;
  worker->ptr = NULL;
  pthread_mutex_unlock(worker->lock);
  if (ptr != NULL)
    {
      mm_free(worker->heap, ptr);
    }

  return NULL;
}

static int stack_workers_start(
  struct stack_worker_s worker[STACK_TEST_WORKERS], struct mm_heap_s *heap)
{
  pthread_attr_t attr;
  int i;
  int ret;

  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      return -ret;
    }

  ret = pthread_attr_setstacksize(
    &attr, CONFIG_BL_OS_FEATURE_TESTS_MM_RECORD_WORKER_STACKSIZE);
  if (ret != 0)
    {
      pthread_attr_destroy(&attr);
      return -ret;
    }

  for (i = 0; i < STACK_TEST_WORKERS; i++)
    {
      memset(&worker[i], 0, sizeof(worker[i]));
      worker[i].heap = heap;
      worker[i].lock = &g_stack_lock;
      worker[i].cond = &g_stack_cond;
      worker[i].index = i;
      ret = pthread_create(&worker[i].thread, &attr, stack_worker_main,
                           &worker[i]);
      if (ret != 0)
        {
          int created = i;

          pthread_attr_destroy(&attr);
          pthread_mutex_lock(&g_stack_lock);
          while (i > 0)
            {
              worker[--i].action = STACK_ACTION_STOP;
            }

          pthread_cond_broadcast(&g_stack_cond);
          pthread_mutex_unlock(&g_stack_lock);
          for (i = 0; i < created; i++)
            {
              int joinret = pthread_join(worker[i].thread, NULL);

              if (joinret != 0)
                {
                  printf("MM_RECORD_STACK workers cleanup_join[%d]=%d\n",
                         i, joinret);
                }
            }

          return -ret;
        }
    }

  pthread_attr_destroy(&attr);
  pthread_mutex_lock(&g_stack_lock);
  while (!worker[0].ready || !worker[1].ready)
    {
      pthread_cond_wait(&g_stack_cond, &g_stack_lock);
    }

  pthread_mutex_unlock(&g_stack_lock);
  return 0;
}

static int stack_worker_request(struct stack_worker_s *worker,
                                enum stack_action_e action, size_t size)
{
  int ret;

  pthread_mutex_lock(&g_stack_lock);
  if (worker->action != STACK_ACTION_NONE)
    {
      pthread_mutex_unlock(&g_stack_lock);
      return -EBUSY;
    }

  worker->size = size;
  worker->done = false;
  worker->action = action;
  pthread_cond_broadcast(&g_stack_cond);
  while (!worker->done && worker->action != STACK_ACTION_STOP)
    {
      pthread_cond_wait(&g_stack_cond, &g_stack_lock);
    }

  ret = worker->result;
  pthread_mutex_unlock(&g_stack_lock);
  return ret;
}

static void *stack_worker_detach(struct stack_worker_s *worker)
{
  void *ptr;

  pthread_mutex_lock(&g_stack_lock);
  ptr = worker->ptr;
  worker->ptr = NULL;
  pthread_mutex_unlock(&g_stack_lock);
  return ptr;
}

static int stack_workers_request_both(
  struct stack_worker_s worker[STACK_TEST_WORKERS],
  enum stack_action_e action, size_t size0, size_t size1)
{
  int ret = 0;
  int i;

  pthread_mutex_lock(&g_stack_lock);
  worker[0].size = size0;
  worker[1].size = size1;
  worker[0].done = false;
  worker[1].done = false;
  worker[0].action = action;
  worker[1].action = action;
  pthread_cond_broadcast(&g_stack_cond);
  while (!worker[0].done || !worker[1].done)
    {
      pthread_cond_wait(&g_stack_cond, &g_stack_lock);
    }

  for (i = 0; i < STACK_TEST_WORKERS; i++)
    {
      if (worker[i].result < 0 && ret == 0)
        {
          ret = worker[i].result;
        }
    }

  pthread_mutex_unlock(&g_stack_lock);
  return ret;
}

static int stack_workers_stop(
  struct stack_worker_s worker[STACK_TEST_WORKERS])
{
  int result = 0;
  int i;
  int ret;

  pthread_mutex_lock(&g_stack_lock);
  for (i = 0; i < STACK_TEST_WORKERS; i++)
    {
      worker[i].action = STACK_ACTION_STOP;
    }

  pthread_cond_broadcast(&g_stack_cond);
  pthread_mutex_unlock(&g_stack_lock);

  for (i = 0; i < STACK_TEST_WORKERS; i++)
    {
      ret = pthread_join(worker[i].thread, NULL);
      if (ret != 0 && result == 0)
        {
          result = -ret;
        }
    }

  mm_free_delaylist(worker[0].heap);
  return result;
}

static int stack_case_gate(void)
{
  struct stack_fixture_s fixture;
  struct stack_worker_s worker[STACK_TEST_WORKERS];
  struct stack_snapshot_s snapshot;
  struct stack_record_s *record;
  bool default_enabled;
  void *ptr[3];
  int ret = -1;

  memset(ptr, 0, sizeof(ptr));

  if (stack_fixture_init(&fixture, "record-gate", false) < 0)
    {
      return stack_fail("M02-002", "fixture");
    }

  if (stack_workers_start(worker, fixture.heap) < 0)
    {
      stack_fixture_uninit(&fixture);
      return stack_fail("M02-002", "workers");
    }

  printf("MM_RECORD_STACK M02-002 DEFAULT enabled=%d\n",
         fixture.heap->mm_procfs->backtrace);
  default_enabled = fixture.heap->mm_procfs->backtrace;
  if (stack_memdump_command("off") < 0)
    {
      goto out;
    }

  if (stack_worker_request(&worker[0], STACK_ACTION_ALLOC, 64) < 0)
    {
      goto out;
    }

  ptr[0] = stack_worker_detach(&worker[0]);
  stack_snapshot(fixture.heap, &snapshot);
  record = stack_find(&snapshot, ptr[0]);
  if (record == NULL || record->stack != NULL)
    {
      goto out;
    }

  if (stack_memdump_command("on") < 0 ||
      stack_worker_request(&worker[0], STACK_ACTION_ALLOC, 64) < 0)
    {
      goto out;
    }

  ptr[1] = stack_worker_detach(&worker[0]);
  stack_snapshot(fixture.heap, &snapshot);
  record = stack_find(&snapshot, ptr[1]);
  if (record == NULL || record->stack == NULL)
    {
      goto out;
    }

  if (stack_memdump_command("off") < 0 ||
      stack_worker_request(&worker[0], STACK_ACTION_ALLOC, 64) < 0)
    {
      goto out;
    }

  ptr[2] = stack_worker_detach(&worker[0]);
  stack_snapshot(fixture.heap, &snapshot);
  record = stack_find(&snapshot, ptr[2]);
  if (record == NULL || record->stack != NULL)
    {
      goto out;
    }

  ret = 0;

out:
  stack_memdump_command(default_enabled ? "on" : "off");
  if (ptr[0] != NULL)
    {
      mm_free(fixture.heap, ptr[0]);
    }

  if (ptr[1] != NULL)
    {
      mm_free(fixture.heap, ptr[1]);
    }

  if (ptr[2] != NULL)
    {
      mm_free(fixture.heap, ptr[2]);
    }

  if (stack_workers_stop(worker) < 0)
    {
      ret = -1;
    }

  stack_snapshot(fixture.heap, &snapshot);
  if (snapshot.count != 0)
    {
      ret = -1;
    }

  stack_fixture_uninit(&fixture);
  return ret == 0 ? 0 : stack_fail("M02-002", "global gate");
}

static int stack_case_tid(void)
{
  struct stack_fixture_s fixture;
  struct stack_worker_s worker[STACK_TEST_WORKERS];
  struct stack_snapshot_s snapshot;
  struct stack_record_s *left;
  struct stack_record_s *right;
  struct stack_record_s *controller;
  bool default_enabled;
  void *controller_ptr = NULL;
  pid_t controller_tid;
  int ret = -1;

  if (stack_fixture_init(&fixture, "record-tid", false) < 0)
    {
      return stack_fail("M02-003", "fixture");
    }

  if (stack_workers_start(worker, fixture.heap) < 0)
    {
      stack_fixture_uninit(&fixture);
      return stack_fail("M02-003", "workers");
    }

  controller_tid = gettid();
  default_enabled = fixture.heap->mm_procfs->backtrace;
  if (stack_memdump_command("off") < 0 ||
      stack_memdump_tid(controller_tid, true) < 0)
    {
      goto out;
    }

  controller_ptr = stack_alloc0(fixture.heap, 56);
  stack_snapshot(fixture.heap, &snapshot);
  controller = stack_find(&snapshot, controller_ptr);
  if (controller == NULL || controller->pid != controller_tid ||
      controller->stack == NULL)
    {
      goto out;
    }

  mm_free(fixture.heap, controller_ptr);
  controller_ptr = NULL;
  if (stack_memdump_tid(controller_tid, false) < 0 ||
      stack_memdump_tid(worker[0].tid, true) < 0 ||
      stack_memdump_tid(worker[1].tid, false) < 0 ||
      stack_worker_request(&worker[0], STACK_ACTION_ALLOC, 72) < 0 ||
      stack_worker_request(&worker[1], STACK_ACTION_ALLOC, 80) < 0)
    {
      goto out;
    }

  stack_snapshot(fixture.heap, &snapshot);
  left = stack_find(&snapshot, worker[0].ptr);
  right = stack_find(&snapshot, worker[1].ptr);
  if (left == NULL || right == NULL || left->pid != worker[0].tid ||
      right->pid != worker[1].tid || left->stack == NULL ||
      right->stack != NULL)
    {
      goto out;
    }

  if (stack_worker_request(&worker[0], STACK_ACTION_FREE, 0) < 0 ||
      stack_worker_request(&worker[1], STACK_ACTION_FREE, 0) < 0 ||
      stack_memdump_tid(worker[0].tid, false) < 0 ||
      stack_memdump_command("on") < 0)
    {
      goto out;
    }

  if (stack_worker_request(&worker[0], STACK_ACTION_ALLOC, 88) < 0)
    {
      goto out;
    }

  stack_snapshot(fixture.heap, &snapshot);
  left = stack_find(&snapshot, worker[0].ptr);
  ret = left != NULL && left->stack != NULL ? 0 : -1;

out:
  if (controller_ptr != NULL)
    {
      mm_free(fixture.heap, controller_ptr);
    }

  stack_memdump_tid(controller_tid, false);
  stack_memdump_tid(worker[0].tid, false);
  stack_memdump_tid(worker[1].tid, false);
  stack_memdump_command(default_enabled ? "on" : "off");
  if (stack_workers_stop(worker) < 0)
    {
      ret = -1;
    }

  stack_snapshot(fixture.heap, &snapshot);
  if (snapshot.count != 0)
    {
      ret = -1;
    }

  stack_fixture_uninit(&fixture);
  return ret == 0 ? 0 : stack_fail("M02-003", "TID gate or OR");
}

static int stack_case_filter(void)
{
  struct stack_fixture_s fixture;
  struct stack_worker_s worker[STACK_TEST_WORKERS];
  struct stack_snapshot_s before;
  struct stack_snapshot_s after;
  struct stack_record_s *record;
  struct mallinfo_task match;
  struct mallinfo_task miss;
  struct malltask task;
  bool default_enabled;
  void *before_ptr = NULL;
  void *target_ptr = NULL;
  void *after_ptr = NULL;
  unsigned long low;
  unsigned long high;
  int ret = -1;

  if (stack_fixture_init(&fixture, "record-filter", false) < 0)
    {
      return stack_fail("M02-004", "fixture");
    }

  if (stack_workers_start(worker, fixture.heap) < 0)
    {
      stack_fixture_uninit(&fixture);
      return stack_fail("M02-004", "workers");
    }

  default_enabled = fixture.heap->mm_procfs->backtrace;
  if (stack_memdump_command("off") < 0 ||
      stack_memdump_tid(worker[1].tid, true) < 0)
    {
      goto out;
    }

  stack_snapshot(fixture.heap, &before);
  if (stack_worker_request(&worker[1], STACK_ACTION_ALLOC, 88) < 0)
    {
      goto out;
    }

  before_ptr = stack_worker_detach(&worker[1]);
  low = g_mm_seqno;
  if (stack_worker_request(&worker[1], STACK_ACTION_ALLOC, 96) < 0)
    {
      goto out;
    }

  target_ptr = stack_worker_detach(&worker[1]);
  if (stack_worker_request(&worker[1], STACK_ACTION_ALLOC, 104) < 0)
    {
      goto out;
    }

  after_ptr = stack_worker_detach(&worker[1]);
  stack_snapshot(fixture.heap, &after);
  record = stack_find(&after, target_ptr);
  high = record == NULL ? 0 : record->seqno;
  memset(&task, 0, sizeof(task));
  task.pid = worker[1].tid;
  task.seqmin = low;
  task.seqmax = high;
  match = mm_mallinfo_task(fixture.heap, &task);
  task.pid = worker[0].tid;
  miss = mm_mallinfo_task(fixture.heap, &task);
  printf("MM_RECORD_STACK M02-004 WINDOW tid=%d seq=%lu..%lu "
         "before=%d after=%d\n",
         worker[1].tid, low, high, before.count, after.count);
  if (record == NULL || record->user != target_ptr ||
      record->pid != worker[1].tid ||
      record->stack == NULL || high < low || match.aordblks != 1 ||
      miss.aordblks != 0)
    {
      goto out;
    }

  ret = 0;

out:
  if (before_ptr != NULL)
    {
      mm_free(fixture.heap, before_ptr);
    }

  if (target_ptr != NULL)
    {
      mm_free(fixture.heap, target_ptr);
    }

  if (after_ptr != NULL)
    {
      mm_free(fixture.heap, after_ptr);
    }

  stack_memdump_tid(worker[1].tid, false);
  stack_memdump_tid(worker[0].tid, false);
  stack_memdump_command(default_enabled ? "on" : "off");
  if (stack_workers_stop(worker) < 0)
    {
      ret = -1;
    }

  stack_snapshot(fixture.heap, &after);
  if (after.count != 0)
    {
      ret = -1;
    }

  stack_fixture_uninit(&fixture);
  return ret == 0 ? 0 : stack_fail("M02-004", "PID/sequence/stack");
}

static int stack_case_duplicate(void)
{
  struct stack_fixture_s fixture;
  struct stack_snapshot_s snapshot;
  struct stack_record_s record[3];
  void *ptr[3];
  int i;
  int ret = -1;

  memset(ptr, 0, sizeof(ptr));

  if (stack_fixture_init(&fixture, "record-duplicate", false) < 0)
    {
      return stack_fail("M02-006", "fixture");
    }

  fixture.heap->mm_procfs->backtrace = true;
  stack_pool_dump("M02-006", "BASELINE", 0);
  for (i = 0; i < 3; i++)
    {
      ptr[i] = stack_alloc_same(fixture.heap);
      stack_snapshot(fixture.heap, &snapshot);
      if (ptr[i] == NULL || stack_find(&snapshot, ptr[i]) == NULL)
        {
          goto out;
        }

      record[i] = *stack_find(&snapshot, ptr[i]);
      if (record[i].stack == NULL)
        {
          goto out;
        }
    }

  if (!stack_same(&record[0], &record[1]) ||
      !stack_same(&record[1], &record[2]))
    {
      goto out;
    }

  stack_print_target("M02-006", -1, &record[0]);
  for (i = 0; i < 3; i++)
    {
      stack_pool_dump("M02-006", "ACTIVE", 3 - i);
      mm_free(fixture.heap, ptr[i]);
      ptr[i] = NULL;
    }

  stack_pool_dump("M02-006", "AFTER", 0);
  stack_snapshot(fixture.heap, &snapshot);
  ret = snapshot.count == 0 ? 0 : -1;
  printf("MM_RECORD_STACK M02-006 REF before=3 after=%d\n", snapshot.count);

out:
  for (i = 0; i < 3; i++)
    {
      if (ptr[i] != NULL)
        {
          mm_free(fixture.heap, ptr[i]);
        }
    }

  mm_free_delaylist(fixture.heap);
  stack_snapshot(fixture.heap, &snapshot);
  if (snapshot.count != 0)
    {
      ret = -1;
    }

  stack_fixture_uninit(&fixture);
  if (ret == 0)
    {
      printf("MM_RECORD_STACK M02-006 PARTIAL EXTERNAL "
             "host_parse_required\n");
      return -EAGAIN;
    }

  return stack_fail("M02-006", "duplicate reference");
}

static int stack_case_concurrent(void)
{
  struct stack_fixture_s fixture;
  struct stack_worker_s worker[STACK_TEST_WORKERS];
  struct stack_snapshot_s snapshot;
  struct stack_record_s *left;
  struct stack_record_s *right;
  int ret = -1;
  int i;

  if (stack_fixture_init(&fixture, "record-concurrent", false) < 0)
    {
      return stack_fail("M02-007", "fixture");
    }

  if (stack_workers_start(worker, fixture.heap) < 0)
    {
      stack_fixture_uninit(&fixture);
      return stack_fail("M02-007", "workers");
    }

  fixture.heap->mm_procfs->backtrace = true;
  stack_pool_dump("M02-007", "BEFORE", -1);
  pthread_mutex_lock(&g_stack_lock);
  for (i = 0; i < STACK_TEST_WORKERS; i++)
    {
      worker[i].size = 104 + i * 8;
      worker[i].done = false;
      worker[i].action = STACK_ACTION_ALLOC;
    }

  pthread_cond_broadcast(&g_stack_cond);
  while (!worker[0].done || !worker[1].done)
    {
      pthread_cond_wait(&g_stack_cond, &g_stack_lock);
    }

  pthread_mutex_unlock(&g_stack_lock);

  stack_snapshot(fixture.heap, &snapshot);
  left = stack_find(&snapshot, worker[0].ptr);
  right = stack_find(&snapshot, worker[1].ptr);
  ret = left != NULL && right != NULL && left->pid == worker[0].tid &&
        right->pid == worker[1].tid && left->stack != NULL &&
        right->stack != NULL && left->stack != right->stack ? 0 : -1;

  if (ret == 0 &&
      stack_workers_request_both(worker, STACK_ACTION_REALLOC, 144, 152) < 0)
    {
      ret = -1;
    }

  stack_snapshot(fixture.heap, &snapshot);
  left = stack_find(&snapshot, worker[0].ptr);
  right = stack_find(&snapshot, worker[1].ptr);
  if (left == NULL || right == NULL || left->pid != worker[0].tid ||
      right->pid != worker[1].tid || left->stack == NULL ||
      right->stack == NULL)
    {
      ret = -1;
    }

  if (ret == 0)
    {
      stack_print_target("M02-007", 0, left);
      stack_print_target("M02-007", 1, right);
      stack_pool_dump("M02-007", "ACTIVE", -1);
    }

  if (stack_workers_request_both(worker, STACK_ACTION_FREE, 0, 0) < 0)
    {
      ret = -1;
    }

  mm_free_delaylist(fixture.heap);

  stack_snapshot(fixture.heap, &snapshot);
  stack_pool_dump("M02-007", "AFTER", -1);
  printf("MM_RECORD_STACK M02-007 ASSERT realloc=2 free=2 residual=%d\n",
         snapshot.count);
  if (snapshot.count != 0)
    {
      ret = -1;
    }

  if (stack_workers_stop(worker) < 0)
    {
      ret = -1;
    }

  stack_fixture_uninit(&fixture);
  return ret == 0 ? 0 : stack_fail("M02-007", "concurrent ownership");
}

static int stack_case_bad_input(void)
{
  struct stack_fixture_s fixture;
  struct stack_snapshot_s snapshot;
  struct stack_record_s *record;
  void *ptr = NULL;
  int bad_sequence;
  int invalid_tid;
  int loose_parser;
  int ret = -1;

  stack_memdump_command("off");
  invalid_tid = stack_memdump_command("999999on");
  loose_parser = stack_memdump_command("not-a-command");
  bad_sequence = stack_memdump_command("999999,x,y");
  printf("MM_RECORD_STACK M02-008 invalid_tid=%d loose_parser=%d "
         "bad_sequence=%d\n", invalid_tid, loose_parser, bad_sequence);
  if (invalid_tid != -EINVAL || loose_parser != 0 || bad_sequence != 0 ||
      stack_fixture_init(&fixture, "record-bad-input", false) < 0)
    {
      return stack_fail("M02-008", "parser contract");
    }

  fixture.heap->mm_procfs->backtrace = true;
  ptr = stack_alloc0(fixture.heap, 64);
  stack_snapshot(fixture.heap, &snapshot);
  record = stack_find(&snapshot, ptr);
  if (ptr != NULL && record != NULL && record->stack != NULL)
    {
      mm_free(fixture.heap, ptr);
      ptr = NULL;
      mm_free_delaylist(fixture.heap);
      stack_snapshot(fixture.heap, &snapshot);
      ret = snapshot.count == 0 ? 0 : -1;
    }

  if (ptr != NULL)
    {
      mm_free(fixture.heap, ptr);
      mm_free_delaylist(fixture.heap);
    }

  stack_memdump_command("off");
  stack_fixture_uninit(&fixture);
  return ret == 0 ? 0 : stack_fail("M02-008", "post-error alive");
}

#if CONFIG_LIBC_BACKTRACE_INIT_SIZE == 4
static void stack_expand_dump(const char *phase, int step,
                              int expected_capacity, int expected_used,
                              int expected_records)
{
  printf("MM_RECORD_STACK M02-009 MARK %s step=%d expected_capacity=%d "
         "expected_used=%d expected_records=%d\n", phase, step,
         expected_capacity, expected_used, expected_records);
  sched_lock();
  backtrace_dump();
  sched_unlock();
}
#endif

static int stack_case_expand(void)
{
#if CONFIG_LIBC_BACKTRACE_INIT_SIZE == 4
  struct stack_fixture_s fixture;
  struct stack_snapshot_s snapshot;
  struct stack_record_s *record;
  struct stack_record_s records[4];
  void *ptr[4];
  int ret = -1;
  int i;
  int j;

  memset(ptr, 0, sizeof(ptr));

  if (g_stack_test_invocations != 1)
    {
      return stack_fail("M02-009",
                        "requires first stack_test invocation after boot");
    }

  if (stack_fixture_init(&fixture, "record-expand", false) < 0)
    {
      return stack_fail("M02-009", "fixture");
    }

  fixture.heap->mm_procfs->backtrace = true;
  stack_snapshot(fixture.heap, &snapshot);
  if (snapshot.count != 0)
    {
      goto out;
    }

  stack_expand_dump("BEFORE", 0, 0, 0, 0);
  for (i = 0; i < 4; i++)
    {
      switch (i)
        {
          case 0:
            ptr[i] = stack_unique_alloc0(fixture.heap);
            break;
          case 1:
            ptr[i] = stack_unique_alloc1(fixture.heap);
            break;
          case 2:
            ptr[i] = stack_unique_alloc2(fixture.heap);
            break;
          default:
            ptr[i] = stack_unique_alloc3(fixture.heap);
            break;
        }

      stack_snapshot(fixture.heap, &snapshot);
      if (ptr[i] == NULL || snapshot.count != i + 1)
        {
          goto out;
        }

      for (j = 0; j <= i; j++)
        {
          record = stack_find(&snapshot, ptr[j]);
          if (record == NULL || record->stack == NULL)
            {
              goto out;
            }

          if (j < i && !stack_same(record, &records[j]))
            {
              goto out;
            }

          if (j == i)
            {
              int k;

              for (k = 0; k < i; k++)
                {
                  if (record->stack == records[k].stack ||
                      stack_same(record, &records[k]))
                    {
                      goto out;
                    }
                }

              records[i] = *record;
            }
        }

      stack_expand_dump("AFTER", i + 1, i == 3 ? 8 : 4, i + 1, i + 1);
    }

  record = stack_find(&snapshot, ptr[0]);
  if (record == NULL || !stack_same(record, &records[0]))
    {
      goto out;
    }

  printf("MM_RECORD_STACK M02-009 ASSERT old_entry=present step=4\n");

  for (i = 0; i < 4; i++)
    {
      mm_free(fixture.heap, ptr[i]);
      mm_free_delaylist(fixture.heap);
      stack_snapshot(fixture.heap, &snapshot);
      if (stack_find(&snapshot, ptr[i]) != NULL ||
          snapshot.count != 3 - i)
        {
          goto out;
        }

      ptr[i] = NULL;
      stack_expand_dump("FREE", i + 1, 8, 3 - i, 3 - i);
    }

  ret = 0;

out:
  for (i = 0; i < 4; i++)
    {
      if (ptr[i] != NULL)
        {
          mm_free(fixture.heap, ptr[i]);
        }
    }

  mm_free_delaylist(fixture.heap);
  stack_snapshot(fixture.heap, &snapshot);
  if (snapshot.count != 0)
    {
      ret = -1;
    }

  stack_fixture_uninit(&fixture);
  return ret == 0 ? 0 : stack_fail("M02-009", "unique expansion");
#else
  return stack_fail("M02-009", "requires init_size=4 and fresh boot");
#endif
}

static int stack_case_sweep(void)
{
  struct stack_fixture_s fixture;
  struct stack_snapshot_s snapshot;
  struct stack_record_s *record;
  size_t before;
  size_t between;
  size_t after;
  size_t actual;
  size_t min_actual = SIZE_MAX;
  size_t max_actual = 0;
  size_t size;
  size_t count = 0;
  uint64_t elapsed_ns[2];
  uint64_t batch_ns[2];
  struct timespec start;
  struct timespec end;
  void *ptr = NULL;
  int mode;
  int ret = -1;

  if (stack_fixture_init(&fixture, "record-sweep", false) < 0)
    {
      return stack_fail("M02-010", "fixture");
    }

  before = mm_mallinfo(fixture.heap).uordblks;
  for (mode = 0; mode < 2; mode++)
    {
      fixture.heap->mm_procfs->backtrace = mode != 0;
      if (clock_gettime(CLOCK_MONOTONIC, &start) < 0)
        {
          goto out;
        }

      for (size = 1; size <= STACK_TEST_SWEEP_MAX; size++)
        {
          ptr = mm_malloc(fixture.heap, size);
          stack_snapshot(fixture.heap, &snapshot);
          record = stack_find(&snapshot, ptr);
          if (ptr == NULL || record == NULL ||
              (mode == 0 && record->stack != NULL) ||
              (mode != 0 && record->stack == NULL))
            {
              goto out;
            }

          actual = mm_malloc_size(fixture.heap, ptr);
          if (actual < min_actual)
            {
              min_actual = actual;
            }

          if (actual > max_actual)
            {
              max_actual = actual;
            }

          count++;
          mm_free(fixture.heap, ptr);
          ptr = NULL;
        }

      if (clock_gettime(CLOCK_MONOTONIC, &end) < 0)
        {
          goto out;
        }

      elapsed_ns[mode] = stack_elapsed_ns(&start, &end);

      mm_free_delaylist(fixture.heap);
      between = mm_mallinfo(fixture.heap).uordblks;
      if (between != before)
        {
          goto out;
        }

      if (clock_gettime(CLOCK_MONOTONIC, &start) < 0)
        {
          goto out;
        }

      for (size = 0; size < STACK_TEST_PROFILE_OPS; size++)
        {
          ptr = mm_malloc(fixture.heap, 64);
          if (ptr == NULL)
            {
              goto out;
            }

          mm_free(fixture.heap, ptr);
          ptr = NULL;
          mm_free_delaylist(fixture.heap);
        }

      if (clock_gettime(CLOCK_MONOTONIC, &end) < 0)
        {
          goto out;
        }

      batch_ns[mode] = stack_elapsed_ns(&start, &end);
      if (mm_mallinfo(fixture.heap).uordblks != before)
        {
          goto out;
        }
    }

  after = mm_mallinfo(fixture.heap).uordblks;
  printf("MM_RECORD_STACK M02-010 SWEEP count=%zu used=%zu/%zu/%zu "
         "actual=%zu..%zu off_ns=%llu on_ns=%llu\n", count, before,
         between, after, min_actual, max_actual,
         (unsigned long long)elapsed_ns[0],
         (unsigned long long)elapsed_ns[1]);
  printf("MM_RECORD_STACK M02-010 OPS_PROFILE operations_per_mode=%d "
         "off_ns=%llu on_ns=%llu controller_tid=%d "
         "stack_proc=/proc/%d/status image_and_pool=host_required\n",
         STACK_TEST_PROFILE_OPS, (unsigned long long)batch_ns[0],
         (unsigned long long)batch_ns[1], gettid(), gettid());
  ret = after == before ? 0 : -1;

out:
  if (ptr != NULL)
    {
      mm_free(fixture.heap, ptr);
      mm_free_delaylist(fixture.heap);
    }

  stack_fixture_uninit(&fixture);
  if (ret == 0)
    {
      printf("MM_RECORD_STACK M02-010 PARTIAL EXTERNAL profile_required\n");
      return -EAGAIN;
    }

  return stack_fail("M02-010", "off/on size sweep");
}

static int stack_case_kasan(void)
{
#ifdef CONFIG_MM_KASAN
  struct stack_fixture_s fixture;
  struct stack_snapshot_s snapshot;
  struct stack_record_s *record;
  size_t before;
  size_t after;
  void *ptr;

  if (stack_fixture_init(&fixture, "record-kasan", true) < 0)
    {
      return stack_fail("M02-011", "kasan fixture");
    }

  fixture.heap->mm_procfs->backtrace = true;
  before = mm_mallinfo(fixture.heap).uordblks;
  ptr = mm_malloc(fixture.heap, 64);
  if (ptr == NULL)
    {
      stack_fixture_uninit(&fixture);
      return stack_fail("M02-011", "kasan allocation");
    }

  stack_snapshot(fixture.heap, &snapshot);
  record = stack_find(&snapshot, ptr);
  if (record == NULL || record->stack == NULL || record->depth <= 0)
    {
      mm_free(fixture.heap, ptr);
      mm_free_delaylist(fixture.heap);
      stack_fixture_uninit(&fixture);
      return stack_fail("M02-011", "active stack record");
    }

  memset(ptr, 0x5a, 64);
  mm_free(fixture.heap, ptr);
  mm_free_delaylist(fixture.heap);
  after = mm_mallinfo(fixture.heap).uordblks;
  if (after != before)
    {
      stack_fixture_uninit(&fixture);
      return stack_fail("M02-011", "heap usage retained after free");
    }

  stack_fixture_uninit(&fixture);
  printf("MM_RECORD_STACK M02-011 ASSERT kasan register/free/unregister "
         "used=%zu/%zu\n", before, after);
  return 0;
#else
  printf("MM_RECORD_STACK M02-011 SKIP CONFIG_MM_KASAN=0\n");
  return -ENOTSUP;
#endif
}

static int stack_case_diagnostics(void)
{
  printf("MM_RECORD_STACK M02-012 DIAG ");
#ifdef CONFIG_SCHED_INSTRUMENTATION_HEAP
  printf("heap_note=1 ");
#else
  printf("heap_note=0 ");
#endif
#ifdef CONFIG_DRIVERS_NOTE
  printf("note_driver=1 ");
#else
  printf("note_driver=0 ");
#endif
#ifdef CONFIG_BOARD_COREDUMP_SYSLOG
  printf("coredump_syslog=1\n");
#else
  printf("coredump_syslog=0\n");
#endif
  printf("MM_RECORD_STACK M02-012 PARTIAL EXTERNAL "
         "pool capture not exercised\n");
  return -EAGAIN;
}

static int stack_case_peripheral(void)
{
  printf("MM_RECORD_STACK M02-013 COMMANDS\n");
  printf("  mcu_gpio_test -c edge --out /dev/gpio12 -n 3 -v\n");
  printf("  mcu_timer_test -c 001 -t 10000 -n 5 -e 5 -v\n");
  printf("  mcu_timer_test -c 002 -t 500000 -a 39 -b 79 -v\n");
  printf("  mcu_timer_test -c 005\n");
  printf("  mcu_wdt_test -c 002 -t 1000 -p 3000 -i 500 -v\n");
  printf("  mcu_wdt_test -c 003 -t 1000\n");
  printf("  mcu_rtc_test -c all\n");
  printf("MM_RECORD_STACK M02-013 PARTIAL EXTERNAL evidence required\n");
  return -EAGAIN;
}

static bool stack_case_valid(const char *case_id)
{
  static const char *const cases[] =
  {
    "M02-001", "M02-002", "M02-003", "M02-004", "M02-005",
    "M02-006", "M02-007", "M02-008", "M02-009", "M02-010",
    "M02-011", "M02-012", "M02-013"
  };

  size_t i;

  for (i = 0; i < nitems(cases); i++)
    {
      if (strcmp(case_id, cases[i]) == 0)
        {
          return true;
        }
    }

  return false;
}

static int stack_case_lifecycle(void)
{
  return mm_realloc_stack_test(NULL) == 0 ? 0 :
         stack_fail("M02-005", "R01-R06 lifecycle");
}

static int stack_case_default(void)
{
  struct stack_fixture_s fixture;
  bool actual;
  bool expected;

#ifdef CONFIG_MM_RECORD_STACK_DEFAULT
  expected = true;
#else
  expected = false;
#endif
  if (stack_fixture_init(&fixture, "record-default", false) < 0)
    {
      return stack_fail("M02-001", "fixture");
    }

  actual = fixture.heap->mm_procfs->backtrace;
  stack_fixture_uninit(&fixture);
#ifdef CONFIG_MM_RECORD_STACK_DEFAULT
  printf("MM_RECORD_STACK M02-001 default compile=1 runtime=%d\n", actual);
#else
  printf("MM_RECORD_STACK M02-001 default compile=0 runtime=%d\n", actual);
#endif
  return actual == expected ? 0 : stack_fail("M02-001", "default state");
}

static void stack_result_update(const char *case_id, int result,
                                int *failed, int *partial, int *skipped)
{
  if (result == 0)
    {
      printf("MM_RECORD_STACK %s PASS\n", case_id);
    }
  else if (result == -EAGAIN)
    {
      (*partial)++;
    }
  else if (result == -ENOTSUP)
    {
      (*skipped)++;
    }
  else
    {
      (*failed)++;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int mm_record_stack_test(const char *case_id)
{
  bool run_all;
  int failed = 0;
  int partial = 0;
  int skipped = 0;

  run_all = case_id == NULL || strcmp(case_id, "all") == 0;
  if (!run_all && !stack_case_valid(case_id))
    {
      return stack_fail("ARG", "unknown case");
    }

  g_stack_test_invocations++;

  if (run_all || strcmp(case_id, "M02-001") == 0)
    {
      stack_result_update("M02-001", stack_case_default(), &failed,
                          &partial, &skipped);
    }

  if (run_all || strcmp(case_id, "M02-002") == 0)
    {
      stack_result_update("M02-002", stack_case_gate(), &failed,
                          &partial, &skipped);
    }

  if (run_all || strcmp(case_id, "M02-003") == 0)
    {
      stack_result_update("M02-003", stack_case_tid(), &failed,
                          &partial, &skipped);
    }

  if (run_all || strcmp(case_id, "M02-004") == 0)
    {
      stack_result_update("M02-004", stack_case_filter(), &failed,
                          &partial, &skipped);
    }

  if (run_all || strcmp(case_id, "M02-005") == 0)
    {
      stack_result_update("M02-005", stack_case_lifecycle(), &failed,
                          &partial, &skipped);
    }

  if (run_all || strcmp(case_id, "M02-006") == 0)
    {
      stack_result_update("M02-006", stack_case_duplicate(), &failed,
                          &partial, &skipped);
    }

  if (run_all || strcmp(case_id, "M02-007") == 0)
    {
      stack_result_update("M02-007", stack_case_concurrent(), &failed,
                          &partial, &skipped);
    }

  if (run_all || strcmp(case_id, "M02-008") == 0)
    {
      stack_result_update("M02-008", stack_case_bad_input(), &failed,
                          &partial, &skipped);
    }

  if (!run_all && strcmp(case_id, "M02-009") == 0)
    {
      if (stack_case_expand() < 0)
        {
          return -1;
        }

      printf("MM_RECORD_STACK M02-009 PARTIAL EXTERNAL "
             "host_parse_required\n");
      return -EAGAIN;
    }

  if (run_all)
    {
      printf("MM_RECORD_STACK M02-009 PARTIAL EXTERNAL "
             "fresh_boot_required\n");
      partial++;
    }

  if (run_all || strcmp(case_id, "M02-010") == 0)
    {
      stack_result_update("M02-010", stack_case_sweep(), &failed,
                          &partial, &skipped);
    }

  if (run_all || strcmp(case_id, "M02-011") == 0)
    {
      stack_result_update("M02-011", stack_case_kasan(), &failed,
                          &partial, &skipped);
    }

  if (run_all || strcmp(case_id, "M02-012") == 0)
    {
      stack_result_update("M02-012", stack_case_diagnostics(), &failed,
                          &partial, &skipped);
    }

  if (!run_all && strcmp(case_id, "M02-013") == 0)
    {
      stack_result_update("M02-013", stack_case_peripheral(), &failed,
                          &partial, &skipped);
    }
  else if (run_all)
    {
      printf("MM_RECORD_STACK M02-013 PARTIAL EXTERNAL evidence_required\n");
      partial++;
    }

  if (run_all)
    {
      printf("MM_RECORD_STACK SUMMARY pass=%d partial=%d skip=%d fail=%d\n",
             13 - failed - partial - skipped, partial, skipped, failed);
    }

  if (failed > 0)
    {
      return -1;
    }

  if (partial > 0)
    {
      return -EAGAIN;
    }

  return skipped > 0 ? -ENOTSUP : 0;
}
