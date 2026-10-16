/****************************************************************************
 * vendor/bouffalolab/apps/mcu_peripheral_tests/dma/dma_test_main.c
 *
 * BL616CL DMA0 contract and memory-to-memory test cases.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <nuttx/cache.h>
#include <nuttx/dma/dma.h>

#include <arch/chip/bl616cl_dma.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DMA_CHANNEL_COUNT      8u
#define DMA_TRANSFER_MAX_UNITS 4095u
#define DMA_TEST_MAX_BYTES     (DMA_TRANSFER_MAX_UNITS * 4u)
#define DMA_SMALL_BYTES        128u
#define DMA_WAIT_LOOPS         2000u
#define DMA_GUARD_BYTES        32u
#define DMA_GUARD_VALUE        0xeeu
#define DMA_CACHE_ALIAS        0x40000000u
#define DMA_CACHED_RAM_START   0x60fc0400u
#define DMA_CACHED_RAM_END     0x61010000u

#define RESULT_PASS            0
#define RESULT_FAIL            1

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct dma_event_s
{
  volatile unsigned int callbacks;
  volatile ssize_t result;
};

struct dma_get_context_s
{
  struct dma_dev_s *dev;
  unsigned int ident;
  volatile bool started;
  volatile bool returned;
  struct dma_chan_s *chan;
};

struct dma_put_context_s
{
  struct dma_dev_s *dev;
  struct dma_chan_s *chan;
  sem_t *ready;
  sem_t *start;
  volatile bool started;
  volatile bool returned;
};

struct dma_case_s
{
  const char *name;
  int (*run)(struct dma_dev_s *dev);
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint8_t g_source[DMA_TEST_MAX_BYTES] __attribute__((aligned(32)));
static uint8_t g_destination[DMA_TEST_MAX_BYTES + DMA_GUARD_BYTES]
  __attribute__((aligned(32)));
static uint8_t g_parallel_source[DMA_CHANNEL_COUNT][DMA_SMALL_BYTES]
  __attribute__((aligned(32)));
static uint8_t g_parallel_destination[DMA_CHANNEL_COUNT][DMA_SMALL_BYTES]
  __attribute__((aligned(32)));

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void dma_callback(struct dma_chan_s *chan, void *arg,
                         ssize_t len)
{
  struct dma_event_s *event = arg;

  (void)chan;
  event->result = len;
  event->callbacks++;
}

static void *dma_get_thread(void *arg)
{
  struct dma_get_context_s *context = arg;

  context->started = true;
  context->chan = DMA_GET_CHAN(context->dev, context->ident);
  context->returned = true;
  return NULL;
}

static void *dma_put_thread(void *arg)
{
  struct dma_put_context_s *context = arg;

  context->started = true;
  if (context->ready != NULL)
    {
      sem_post(context->ready);
    }

  if (context->start != NULL)
    {
      while (sem_wait(context->start) < 0 && errno == EINTR)
        {
        }
    }

  DMA_PUT_CHAN(context->dev, context->chan);
  context->returned = true;
  return NULL;
}

static int wait_sem(sem_t *sem)
{
  int ret;

  do
    {
      ret = sem_wait(sem);
    }
  while (ret < 0 && errno == EINTR);

  return ret < 0 ? -errno : OK;
}

static int wait_for_flag(const volatile bool *flag)
{
  unsigned int i;

  for (i = 0; i < DMA_WAIT_LOOPS; i++)
    {
      if (*flag)
        {
          return OK;
        }

      usleep(1000);
    }

  return -ETIMEDOUT;
}

static void fill_pattern(uint8_t *buffer, size_t length, uint8_t seed)
{
  size_t i;

  for (i = 0; i < length; i++)
    {
      buffer[i] = (uint8_t)(seed + i * 13u);
    }
}

static int wait_for_callback(const struct dma_event_s *event)
{
  unsigned int i;

  for (i = 0; i < DMA_WAIT_LOOPS; i++)
    {
      if (event->callbacks != 0)
        {
          return OK;
        }

      usleep(1000);
    }

  return -ETIMEDOUT;
}

static void prepare_destination(uint8_t *destination, size_t length)
{
  memset(destination, 0, length);
  memset(destination + length, DMA_GUARD_VALUE, DMA_GUARD_BYTES);

  /* Write the initial contents back and drop the lines, so that the CPU
   * later reads what the DMA left in memory.
   */

  up_flush_dcache((uintptr_t)destination,
                  (uintptr_t)destination + length + DMA_GUARD_BYTES);
}

