/****************************************************************************
 * vendor/bouffalolab/apps/os_feature_tests/prio_inherit/prio_inherit_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/* Mutex priority inheritance test.
 *
 * Each case lets a low priority owner hold a mutex that a higher priority
 * thread waits for, and reads the owner's effective priority with
 * pthread_getschedparam().  The controller (main) runs above all test
 * threads, so it observes them without being preempted.
 *
 *   default  pthread_mutex_init(NULL), as applications use it
 *   nxmutex  kernel nxmutex, as drivers use it
 *   none     PTHREAD_PRIO_NONE: the control case, where the inversion
 *            must show up
 *   nested   the owner holds two mutexes with a waiter each and releases
 *            them in both orders
 *   timeout  the waiter gives up with pthread_mutex_timedlock()
 *
 * In the first three a middle priority thread burns the CPU for
 * PI_BUSY_MS.  With inheritance the boosted owner preempts it, so the
 * waiter gets the mutex before the middle thread ends; without, the owner
 * cannot run and the waiter only gets it afterwards.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/mutex.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* All below the Wi-Fi firmware task (130) and LPWORK (150), which may
 * preempt the test but do not change its outcome.
 */

#define PI_PRIO_LOW          101
#define PI_PRIO_MID          105
#define PI_PRIO_HIGH         110
#define PI_PRIO_HIGHER       112
#define PI_PRIO_CTRL         115

#define PI_STACKSIZE         2048
#define PI_SETTLE_MS         20   /* Time for the waiters to block */
#define PI_BUSY_MS           200  /* CPU time burnt by the middle thread */
#define PI_TIMEOUT_MS        50   /* pthread_mutex_timedlock() timeout */

#ifdef CONFIG_PTHREAD_MUTEX_DEFAULT_PRIO_INHERIT
#  define PI_DEFAULT_INHERITS true
#else
#  define PI_DEFAULT_INHERITS false
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum pi_kind_e
{
  PI_PTHREAD_DEFAULT = 0,
  PI_PTHREAD_NONE,
  PI_NXMUTEX,
};

struct pi_lock_s
{
  enum pi_kind_e kind;
  pthread_mutex_t pmutex;
  mutex_t kmutex;
};

/* Low priority owner: takes locks[] in order, reports through locked,
 * waits for release, then gives them back in the same order and records
 * its priority after each unlock.
 */

struct pi_owner_s
{
  struct pi_lock_s *locks[2];
  int nlocks;
  sem_t locked;
  sem_t release;
  int prio_after[2];
};

struct pi_waiter_s
{
  struct pi_lock_s *lock;
  int timeout_ms;              /* 0: wait forever */
  int result;                  /* 0 or an errno value */
  uint64_t acquired_us;
};

struct pi_busy_s
{
  uint64_t end_us;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint64_t pi_now_us(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static int pi_lock_init(struct pi_lock_s *lock, enum pi_kind_e kind)
{
  pthread_mutexattr_t attr;
  int ret;

  lock->kind = kind;
  switch (kind)
    {
      case PI_PTHREAD_DEFAULT:
        return pthread_mutex_init(&lock->pmutex, NULL);

      case PI_PTHREAD_NONE:
        pthread_mutexattr_init(&attr);
        ret = pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_NONE);
        if (ret == 0)
          {
            ret = pthread_mutex_init(&lock->pmutex, &attr);
          }

        pthread_mutexattr_destroy(&attr);
        return ret;

      default:
        return -nxmutex_init(&lock->kmutex);
    }
}

static void pi_lock_destroy(struct pi_lock_s *lock)
{
  if (lock->kind == PI_NXMUTEX)
    {
      nxmutex_destroy(&lock->kmutex);
    }
  else
    {
      pthread_mutex_destroy(&lock->pmutex);
    }
}

/* Return 0 or an errno value, as the pthread functions do */

static int pi_lock_take(struct pi_lock_s *lock, int timeout_ms)
{
  struct timespec abstime;

  if (lock->kind == PI_NXMUTEX)
    {
      return -nxmutex_lock(&lock->kmutex);
    }

  if (timeout_ms == 0)
    {
      return pthread_mutex_lock(&lock->pmutex);
    }

  clock_gettime(CLOCK_REALTIME, &abstime);
  abstime.tv_nsec += timeout_ms * 1000000L;
  abstime.tv_sec += abstime.tv_nsec / 1000000000L;
  abstime.tv_nsec %= 1000000000L;
  return pthread_mutex_timedlock(&lock->pmutex, &abstime);
}

static void pi_lock_give(struct pi_lock_s *lock)
{
  if (lock->kind == PI_NXMUTEX)
    {
      nxmutex_unlock(&lock->kmutex);
    }
  else
    {
      pthread_mutex_unlock(&lock->pmutex);
    }
}

static int pi_get_prio(pthread_t thread)
{
  struct sched_param param;
  int policy;

  if (pthread_getschedparam(thread, &policy, &param) != 0)
    {
      return -1;
    }

  return param.sched_priority;
}

static void pi_sem_wait(sem_t *sem)
{
  while (sem_wait(sem) < 0 && errno == EINTR)
    {
    }
}

static int pi_create(pthread_t *thread, int prio,
                     void *(*entry)(void *), void *arg)
{
  struct sched_param param;
  pthread_attr_t attr;
  int ret;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, PI_STACKSIZE);
  pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
  param.sched_priority = prio;
  pthread_attr_setschedparam(&attr, &param);
  ret = pthread_create(thread, &attr, entry, arg);
  pthread_attr_destroy(&attr);

