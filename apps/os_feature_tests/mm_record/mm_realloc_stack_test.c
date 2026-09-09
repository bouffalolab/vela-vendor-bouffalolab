/****************************************************************************
 * apps/vendor/bouffalolab/apps/os_feature_tests/mm_record/mm_realloc_stack_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <execinfo.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <nuttx/mm/mm.h>
#include <nuttx/tls.h>

/* NuttX private default-allocator layout, test-only. */

#include <mm.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TEST_ARENA_SIZE 16384
#define TEST_PATTERN 0xa5
#define TEST_PATTERN_SIZE 128

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct node_snapshot_s
{
  pid_t pid;
  unsigned long seqno;
  void *stack;
  int depth;
  void *frames[CONFIG_LIBC_BACKTRACE_DEPTH];
};

struct heap_fixture_s
{
  struct mm_heap_s *heap;
  struct tls_info_s *tls;
  uint32_t tls_flags;
};

struct worker_alloc_s
{
  struct mm_heap_s *heap;
  void *ptr;
  size_t size;
  pid_t tid;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint8_t g_test_arena[TEST_ARENA_SIZE]
  __attribute__((aligned(MM_ALIGN)));

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int test_fail(const char *case_id, const char *reason)
{
  printf("MM_REALLOC_STACK %s FAIL %s\n", case_id, reason);
  return -1;
}

static int snapshot_node(void *ptr, struct node_snapshot_s *snapshot)
{
  struct mm_allocnode_s *node;
  void **frames;
  int depth;

  node = (struct mm_allocnode_s *)
    ((uint8_t *)ptr - MM_SIZEOF_ALLOCNODE);
  if (!MM_NODE_IS_ALLOC(node))
    {
      return -1;
    }

  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->pid = node->pid;
  snapshot->seqno = node->seqno;
  snapshot->stack = node->stack;

  frames = backtrace_get(node->stack, &depth);
  if (frames != NULL && depth > 0)
    {
      if (depth > CONFIG_LIBC_BACKTRACE_DEPTH)
        {
          return -1;
        }

      memcpy(snapshot->frames, frames, depth * sizeof(void *));
      snapshot->depth = depth;
    }

  return 0;
}

static bool same_stack(const struct node_snapshot_s *a,
                       const struct node_snapshot_s *b)
{
  return a->stack == b->stack && a->depth == b->depth && a->depth > 0 &&
         memcmp(a->frames, b->frames, a->depth * sizeof(void *)) == 0;
}

static int fixture_init(struct heap_fixture_s *fixture, const char *name)
{
  struct mm_heap_config_s config;

  memset(g_test_arena, 0, sizeof(g_test_arena));
  memset(fixture, 0, sizeof(*fixture));

  fixture->tls = tls_get_info();
  if (fixture->tls != NULL)
    {
      fixture->tls_flags = fixture->tls->tl_flags;
      fixture->tls->tl_flags &= ~TLS_FLAG_HEAP_DUMP;
    }

  memset(&config, 0, sizeof(config));
  config.name = name;
  config.start = g_test_arena;
  config.size = sizeof(g_test_arena);
  config.nokasan = true;
  config.allocheap = false;
  mm_initialize_heap(&config, &fixture->heap);

  if (fixture->heap == NULL || fixture->heap->mm_procfs == NULL)
    {
      return -1;
    }

  fixture->heap->mm_procfs->backtrace = true;
  return 0;
}

static void fixture_uninit(struct heap_fixture_s *fixture)
{
  if (fixture->heap != NULL)
    {
      if (fixture->heap->mm_procfs != NULL)
        {
          fixture->heap->mm_procfs->backtrace = false;
        }

      mm_uninitialize(fixture->heap);
      fixture->heap = NULL;
    }

  if (fixture->tls != NULL)
    {
      fixture->tls->tl_flags = fixture->tls_flags;
    }
}

static noinline_function void *worker_alloc(void *arg)
{
  struct worker_alloc_s *request = arg;

  request->tid = gettid();
  request->ptr = mm_malloc(request->heap, request->size);
  if (request->ptr != NULL)
    {
      memset(request->ptr, TEST_PATTERN,
             request->size < TEST_PATTERN_SIZE ? request->size :
             TEST_PATTERN_SIZE);
    }

  return NULL;
}

static int alloc_from_worker(struct mm_heap_s *heap, size_t size,
                             void **ptr, pid_t *tid)
{
  struct worker_alloc_s request;
  pthread_t thread;
  int ret;

  memset(&request, 0, sizeof(request));
  request.heap = heap;
  request.size = size;

  ret = pthread_create(&thread, NULL, worker_alloc, &request);
  if (ret != 0)
    {
      return -ret;
    }

  ret = pthread_join(thread, NULL);
  if (ret != 0)
    {
      return -ret;
    }

  *ptr = request.ptr;
  *tid = request.tid;
  return request.ptr == NULL ? -1 : 0;
}

static bool pattern_valid(const void *ptr, size_t size)
{
  const uint8_t *bytes = ptr;
  size_t limit = size < TEST_PATTERN_SIZE ? size : TEST_PATTERN_SIZE;
  size_t i;

  for (i = 0; i < limit; i++)
    {
      if (bytes[i] != TEST_PATTERN)
        {
          return false;
        }
    }

  return true;
}

static int test_shrink(void)
{
  struct node_snapshot_s before;
  struct node_snapshot_s after;
  struct heap_fixture_s fixture;
  void *target = NULL;
  void *result;
  pid_t worker_tid;
  int ret = -1;

  if (fixture_init(&fixture, "realloc-r01") < 0 ||
      alloc_from_worker(fixture.heap, 512, &target, &worker_tid) < 0 ||
      snapshot_node(target, &before) < 0 || before.stack == NULL ||
      before.pid != worker_tid)
    {
      goto out;
    }

  result = mm_realloc(fixture.heap, target, 128);
  if (result != target || !pattern_valid(result, 128) ||
      snapshot_node(result, &after) < 0 || after.stack == NULL ||
      after.pid != gettid() || after.seqno == before.seqno)
    {
      goto out;
    }

  ret = 0;

out:
  if (target != NULL)
    {
      mm_free(fixture.heap, target);
    }

  fixture_uninit(&fixture);
  return ret == 0 ? 0 : test_fail("R01", "shrink contract");
}

static int test_inplace_grow(void)
{
  struct node_snapshot_s before;
  struct node_snapshot_s after;
  struct heap_fixture_s fixture;
  void *prefix = NULL;
  void *target = NULL;
  void *result;
  pid_t worker_tid;
  int ret = -1;

  if (fixture_init(&fixture, "realloc-r02") < 0)
    {
      goto out;
    }

  prefix = mm_malloc(fixture.heap, 128);
  if (prefix == NULL ||
      alloc_from_worker(fixture.heap, 256, &target, &worker_tid) < 0 ||
      snapshot_node(target, &before) < 0 || before.stack == NULL ||
      before.pid != worker_tid)
    {
      goto out;
    }

  result = mm_realloc(fixture.heap, target, 512);
  if (result == NULL)
    {
      goto out;
    }

  if (result != target)
    {
      target = result;
      goto out;
    }

  if (!pattern_valid(result, 256) ||
      snapshot_node(result, &after) < 0 || after.stack == NULL ||
      after.pid != gettid() || after.seqno == before.seqno)
    {
      goto out;
    }

  ret = 0;

out:
  if (target != NULL)
    {
      mm_free(fixture.heap, target);
    }

  if (prefix != NULL)
    {
      mm_free(fixture.heap, prefix);
    }

  fixture_uninit(&fixture);
  return ret == 0 ? 0 : test_fail("R02", "in-place grow contract");
}

static int test_moved_grow(void)
{
  struct node_snapshot_s before;
  struct node_snapshot_s after;
  struct heap_fixture_s fixture;
  void *prefix = NULL;
  void *target = NULL;
  void *suffix = NULL;
  void *result = NULL;
  uintptr_t old_address;
  pid_t worker_tid;
  int ret = -1;

  if (fixture_init(&fixture, "realloc-r03") < 0)
    {
      goto out;
    }

  prefix = mm_malloc(fixture.heap, 512);
  if (prefix == NULL ||
      alloc_from_worker(fixture.heap, 256, &target, &worker_tid) < 0)
    {
      goto out;
    }

  suffix = mm_malloc(fixture.heap, 256);
  if (suffix == NULL || snapshot_node(target, &before) < 0 ||
      before.stack == NULL || before.pid != worker_tid)
    {
      goto out;
    }

  mm_free(fixture.heap, prefix);
  prefix = NULL;
  old_address = (uintptr_t)target;
  result = mm_realloc(fixture.heap, target, 512);
  if (result == NULL)
    {
      goto out;
    }

  if ((uintptr_t)result >= old_address ||
      old_address - (uintptr_t)result < 256)
    {
      target = result;
      goto out;
    }

  target = result;
  if (!pattern_valid(result, 256) ||
      snapshot_node(result, &after) < 0 || after.stack == NULL ||
      after.pid != gettid() || after.seqno == before.seqno)
    {
      goto out;
    }

  ret = 0;

out:
  if (target != NULL)
    {
      mm_free(fixture.heap, target);
    }

  if (prefix != NULL)
    {
      mm_free(fixture.heap, prefix);
    }

  if (suffix != NULL)
    {
      mm_free(fixture.heap, suffix);
    }

  fixture_uninit(&fixture);
  return ret == 0 ? 0 : test_fail("R03", "moved grow contract");
}

static int test_fallback_success(void)
{
  struct node_snapshot_s before;
  struct node_snapshot_s after;
  struct heap_fixture_s fixture;
  void *prefix = NULL;
  void *target = NULL;
  void *suffix = NULL;
  void *result = NULL;
  pid_t worker_tid;
  int ret = -1;

  if (fixture_init(&fixture, "realloc-r04") < 0)
    {
      goto out;
    }

  prefix = mm_malloc(fixture.heap, 128);
  if (prefix == NULL ||
      alloc_from_worker(fixture.heap, 256, &target, &worker_tid) < 0)
    {
      goto out;
    }

  suffix = mm_malloc(fixture.heap, 128);
  if (suffix == NULL || snapshot_node(target, &before) < 0 ||
      before.stack == NULL || before.pid != worker_tid)
    {
      goto out;
    }

  result = mm_realloc(fixture.heap, target, 1024);
  if (result == NULL)
    {
      goto out;
    }

  if (result == target)
    {
      goto out;
    }

  target = result;
  if (!pattern_valid(result, 256) ||
      snapshot_node(result, &after) < 0 || after.stack == NULL ||
      after.pid != gettid() || after.seqno == before.seqno)
    {
      goto out;
    }

  ret = 0;

out:
  if (target != NULL)
    {
      mm_free(fixture.heap, target);
    }

  if (prefix != NULL)
    {
      mm_free(fixture.heap, prefix);
    }

  if (suffix != NULL)
    {
      mm_free(fixture.heap, suffix);
    }

  fixture_uninit(&fixture);
  return ret == 0 ? 0 : test_fail("R04", "fallback success contract");
}

static int test_fallback_failure(void)
{
  struct node_snapshot_s before;
  struct node_snapshot_s after;
  struct heap_fixture_s fixture;
  struct mallinfo info;
  void *prefix = NULL;
  void *target = NULL;
  void *suffix = NULL;
  void *filler = NULL;
  void *result;
  size_t largest;
  pid_t worker_tid;
  int ret = -1;

  if (fixture_init(&fixture, "realloc-r05") < 0)
    {
      ret = test_fail("R05", "fixture init");
      goto out_no_report;
    }

  prefix = mm_malloc(fixture.heap, 128);
  if (prefix == NULL)
    {
      ret = test_fail("R05", "prefix allocation");
      goto out;
    }

  if (alloc_from_worker(fixture.heap, 256, &target, &worker_tid) < 0)
    {
      ret = test_fail("R05", "target allocation");
      goto out;
    }

  suffix = mm_malloc(fixture.heap, 128);
  if (suffix == NULL)
    {
      ret = test_fail("R05", "suffix allocation");
      goto out;
    }

  if (snapshot_node(target, &before) < 0 || before.pid != worker_tid ||
      before.stack == NULL)
    {
      ret = test_fail("R05", "target snapshot");
      goto out;
    }

  info = mm_mallinfo(fixture.heap);
  largest = info.mxordblk;
  if (largest <= MM_ALLOCNODE_OVERHEAD)
    {
      ret = test_fail("R05", "largest free chunk");
      goto out;
    }

  printf("MM_REALLOC_STACK R05 INFO largest=%zu overhead=%zu\n",
         largest, (size_t)MM_ALLOCNODE_OVERHEAD);
  filler = mm_malloc(fixture.heap, largest - MM_ALLOCNODE_OVERHEAD);
  if (filler == NULL)
    {
      ret = test_fail("R05", "filler allocation");
      goto out;
    }

  result = mm_realloc(fixture.heap, target, 1024);
  if (result != NULL)
    {
      target = result;
      ret = test_fail("R05", "fallback unexpectedly succeeded");
      goto out_no_report;
    }

  if (!pattern_valid(target, 256))
    {
      ret = test_fail("R05", "old content changed");
      goto out_no_report;
    }

  if (snapshot_node(target, &after) < 0)
    {
      ret = test_fail("R05", "old node is invalid");
      goto out_no_report;
    }

  if (after.pid != before.pid)
    {
      ret = test_fail("R05", "old pid changed");
      goto out_no_report;
    }

  if (after.seqno != before.seqno)
    {
      ret = test_fail("R05", "old sequence changed");
      goto out_no_report;
    }

  if (!same_stack(&before, &after))
    {
      printf("MM_REALLOC_STACK R05 INFO before=%p/%d after=%p/%d\n",
             before.stack, before.depth, after.stack, after.depth);
      ret = test_fail("R05", "old stack changed");
      goto out_no_report;
    }

  ret = 0;

out:
out_no_report:
  if (filler != NULL)
    {
      mm_free(fixture.heap, filler);
    }

  if (target != NULL)
    {
      mm_free(fixture.heap, target);
    }

  if (prefix != NULL)
    {
      mm_free(fixture.heap, prefix);
    }

  if (suffix != NULL)
    {
      mm_free(fixture.heap, suffix);
    }

  fixture_uninit(&fixture);
  return ret;
}

static noinline_function void *duplicate_alloc(struct mm_heap_s *heap,
                                                size_t size)
{
  void *ptr = mm_malloc(heap, size);

  if (ptr != NULL)
    {
      *((volatile uint8_t *)ptr) = TEST_PATTERN;
    }

  return ptr;
}

static noinline_function void *duplicate_shrink(struct mm_heap_s *heap,
                                                 void *ptr)
{
  return mm_realloc(heap, ptr, 16);
}

static noinline_function void *duplicate_grow(struct mm_heap_s *heap,
                                               void *ptr)
{
  return mm_realloc(heap, ptr, 128);
}

static void duplicate_dump(void *stack, int expected_ref)
{
  printf("MM_REALLOC_STACK R06 MARK expected_ref=%d stack=%p\n",
         expected_ref, stack);
  sched_lock();
  backtrace_dump();
  sched_unlock();
}

static int test_duplicate_ref(void)
{
  struct node_snapshot_s snapshot[3];
  struct node_snapshot_s updated;
  struct heap_fixture_s fixture;
  void *ptr[3];
  void *shared_stack;
  void *result;
  uintptr_t old_address;
  int ret = -1;
  int i;

  memset(ptr, 0, sizeof(ptr));

  if (fixture_init(&fixture, "realloc-r06") < 0)
    {
      goto out;
    }

  for (i = 0; i < 3; i++)
    {
      ptr[i] = duplicate_alloc(fixture.heap, 64);
      if (ptr[i] == NULL || snapshot_node(ptr[i], &snapshot[i]) < 0 ||
          snapshot[i].stack == NULL)
        {
          goto out;
        }
    }

  if (snapshot[0].stack != snapshot[1].stack ||
      snapshot[0].stack != snapshot[2].stack)
    {
      goto out;
    }

  shared_stack = snapshot[0].stack;
  duplicate_dump(shared_stack, 3);

  old_address = (uintptr_t)ptr[2];
  result = duplicate_shrink(fixture.heap, ptr[2]);
  if (result == NULL)
    {
      goto out;
    }

  ptr[2] = result;
  if ((uintptr_t)ptr[2] != old_address ||
      snapshot_node(ptr[2], &updated) < 0 ||
      updated.stack == NULL || same_stack(&updated, &snapshot[2]))
    {
      goto out;
    }

  duplicate_dump(shared_stack, 2);

  mm_free(fixture.heap, ptr[1]);
  ptr[1] = NULL;
  mm_free_delaylist(fixture.heap);
  duplicate_dump(shared_stack, 1);

  old_address = (uintptr_t)ptr[0];
  result = duplicate_grow(fixture.heap, ptr[0]);
  if (result == NULL)
    {
      goto out;
    }

  ptr[0] = result;
  if ((uintptr_t)ptr[0] != old_address ||
      snapshot_node(ptr[0], &updated) < 0 || updated.stack == NULL ||
      same_stack(&updated, &snapshot[0]))
    {
      goto out;
    }

  duplicate_dump(shared_stack, 0);
  ret = 0;

out:
  for (i = 0; i < 3; i++)
    {
      if (ptr[i] != NULL)
        {
          mm_free(fixture.heap, ptr[i]);
        }
    }

  mm_free_delaylist(fixture.heap);

  fixture_uninit(&fixture);
  return ret == 0 ? 0 : test_fail("R06", "duplicate stack contract");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int mm_realloc_stack_test(const char *case_id)
{
  int ret = 0;

  if (case_id == NULL || strcmp(case_id, "R01") == 0)
    {
      ret |= test_shrink();
    }

  if (case_id == NULL || strcmp(case_id, "R02") == 0)
    {
      ret |= test_inplace_grow();
    }

  if (case_id == NULL || strcmp(case_id, "R03") == 0)
    {
      ret |= test_moved_grow();
    }

  if (case_id == NULL || strcmp(case_id, "R04") == 0)
    {
      ret |= test_fallback_success();
    }

  if (case_id == NULL || strcmp(case_id, "R05") == 0)
    {
      ret |= test_fallback_failure();
    }

  if (case_id == NULL || strcmp(case_id, "R06") == 0)
    {
      ret |= test_duplicate_ref();
    }

  if (case_id != NULL && strcmp(case_id, "R01") != 0 &&
      strcmp(case_id, "R02") != 0 && strcmp(case_id, "R03") != 0 &&
      strcmp(case_id, "R04") != 0 && strcmp(case_id, "R05") != 0 &&
      strcmp(case_id, "R06") != 0)
    {
      return test_fail("ARG", "unknown case");
    }

  if (ret == 0)
    {
      printf("MM_REALLOC_STACK %s PASS\n",
             case_id == NULL ? "ALL" : case_id);
    }

  return ret;
}