static int check_destination(const uint8_t *expected, uint8_t *destination,
                             size_t length)
{
  size_t i;

  up_invalidate_dcache((uintptr_t)destination,
                       (uintptr_t)destination + length + DMA_GUARD_BYTES);
  if (memcmp(expected, destination, length) != 0)
    {
      return -EIO;
    }

  /* The DMA must not write past the requested length. */

  for (i = 0; i < DMA_GUARD_BYTES; i++)
    {
      if (destination[length + i] != DMA_GUARD_VALUE)
        {
          return -EIO;
        }
    }

  return OK;
}

static int configure_mem2mem(struct dma_chan_s *chan, unsigned int width,
                             int src_step, int dst_step)
{
  struct dma_config_s config;

  memset(&config, 0, sizeof(config));
  config.direction = DMA_MEM_TO_MEM;
  config.src_width = width;
  config.dst_width = width;
  config.src_step = src_step;
  config.dst_step = dst_step;
  return DMA_CONFIG(chan, &config);
}

static int start_and_check(struct dma_chan_s *chan,
                           struct dma_event_s *event,
                           uintptr_t destination, uintptr_t source,
                           size_t length)
{
  int ret;

  event->callbacks = 0;
  event->result = 0;
  ret = DMA_START(chan, dma_callback, event, destination, source, length);
  if (ret < 0)
    {
      return ret;
    }

  ret = wait_for_callback(event);
  if (ret < 0)
    {
      return ret;
    }

  /* A completed transfer reports its full length once, no residual. */

  if (event->callbacks != 1 || event->result != (ssize_t)length ||
      DMA_RESIDUAL(chan) != 0)
    {
      return -EIO;
    }

  return OK;
}

static int run_width_step_case(struct dma_dev_s *dev)
{
  static const unsigned int widths[] = {
    1, 2, 4
  };

  struct dma_chan_s *fixed_chan;
  struct dma_event_s fixed_event;
  uint8_t expected[DMA_SMALL_BYTES];
  unsigned int i;
  int ret = OK;

  printf("[DMA-001] width/step, TC callback and data\n");
  for (i = 0; i < sizeof(widths) / sizeof(widths[0]); i++)
    {
      struct dma_chan_s *chan = DMA_GET_CHAN(dev, 0);
      struct dma_event_s event;
      size_t length = 256u * widths[i];

      memset(&event, 0, sizeof(event));
      fill_pattern(g_source, length, (uint8_t)(0x20u + i));
      up_clean_dcache((uintptr_t)g_source, (uintptr_t)g_source + length);
      prepare_destination(g_destination, length);
      ret = configure_mem2mem(chan, widths[i], (int)widths[i],
                              (int)widths[i]);
      if (ret >= 0)
        {
          ret = start_and_check(chan, &event, (uintptr_t)g_destination,
                                (uintptr_t)g_source, length);
        }

      if (ret >= 0)
        {
          ret = check_destination(g_source, g_destination, length);
        }

      printf("  width=%u step=width ret=%d callbacks=%u residual=%lu\n",
             widths[i], ret, event.callbacks,
             (unsigned long)DMA_RESIDUAL(chan));
      DMA_PUT_CHAN(dev, chan);
      if (ret < 0)
        {
          return ret;
        }
    }

  /* The source bytes all differ, so only a fixed source address copies
   * the first byte everywhere.
   */

  fixed_chan = DMA_GET_CHAN(dev, 0);
  memset(&fixed_event, 0, sizeof(fixed_event));
  fill_pattern(g_source, DMA_SMALL_BYTES, 0xa5);
  memset(expected, 0xa5, sizeof(expected));
  up_clean_dcache((uintptr_t)g_source,
                  (uintptr_t)g_source + DMA_SMALL_BYTES);
  prepare_destination(g_destination, DMA_SMALL_BYTES);
  ret = configure_mem2mem(fixed_chan, 1, 0, 1);
  if (ret >= 0)
    {
      ret = start_and_check(fixed_chan, &fixed_event,
                            (uintptr_t)g_destination,
                            (uintptr_t)g_source, DMA_SMALL_BYTES);
    }

  if (ret >= 0)
    {
      ret = check_destination(expected, g_destination, DMA_SMALL_BYTES);
    }

  printf("  width=1 src_step=0 ret=%d callbacks=%u\n", ret,
         fixed_event.callbacks);
  DMA_PUT_CHAN(dev, fixed_chan);
  if (ret < 0)
    {
      return ret;
    }

  return OK;
}