  if (ret != 0)
    {
      printf("PRIO_INHERIT_TEST pthread_create(prio %d) failed: %d\n",
             prio, ret);
    }

  return ret;
}

static void *pi_owner_main(void *arg)
{
  struct pi_owner_s *owner = arg;
  int i;

  for (i = 0; i < owner->nlocks; i++)
    {
      pi_lock_take(owner->locks[i], 0);
    }

  sem_post(&owner->locked);
  pi_sem_wait(&owner->release);

  for (i = 0; i < owner->nlocks; i++)
    {
      pi_lock_give(owner->locks[i]);
      owner->prio_after[i] = pi_get_prio(pthread_self());
    }

  return NULL;
}

static void *pi_waiter_main(void *arg)
{
  struct pi_waiter_s *waiter = arg;

  waiter->result = pi_lock_take(waiter->lock, waiter->timeout_ms);
  waiter->acquired_us = pi_now_us();
  if (waiter->result == 0)
    {
      pi_lock_give(waiter->lock);
    }

  return NULL;
}

static void *pi_busy_main(void *arg)
{
  struct pi_busy_s *busy = arg;
  uint64_t end = pi_now_us() + PI_BUSY_MS * 1000;

  while (pi_now_us() < end)
    {
    }

  busy->end_us = pi_now_us();
  return NULL;
}

static void pi_owner_init(struct pi_owner_s *owner, struct pi_lock_s *a,
                          struct pi_lock_s *b)
{
  memset(owner, 0, sizeof(*owner));
  owner->locks[0] = a;
  owner->locks[1] = b;
  owner->nlocks = b != NULL ? 2 : 1;
  sem_init(&owner->locked, 0, 0);
  sem_init(&owner->release, 0, 0);
}

static void pi_owner_deinit(struct pi_owner_s *owner)
{
  sem_destroy(&owner->locked);
  sem_destroy(&owner->release);
}

/* Low owner, busy middle thread and high waiter on one mutex */

static bool pi_case_inversion(const char *name, enum pi_kind_e kind,
                              bool inherits)
{
  struct pi_owner_s owner;
  struct pi_waiter_s waiter;
  struct pi_busy_s busy;
  struct pi_lock_s lock;
  pthread_t owner_tid;
  pthread_t waiter_tid;
  pthread_t busy_tid;
  unsigned long handoff_us;
  uint64_t release_us;
  bool mid_first;
  int expect;
  int boosted;
  bool pass;
  int ret;

  ret = pi_lock_init(&lock, kind);
  if (ret != 0)
    {
      printf("PRIO_INHERIT_TEST %s FAIL lock init: %d\n", name, ret);
      return false;
    }

  pi_owner_init(&owner, &lock, NULL);
  memset(&waiter, 0, sizeof(waiter));
  waiter.lock = &lock;
  memset(&busy, 0, sizeof(busy));

  if (pi_create(&owner_tid, PI_PRIO_LOW, pi_owner_main, &owner) != 0)
    {
      goto errout;
    }

  pi_sem_wait(&owner.locked);

  if (pi_create(&busy_tid, PI_PRIO_MID, pi_busy_main, &busy) != 0)
    {
      sem_post(&owner.release);
      pthread_join(owner_tid, NULL);
      goto errout;
    }

  if (pi_create(&waiter_tid, PI_PRIO_HIGH, pi_waiter_main, &waiter) != 0)
    {
      sem_post(&owner.release);
      pthread_join(busy_tid, NULL);
      pthread_join(owner_tid, NULL);
      goto errout;
    }

  /* The waiter blocks on the mutex, the middle thread spins */

  usleep(PI_SETTLE_MS * 1000);
  boosted = pi_get_prio(owner_tid);

  release_us = pi_now_us();
  sem_post(&owner.release);
  pthread_join(waiter_tid, NULL);
  pthread_join(busy_tid, NULL);
  pthread_join(owner_tid, NULL);

  handoff_us = (unsigned long)(waiter.acquired_us - release_us);
  mid_first = waiter.acquired_us >= busy.end_us;
  expect = inherits ? PI_PRIO_HIGH : PI_PRIO_LOW;
  pass = waiter.result == 0 && boosted == expect &&
         mid_first == !inherits && owner.prio_after[0] == PI_PRIO_LOW;

  printf("PRIO_INHERIT_TEST %s %s boosted=%d/%d restored=%d/%d "
         "handoff_us=%lu mid_done_first=%s/%s\n",
         name, pass ? "PASS" : "FAIL", boosted, expect,
         owner.prio_after[0], PI_PRIO_LOW, handoff_us,
         mid_first ? "yes" : "no", inherits ? "no" : "yes");

  pi_owner_deinit(&owner);
  pi_lock_destroy(&lock);
  return pass;

errout:
  printf("PRIO_INHERIT_TEST %s FAIL setup\n", name);
  pi_owner_deinit(&owner);
  pi_lock_destroy(&lock);
  return false;
}

/* The owner holds two mutexes, each with a waiter of a different
 * priority.  It must run at the highest waiter's priority, and after each
 * unlock at the highest priority still waiting on what it holds.
 */

static bool pi_case_nested(bool low_first)
{
  const char *name = low_first ? "nested-low-first" : "nested-high-first";
  struct pi_waiter_s high;
  struct pi_waiter_s higher;
  struct pi_owner_s owner;
  struct pi_lock_s lock_high;
  struct pi_lock_s lock_higher;
  pthread_t owner_tid;
  pthread_t high_tid;
  pthread_t higher_tid;
  int expect_after0;
  int boosted;
  bool pass;

  if (pi_lock_init(&lock_high, PI_PTHREAD_DEFAULT) != 0 ||
      pi_lock_init(&lock_higher, PI_PTHREAD_DEFAULT) != 0)
    {
      printf("PRIO_INHERIT_TEST %s FAIL lock init\n", name);
      return false;
    }

  if (low_first)
    {
      pi_owner_init(&owner, &lock_high, &lock_higher);
      expect_after0 = PI_PRIO_HIGHER;
    }
  else
    {
      pi_owner_init(&owner, &lock_higher, &lock_high);
      expect_after0 = PI_PRIO_HIGH;
    }

  memset(&high, 0, sizeof(high));
  high.lock = &lock_high;
  memset(&higher, 0, sizeof(higher));
  higher.lock = &lock_higher;

  if (pi_create(&owner_tid, PI_PRIO_LOW, pi_owner_main, &owner) != 0)
    {
      goto errout;
    }

  pi_sem_wait(&owner.locked);

  if (pi_create(&high_tid, PI_PRIO_HIGH, pi_waiter_main, &high) != 0)
    {
      sem_post(&owner.release);
      pthread_join(owner_tid, NULL);
      goto errout;
    }

  if (pi_create(&higher_tid, PI_PRIO_HIGHER, pi_waiter_main, &higher) != 0)
    {
      sem_post(&owner.release);
      pthread_join(high_tid, NULL);
      pthread_join(owner_tid, NULL);
      goto errout;
    }

  usleep(PI_SETTLE_MS * 1000);
  boosted = pi_get_prio(owner_tid);

  sem_post(&owner.release);
  pthread_join(higher_tid, NULL);
  pthread_join(high_tid, NULL);
  pthread_join(owner_tid, NULL);

  pass = high.result == 0 && higher.result == 0 &&
         boosted == PI_PRIO_HIGHER && owner.prio_after[0] == expect_after0 &&
         owner.prio_after[1] == PI_PRIO_LOW;

  printf("PRIO_INHERIT_TEST %s %s boosted=%d/%d after_unlock=%d/%d,%d/%d\n",
         name, pass ? "PASS" : "FAIL", boosted, PI_PRIO_HIGHER,
         owner.prio_after[0], expect_after0, owner.prio_after[1],
         PI_PRIO_LOW);

  pi_owner_deinit(&owner);
  pi_lock_destroy(&lock_high);
  pi_lock_destroy(&lock_higher);
  return pass;

errout:
  printf("PRIO_INHERIT_TEST %s FAIL setup\n", name);
  pi_owner_deinit(&owner);
  pi_lock_destroy(&lock_high);
  pi_lock_destroy(&lock_higher);
  return false;
}