static int run_boundary_case(struct dma_dev_s *dev)
{
  struct dma_chan_s *chan = DMA_GET_CHAN(dev, 1);
  struct dma_event_s event;
  size_t length = DMA_TRANSFER_MAX_UNITS * 4u;
  int ret;
  int reject_ret;

  printf("[DMA-002] 4095-unit boundary and 4096 rejection\n");
  memset(&event, 0, sizeof(event));
  fill_pattern(g_source, length, 0x41);
  up_clean_dcache((uintptr_t)g_source, (uintptr_t)g_source + length);
  prepare_destination(g_destination, length);
  ret = configure_mem2mem(chan, 4, 4, 4);
  if (ret >= 0)
    {
      ret = start_and_check(chan, &event, (uintptr_t)g_destination,
                            (uintptr_t)g_source, length);
    }

  if (ret >= 0)
    {
      ret = check_destination(g_source, g_destination, length);
    }

  reject_ret = DMA_START(chan, dma_callback, &event,
                         (uintptr_t)g_destination, (uintptr_t)g_source,
                         length + 4u);
  printf("  units=4095 ret=%d callbacks=%u data=%s; units=4096 ret=%d\n",
         ret, event.callbacks, ret == OK ? "match" : "mismatch", reject_ret);
  DMA_PUT_CHAN(dev, chan);
  return ret < 0 ? ret : (reject_ret == -E2BIG ? OK : -EIO);
}