/* The boost must be withdrawn when the only waiter times out */

static bool pi_case_timeout(void)
{
  struct pi_owner_s owner;
  struct pi_waiter_s waiter;
  struct pi_lock_s lock;
  pthread_t owner_tid;
  pthread_t waiter_tid;
  int after_timeout;
  int boosted;
  bool pass;

  if (pi_lock_init(&lock, PI_PTHREAD_DEFAULT) != 0)
    {
      printf("PRIO_INHERIT_TEST timeout FAIL lock init\n");
      return false;
    }

  pi_owner_init(&owner, &lock, NULL);
  memset(&waiter, 0, sizeof(waiter));
  waiter.lock = &lock;
  waiter.timeout_ms = PI_TIMEOUT_MS;

  if (pi_create(&owner_tid, PI_PRIO_LOW, pi_owner_main, &owner) != 0)
    {
      goto errout;
    }

  pi_sem_wait(&owner.locked);

  if (pi_create(&waiter_tid, PI_PRIO_HIGH, pi_waiter_main, &waiter) != 0)
    {
      sem_post(&owner.release);
      pthread_join(owner_tid, NULL);
      goto errout;
    }

  usleep(PI_SETTLE_MS * 1000);
  boosted = pi_get_prio(owner_tid);

  pthread_join(waiter_tid, NULL);
  after_timeout = pi_get_prio(owner_tid);

  sem_post(&owner.release);
  pthread_join(owner_tid, NULL);

  pass = waiter.result == ETIMEDOUT && boosted == PI_PRIO_HIGH &&
         after_timeout == PI_PRIO_LOW && owner.prio_after[0] == PI_PRIO_LOW;

  printf("PRIO_INHERIT_TEST timeout %s boosted=%d/%d wait=%s(%d) "
         "after_timeout=%d/%d restored=%d/%d\n",
         pass ? "PASS" : "FAIL", boosted, PI_PRIO_HIGH,
         waiter.result == ETIMEDOUT ? "ETIMEDOUT" : "error", waiter.result,
         after_timeout, PI_PRIO_LOW, owner.prio_after[0], PI_PRIO_LOW);

  pi_owner_deinit(&owner);
  pi_lock_destroy(&lock);
  return pass;

errout:
  printf("PRIO_INHERIT_TEST timeout FAIL setup\n");
  pi_owner_deinit(&owner);
  pi_lock_destroy(&lock);
  return false;
}

static bool pi_run(const char *which, const char *name)
{
  return strcmp(which, "all") == 0 || strcmp(which, name) == 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  const char *which = argc > 1 ? argv[1] : "all";
  struct sched_param param;
  int failures = 0;
  int oldprio;

  if (argc > 2 ||
      (!pi_run(which, "default") && !pi_run(which, "nxmutex") &&
       !pi_run(which, "none") && !pi_run(which, "nested") &&
       !pi_run(which, "timeout")))
    {
      printf("Usage: %s [all|default|nxmutex|none|nested|timeout]\n",
             argv[0]);
      return EXIT_FAILURE;
    }

  sched_getparam(0, &param);
  oldprio = param.sched_priority;
  param.sched_priority = PI_PRIO_CTRL;
  sched_setparam(0, &param);

  if (pi_run(which, "default") &&
      !pi_case_inversion("default", PI_PTHREAD_DEFAULT,
                         PI_DEFAULT_INHERITS))
    {
      failures++;
    }

  if (pi_run(which, "nxmutex") &&
      !pi_case_inversion("nxmutex", PI_NXMUTEX, true))
    {
      failures++;
    }

  if (pi_run(which, "none") &&
      !pi_case_inversion("none", PI_PTHREAD_NONE, false))
    {
      failures++;
    }

  if (pi_run(which, "nested"))
    {
      failures += !pi_case_nested(true);
      failures += !pi_case_nested(false);
    }

  if (pi_run(which, "timeout") && !pi_case_timeout())
    {
      failures++;
    }

  param.sched_priority = oldprio;
  sched_setparam(0, &param);

  printf("PRIO_INHERIT_TEST %s failures=%d\n",
         failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