static int run_invalid_atomic_case(struct dma_dev_s *dev)
{
  struct dma_chan_s *chan = DMA_GET_CHAN(dev, 2);
  struct dma_config_s invalid;
  struct dma_event_s event;
  uintptr_t source = (uintptr_t)g_source;
  uintptr_t destination = (uintptr_t)g_destination;
  int ret;
  int config_step;
  int config_direction;
  int config_width;
  int config_drq;
  int config_priority;
  int config_timeout;
  int config_option;
  int start_zero;
  int start_nonmultiple;
  int start_src_align;
  int start_dst_align;
  int start_overflow;
  int cyclic;
  int pause;
  int resume;
  int contract_ret;
  unsigned int contract_failures = 0;
  size_t residual;

  printf("[DMA-003] invalid config/start atomicity\n");
  memset(&event, 0, sizeof(event));
  ret = configure_mem2mem(chan, 4, 4, 4);
  memset(&invalid, 0, sizeof(invalid));
  invalid.direction = DMA_MEM_TO_MEM;
  invalid.src_width = 4;
  invalid.dst_width = 4;
  invalid.src_step = -4;
  invalid.dst_step = 4;
  config_step = DMA_CONFIG(chan, &invalid);
  invalid.src_step = 4;
  invalid.direction = DMA_MEM_TO_DEV;
  config_direction = DMA_CONFIG(chan, &invalid);
  invalid.direction = DMA_MEM_TO_MEM;
  invalid.dst_width = 2;
  config_width = DMA_CONFIG(chan, &invalid);
  invalid.dst_width = 4;
  invalid.src_drq = 1;
  config_drq = DMA_CONFIG(chan, &invalid);
  invalid.src_drq = 0;
  invalid.priority = 1;
  config_priority = DMA_CONFIG(chan, &invalid);
  invalid.priority = 0;
  invalid.timeout = 1;
  config_timeout = DMA_CONFIG(chan, &invalid);
  invalid.timeout = 0;
  invalid.option = 1;
  config_option = DMA_CONFIG(chan, &invalid);

  start_zero = DMA_START(chan, dma_callback, &event, destination, source, 0);
  start_nonmultiple = DMA_START(chan, dma_callback, &event, destination,
                                source, 6);
  start_src_align = DMA_START(chan, dma_callback, &event, destination,
                              source + 1, 4);
  start_dst_align = DMA_START(chan, dma_callback, &event, destination + 1,
                              source, 4);
  start_overflow = DMA_START(chan, dma_callback, &event,
                             UINTPTR_MAX - 3u, source, 4);
  cyclic = DMA_START_CYCLIC(chan, dma_callback, &event, destination, source,
                            64, 16);
  pause = DMA_PAUSE(chan);
  resume = DMA_RESUME(chan);

  residual = DMA_RESIDUAL(chan);
  contract_ret = ret;
  if (contract_ret < 0)
    {
      contract_failures |= 1u << 0;
    }

  if (residual != 0)
    {
      contract_failures |= 1u << 1;
    }

  if (config_step != -EINVAL)
    {
      contract_failures |= 1u << 2;
    }

  if (config_direction != -EINVAL)
    {
      contract_failures |= 1u << 3;
    }

  if (config_width != -EINVAL)
    {
      contract_failures |= 1u << 4;
    }

  if (config_drq != -EINVAL)
    {
      contract_failures |= 1u << 5;
    }

  if (config_priority != -ENOTSUP)
    {
      contract_failures |= 1u << 6;
    }

  if (config_timeout != -ENOTSUP)
    {
      contract_failures |= 1u << 7;
    }

  if (config_option != -ENOTSUP)
    {
      contract_failures |= 1u << 8;
    }

  if (start_zero != -EINVAL)
    {
      contract_failures |= 1u << 9;
    }

  if (start_nonmultiple != -EINVAL)
    {
      contract_failures |= 1u << 10;
    }

  if (start_src_align != -EINVAL)
    {
      contract_failures |= 1u << 11;
    }

  if (start_dst_align != -EINVAL)
    {
      contract_failures |= 1u << 12;
    }

  if (start_overflow != -EINVAL)
    {
      contract_failures |= 1u << 13;
    }

  if (cyclic != -ENOTSUP)
    {
      contract_failures |= 1u << 14;
    }

  if (pause != -ENOTSUP)
    {
      contract_failures |= 1u << 15;
    }

  if (resume != -ENOTSUP)
    {
      contract_failures |= 1u << 16;
    }

  fill_pattern(g_source, 64, 0x52);
  up_clean_dcache(source, source + 64);
  prepare_destination(g_destination, 64);
  ret = contract_failures == 0 ?
          start_and_check(chan, &event, destination, source, 64) :
          -EIO;
  if (ret >= 0)
    {
      ret = check_destination(g_source, g_destination, 64);
    }

  printf("  config invalid=%d/%d/%d/%d unsupported=%d/%d/%d\n",
         config_step, config_direction, config_width, config_drq,
         config_priority, config_timeout, config_option);
  printf("  start invalid=%d/%d/%d/%d/%d ops=%d/%d/%d retained=%d "
         "residual=%lu contract=0x%05x\n",
         start_zero, start_nonmultiple, start_src_align, start_dst_align,
         start_overflow, cyclic, pause, resume, ret,
         (unsigned long)residual, contract_failures);
  DMA_PUT_CHAN(dev, chan);
  return ret;
}

static int run_parallel_case(struct dma_dev_s *dev)
{
  struct dma_chan_s *channels[DMA_CHANNEL_COUNT];
  struct dma_event_s events[DMA_CHANNEL_COUNT];
  unsigned int i;
  int ret = OK;

  printf("[DMA-004] eight fixed channels and shared TC IRQ\n");
  memset(channels, 0, sizeof(channels));
  memset(events, 0, sizeof(events));
  for (i = 0; i < DMA_CHANNEL_COUNT; i++)
    {
      channels[i] = DMA_GET_CHAN(dev, i);
      fill_pattern(g_parallel_source[i], DMA_SMALL_BYTES,
                   (uint8_t)(0x60u + i));
      memset(g_parallel_destination[i], 0, DMA_SMALL_BYTES);
      up_clean_dcache((uintptr_t)g_parallel_source[i],
                      (uintptr_t)g_parallel_source[i] + DMA_SMALL_BYTES);
      up_invalidate_dcache((uintptr_t)g_parallel_destination[i],
                           (uintptr_t)g_parallel_destination[i] +
                           DMA_SMALL_BYTES);
      ret = configure_mem2mem(channels[i], 1, 1, 1);
      if (ret < 0)
        {
          break;
        }
    }

  for (i = 0; ret >= 0 && i < DMA_CHANNEL_COUNT; i++)
    {
      ret = DMA_START(channels[i], dma_callback, &events[i],
                      (uintptr_t)g_parallel_destination[i],
                      (uintptr_t)g_parallel_source[i], DMA_SMALL_BYTES);
    }

  for (i = 0; ret >= 0 && i < DMA_CHANNEL_COUNT; i++)
    {
      ret = wait_for_callback(&events[i]);
    }

  for (i = 0; i < DMA_CHANNEL_COUNT; i++)
    {
      if (channels[i] != NULL)
        {
          up_invalidate_dcache((uintptr_t)g_parallel_destination[i],
                               (uintptr_t)g_parallel_destination[i] +
                               DMA_SMALL_BYTES);
          if (ret >= 0 && (events[i].callbacks != 1 ||
                           events[i].result != (ssize_t)DMA_SMALL_BYTES ||
                           DMA_RESIDUAL(channels[i]) != 0 ||
                           memcmp(g_parallel_source[i],
                                  g_parallel_destination[i],
                                  DMA_SMALL_BYTES) != 0))
            {
              ret = -EIO;
            }

          DMA_PUT_CHAN(dev, channels[i]);
        }
    }

  printf("  channels=8 ret=%d "
         "(resource/IRQ dispatch evidence, not bandwidth)\n", ret);
  return ret;
}

static int run_null_callback_case(struct dma_dev_s *dev)
{
  struct dma_chan_s *chan = DMA_GET_CHAN(dev, 3);
  size_t residual = DMA_SMALL_BYTES;
  unsigned int i;
  int config_ret = -EBUSY;
  int ret;

  printf("[DMA-005] NULL callback completion\n");
  fill_pattern(g_source, DMA_SMALL_BYTES, 0x73);
  up_clean_dcache((uintptr_t)g_source,
                  (uintptr_t)g_source + DMA_SMALL_BYTES);
  prepare_destination(g_destination, DMA_SMALL_BYTES);
  ret = configure_mem2mem(chan, 1, 1, 1);
  if (ret >= 0)
    {
      ret = DMA_START(chan, NULL, NULL, (uintptr_t)g_destination,
                      (uintptr_t)g_source, DMA_SMALL_BYTES);
    }

  /* Without a callback, completion is visible as a zero residual and as
   * the channel leaving the running state, after which DMA_CONFIG no
   * longer returns -EBUSY.  Reaching it proves that the interrupt handled
   * the transfer without calling a NULL callback.
   */

  for (i = 0; ret >= 0 && i < DMA_WAIT_LOOPS; i++)
    {
      residual = DMA_RESIDUAL(chan);
      if (residual == 0)
        {
          break;
        }

      usleep(1000);
    }

  if (ret >= 0 && residual != 0)
    {
      ret = -ETIMEDOUT;
    }

  for (i = 0; ret >= 0 && i < DMA_WAIT_LOOPS; i++)
    {
      config_ret = configure_mem2mem(chan, 1, 1, 1);
      if (config_ret != -EBUSY)
        {
          break;
        }

      usleep(1000);
    }

  if (ret >= 0)
    {
      ret = config_ret;
    }

  if (ret >= 0)
    {
      ret = check_destination(g_source, g_destination, DMA_SMALL_BYTES);
    }

  printf("  residual=%lu reconfig=%d ret=%d\n", (unsigned long)residual,
         config_ret, ret);
  DMA_PUT_CHAN(dev, chan);
  return ret;
}

static int run_cache_alias_case(struct dma_dev_s *dev)
{
  struct dma_chan_s *chan = DMA_GET_CHAN(dev, 4);
  uint8_t *destination_nc;
  struct dma_event_s event;
  uintptr_t source = (uintptr_t)g_source;
  uintptr_t destination = (uintptr_t)g_destination;
  size_t length = 128;
  int ret;

  printf("[DMA-006] cached source to non-cache destination alias\n");
  memset(&event, 0, sizeof(event));
  if (source < DMA_CACHED_RAM_START ||
      source + length > DMA_CACHED_RAM_END ||
      destination < DMA_CACHED_RAM_START ||
      destination + length + DMA_GUARD_BYTES > DMA_CACHED_RAM_END)
    {
      printf("  buffers outside cached OCRAM alias window\n");
      DMA_PUT_CHAN(dev, chan);
      return -ERANGE;
    }

  destination_nc = (uint8_t *)(destination - DMA_CACHE_ALIAS);
  fill_pattern(g_source, length, 0x84);
  up_clean_dcache(source, source + length);
  prepare_destination(g_destination, length);
  ret = configure_mem2mem(chan, 1, 1, 1);

  /* Use the required cache maintenance while checking alias visibility. */

  if (ret >= 0)
    {
      ret = start_and_check(chan, &event, (uintptr_t)destination_nc, source,
                            length);
    }

  if (ret >= 0)
    {
      ret = check_destination(g_source, g_destination, length);
    }

  printf("  ret=%d callbacks=%u alias=%p\n", ret, event.callbacks,
         destination_nc);
  DMA_PUT_CHAN(dev, chan);
  return ret;
}

static int run_put_reuse_case(struct dma_dev_s *dev)
{
  struct dma_chan_s *channels[DMA_CHANNEL_COUNT];
  struct dma_chan_s *owned;
  struct dma_get_context_s waiter;
  struct dma_put_context_s puts[2];
  pthread_t waiter_thread;
  pthread_t put_threads[2];
  sem_t ready;
  sem_t start;
  unsigned int i;
  unsigned int put_created = 0;
  bool waiter_created = false;
  int thread_ret;
  int ret = OK;

  printf("[DMA-007] owner wait, exhaustion and concurrent put\n");
  memset(channels, 0, sizeof(channels));
  memset(&waiter, 0, sizeof(waiter));
  for (i = 0; i < DMA_CHANNEL_COUNT; i++)
    {
      channels[i] = DMA_GET_CHAN(dev, i);
      if (channels[i] == NULL)
        {
          ret = -EIO;
          break;
        }
    }

  waiter.dev = dev;
  waiter.ident = DMA_CHANNEL_COUNT - 1;
  if (ret >= 0)
    {
      thread_ret = pthread_create(&waiter_thread, NULL, dma_get_thread,
                                  &waiter);
      if (thread_ret != 0)
        {
          ret = -thread_ret;
        }
      else
        {
          waiter_created = true;
        }
    }

  if (ret >= 0)
    {
      ret = wait_for_flag(&waiter.started);
    }

  usleep(20000);
  if (ret >= 0 && waiter.returned)
    {
      ret = -EIO;
    }

  if (channels[DMA_CHANNEL_COUNT - 1] != NULL)
    {
      DMA_PUT_CHAN(dev, channels[DMA_CHANNEL_COUNT - 1]);
      channels[DMA_CHANNEL_COUNT - 1] = NULL;
    }

  if (ret >= 0)
    {
      ret = wait_for_flag(&waiter.returned);
    }

  if (waiter_created)
    {
      pthread_join(waiter_thread, NULL);
    }

  if (ret >= 0 && waiter.chan == NULL)
    {
      ret = -EIO;
    }

  if (waiter.chan != NULL)
    {
      DMA_PUT_CHAN(dev, waiter.chan);
    }

  for (i = 0; i < DMA_CHANNEL_COUNT; i++)
    {
      if (channels[i] != NULL)
        {
          DMA_PUT_CHAN(dev, channels[i]);
        }
    }

  if (ret < 0)
    {
      return ret;
    }

  owned = DMA_GET_CHAN(dev, 6);
  if (owned == NULL)
    {
      return -EIO;
    }

  if (sem_init(&ready, 0, 0) < 0)
    {
      DMA_PUT_CHAN(dev, owned);
      return -errno;
    }

  if (sem_init(&start, 0, 0) < 0)
    {
      ret = -errno;
      sem_destroy(&ready);
      DMA_PUT_CHAN(dev, owned);
      return ret;
    }

  memset(puts, 0, sizeof(puts));
  for (i = 0; i < 2; i++)
    {
      puts[i].dev = dev;
      puts[i].chan = owned;
      puts[i].ready = &ready;
      puts[i].start = &start;
      thread_ret = pthread_create(&put_threads[i], NULL, dma_put_thread,
                                  &puts[i]);
      if (thread_ret != 0)
        {
          ret = -thread_ret;
          break;
        }

      put_created++;
    }

  for (i = 0; i < put_created; i++)
    {
      int wait_ret = wait_sem(&ready);

      if (ret >= 0 && wait_ret < 0)
        {
          ret = wait_ret;
        }
    }

  for (i = 0; i < put_created; i++)
    {
      sem_post(&start);
    }

  for (i = 0; i < put_created; i++)
    {
      pthread_join(put_threads[i], NULL);
    }

  sem_destroy(&start);
  sem_destroy(&ready);
  if (ret < 0)
    {
      if (put_created == 0)
        {
          DMA_PUT_CHAN(dev, owned);
        }

      return ret;
    }

  owned = DMA_GET_CHAN(dev, 6);
  if (owned == NULL)
    {
      return -EIO;
    }

  memset(&waiter, 0, sizeof(waiter));
  waiter.dev = dev;
  waiter.ident = 6;
  thread_ret = pthread_create(&waiter_thread, NULL, dma_get_thread, &waiter);
  if (thread_ret != 0)
    {
      DMA_PUT_CHAN(dev, owned);
      return -thread_ret;
    }

  ret = wait_for_flag(&waiter.started);
  usleep(20000);
  if (ret >= 0 && waiter.returned)
    {
      ret = -EIO;
    }

  DMA_PUT_CHAN(dev, owned);
  if (ret >= 0)
    {
      ret = wait_for_flag(&waiter.returned);
    }

  pthread_join(waiter_thread, NULL);
  if (ret >= 0 && waiter.chan != owned)
    {
      ret = -EIO;
    }

  if (waiter.chan != NULL)
    {
      DMA_PUT_CHAN(dev, waiter.chan);
    }

  printf("  ninth-waiter=blocked/woken concurrent-put=single-post ret=%d\n",
         ret);
  return ret;
}

static int run_case(const struct dma_case_s *test,
                    struct dma_dev_s *dev)
{
  int ret = test->run(dev);

  printf("[%s] %s ret=%d\n", test->name,
         ret == OK ? "PASS" : "FAIL", ret);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  static const struct dma_case_s tests[] =
  {
    { "DMA-001", run_width_step_case },
    { "DMA-002", run_boundary_case },
    { "DMA-003", run_invalid_atomic_case },
    { "DMA-004", run_parallel_case },
    { "DMA-005", run_null_callback_case },
    { "DMA-006", run_cache_alias_case },
    { "DMA-007", run_put_reuse_case },
  };

  struct dma_dev_s *dev;
  unsigned int i;
  int failures = 0;

  (void)argc;
  (void)argv;
  dev = bl616cl_dma0_device();
  if (dev == NULL)
    {
      printf("DMA0 test unavailable\n");
      return RESULT_FAIL;
    }

  printf("DMA0 test: USB2 is command/log transport; "
         "DMA assertions are local\n");
  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
      if (run_case(&tests[i], dev) != OK)
        {
          failures++;
        }
    }

  printf("DMA0 summary: passed=%u failed=%d\n",
         (unsigned int)(sizeof(tests) / sizeof(tests[0])) - failures,
         failures);
  return failures == 0 ? RESULT_PASS : RESULT_FAIL;
}
