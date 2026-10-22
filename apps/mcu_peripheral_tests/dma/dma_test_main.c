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
#include <nuttx/spinlock.h>

#include <arch/chip/bl616cl_dma.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DMA_CHANNEL_COUNT      BL616CL_DMA0_NCHANNELS
#define DMA_TRANSFER_MAX_UNITS 4095u
#define DMA_TEST_MAX_BYTES     ((DMA_TRANSFER_MAX_UNITS + 1u) * 4u)
#define DMA_SMALL_BYTES        128u
#define DMA_WAIT_LOOPS         2000u
#define DMA_GUARD_BYTES        32u
#define DMA_GUARD_VALUE        0xeeu

/* DMA-001 to DMA-006 use the first five free channels */

#define DMA_TEST_MIN_CHANNELS  5u
#define DMA_CACHE_ALIAS        0x40000000u
#define DMA_CACHED_RAM_START   0x60fc0400u
#define DMA_CACHED_RAM_END     0x61010000u

#define RESULT_PASS            0
#define RESULT_FAIL            1

/* LLI nodes shared by the DMA0 channels; the driver splits a transfer into
 * segments of 4064 units and needs a node for each segment after the first
 * (one-shot) or for every segment (cyclic).
 */

#ifdef CONFIG_BL616CL_DMA0_LLI_NODES
#  define DMA_LLI_NODES        CONFIG_BL616CL_DMA0_LLI_NODES
#else
#  define DMA_LLI_NODES        0
#endif

#define DMA_SEGMENT_UNITS      4064u
#define DMA_FULL_POOL_UNITS    ((DMA_LLI_NODES + 1u) * DMA_SEGMENT_UNITS)

/* Cyclic runs: long enough that the 10-bit LLI counter wraps */

#define DMA_CYCLIC_LOADS       1100u

/* DMA-009 to DMA-013 need this many LLI nodes */

#define DMA_LLI_TEST_NODES     6
#define DMA_LLI_TEST_CASES     5

/* Short windows of a cyclic run checked against memory, and the reads of
 * the position allowed in one window
 */

#define DMA_CYCLIC_WINDOWS     4
#define DMA_WINDOW_READS       200000u

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

/* Channels the test may use: those not assigned to an SDK driver.  The
 * cases name them by position, so they run unchanged on any board.
 */

static unsigned int g_channels[DMA_CHANNEL_COUNT];
static unsigned int g_nchannels;

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

  /* A completed transfer reports its full length once, no residual. Give
   * a second callback time to show up.
   */

  usleep(2000);
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
      struct dma_chan_s *chan = DMA_GET_CHAN(dev, g_channels[0]);
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

  fixed_chan = DMA_GET_CHAN(dev, g_channels[0]);
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
  struct dma_chan_s *chan = DMA_GET_CHAN(dev, g_channels[1]);
  struct dma_event_s event;
  size_t length = DMA_TRANSFER_MAX_UNITS * 4u;
  int ret;
  int long_ret;

  printf("[DMA-002] 4095-unit boundary and 4096 units\n");
  memset(&event, 0, sizeof(event));
  fill_pattern(g_source, length + 4u, 0x41);
  up_clean_dcache((uintptr_t)g_source, (uintptr_t)g_source + length + 4u);
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

  /* One unit more needs a second LLI segment: rejected without LLI
   * nodes, one transfer with one callback with them.
   */

  prepare_destination(g_destination, length + 4u);
  if (DMA_LLI_NODES == 0)
    {
      long_ret = DMA_START(chan, dma_callback, &event,
                           (uintptr_t)g_destination, (uintptr_t)g_source,
                           length + 4u);
      long_ret = long_ret == -E2BIG ? OK : -EIO;
    }
  else
    {
      long_ret = start_and_check(chan, &event, (uintptr_t)g_destination,
                                 (uintptr_t)g_source, length + 4u);
      if (long_ret >= 0)
        {
          long_ret = check_destination(g_source, g_destination,
                                       length + 4u);
        }
    }

  printf("  units=4095 ret=%d; units=4096 ret=%d (LLI nodes %u)\n",
         ret, long_ret, DMA_LLI_NODES);
  DMA_PUT_CHAN(dev, chan);
  return ret < 0 ? ret : long_ret;
}

static int run_invalid_atomic_case(struct dma_dev_s *dev)
{
  struct dma_chan_s *chan = DMA_GET_CHAN(dev, g_channels[2]);
  struct dma_config_s invalid;
  struct dma_event_s event;
  uintptr_t source = (uintptr_t)g_source;
  uintptr_t destination = (uintptr_t)g_destination;
  int ret;
  int config_step;
  int config_direction;
  int config_width;
  int config_drq;
  int config_periph_drq;
  int config_drq_range;
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
  invalid.direction = DMA_DEV_TO_DEV;
  config_direction = DMA_CONFIG(chan, &invalid);
  invalid.direction = DMA_MEM_TO_MEM;
  invalid.dst_width = 2;
  config_width = DMA_CONFIG(chan, &invalid);
  invalid.dst_width = 4;
  invalid.src_drq = 1;
  config_drq = DMA_CONFIG(chan, &invalid);

  /* Peripheral directions take a request only on the device side, and the
   * request must fit the 6-bit hardware field.
   */

  invalid.direction = DMA_MEM_TO_DEV;
  invalid.dst_step = 0;
  config_periph_drq = DMA_CONFIG(chan, &invalid);
  invalid.direction = DMA_DEV_TO_MEM;
  invalid.src_step = 0;
  invalid.dst_step = 4;
  invalid.src_drq = 64;
  config_drq_range = DMA_CONFIG(chan, &invalid);
  invalid.direction = DMA_MEM_TO_MEM;
  invalid.src_step = 4;
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
                            64, 24);
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

  /* A buffer that is not a whole number of periods; nothing to pause or
   * resume on a channel that is not running.
   */

  if (cyclic != (DMA_LLI_NODES == 0 ? -ENOTSUP : -EINVAL))
    {
      contract_failures |= 1u << 14;
    }

  if (pause != -EINVAL)
    {
      contract_failures |= 1u << 15;
    }

  if (resume != -EINVAL)
    {
      contract_failures |= 1u << 16;
    }

  if (config_periph_drq != -EINVAL)
    {
      contract_failures |= 1u << 17;
    }

  if (config_drq_range != -EINVAL)
    {
      contract_failures |= 1u << 18;
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

  printf("  config invalid=%d/%d/%d/%d/%d/%d unsupported=%d/%d/%d\n",
         config_step, config_direction, config_width, config_drq,
         config_periph_drq, config_drq_range, config_priority,
         config_timeout, config_option);
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

  printf("[DMA-004] all free channels and shared TC IRQ\n");
  memset(channels, 0, sizeof(channels));
  memset(events, 0, sizeof(events));
  for (i = 0; i < g_nchannels; i++)
    {
      channels[i] = DMA_GET_CHAN(dev, g_channels[i]);
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

  for (i = 0; ret >= 0 && i < g_nchannels; i++)
    {
      ret = DMA_START(channels[i], dma_callback, &events[i],
                      (uintptr_t)g_parallel_destination[i],
                      (uintptr_t)g_parallel_source[i], DMA_SMALL_BYTES);
    }

  for (i = 0; ret >= 0 && i < g_nchannels; i++)
    {
      ret = wait_for_callback(&events[i]);
    }

  for (i = 0; i < g_nchannels; i++)
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

  printf("  channels=%u ret=%d "
         "(resource/IRQ dispatch evidence, not bandwidth)\n", g_nchannels,
         ret);
  return ret;
}

static int run_null_callback_case(struct dma_dev_s *dev)
{
  struct dma_chan_s *chan = DMA_GET_CHAN(dev, g_channels[3]);
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
  struct dma_chan_s *chan = DMA_GET_CHAN(dev, g_channels[4]);
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
  for (i = 0; i < g_nchannels; i++)
    {
      channels[i] = DMA_GET_CHAN(dev, g_channels[i]);
      if (channels[i] == NULL)
        {
          ret = -EIO;
          break;
        }
    }

  waiter.dev = dev;
  waiter.ident = g_channels[g_nchannels - 1];
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

  if (channels[g_nchannels - 1] != NULL)
    {
      DMA_PUT_CHAN(dev, channels[g_nchannels - 1]);
      channels[g_nchannels - 1] = NULL;
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

  for (i = 0; i < g_nchannels; i++)
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

  owned = DMA_GET_CHAN(dev, g_channels[g_nchannels - 2]);
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

  owned = DMA_GET_CHAN(dev, g_channels[g_nchannels - 2]);
  if (owned == NULL)
    {
      return -EIO;
    }

  memset(&waiter, 0, sizeof(waiter));
  waiter.dev = dev;
  waiter.ident = g_channels[g_nchannels - 2];
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

  printf("  extra-waiter=blocked/woken concurrent-put=single-post ret=%d\n",
         ret);
  return ret;
}

#if DMA_LLI_NODES >= DMA_LLI_TEST_NODES
struct dma_cyclic_s
{
  struct dma_chan_s *chan;
  size_t period;
  unsigned int periods;
  unsigned int stop_at;         /* Stop from the callback at this count */
  volatile unsigned int callbacks;
  volatile unsigned int mismatches;
  volatile ssize_t last;        /* len of the last callback */
};

/* Transfers started one after another from the callback */

struct dma_chain_s
{
  uintptr_t destination;
  uintptr_t source;
  size_t length;
  unsigned int count;
  volatile unsigned int done;
  volatile int error;
};

/* Fixed source and destination words: the length costs no buffer, so a
 * transfer can take every LLI node.
 */

static uint32_t g_fixed_source = 0x5a5aa5a5u;
static uint32_t g_fixed_destination;

/* No byte is 0, and the pattern repeats every 251 bytes, so a segment
 * written to the wrong place or not at all shows up.
 */

static void fill_nonzero(uint8_t *buffer, size_t length)
{
  size_t i;

  for (i = 0; i < length; i++)
    {
      buffer[i] = (uint8_t)(i % 251u + 1u);
    }
}

/* Each period callback must report the offset at the end of the next
 * period in order, so a lost or repeated period shows up as a mismatch.
 */

static void dma_cyclic_callback(struct dma_chan_s *chan, void *arg,
                                ssize_t len)
{
  struct dma_cyclic_s *cyclic = arg;
  size_t expected = ((cyclic->callbacks + 1u) % cyclic->periods) *
                    cyclic->period;

  if (len != (ssize_t)expected)
    {
      cyclic->mismatches++;
    }

  cyclic->last = len;
  cyclic->callbacks++;
  if (cyclic->stop_at != 0 && cyclic->callbacks == cyclic->stop_at)
    {
      DMA_STOP(chan);
    }
}

static void dma_chain_callback(struct dma_chan_s *chan, void *arg,
                               ssize_t len)
{
  struct dma_chain_s *chain = arg;
  int ret;

  if (len != (ssize_t)chain->length)
    {
      chain->error = len < 0 ? (int)len : -EIO;
      return;
    }

  /* The finished transfer has given its nodes back already, so the next
   * one may take all of them.
   */

  if (++chain->done < chain->count)
    {
      ret = DMA_START(chan, dma_chain_callback, chain, chain->destination,
                      chain->source, chain->length);
      if (ret < 0)
        {
          chain->error = ret;
        }
    }
}

static int wait_for_count(const volatile unsigned int *count,
                          unsigned int target)
{
  unsigned int i;

  for (i = 0; i < DMA_WAIT_LOOPS; i++)
    {
      if (*count >= target)
        {
          return OK;
        }

      usleep(1000);
    }

  return -ETIMEDOUT;
}

static int start_cyclic(struct dma_cyclic_s *cyclic, uintptr_t destination,
                        uintptr_t source)
{
  cyclic->callbacks = 0;
  cyclic->mismatches = 0;
  cyclic->last = 0;
  return DMA_START_CYCLIC(cyclic->chan, dma_cyclic_callback, cyclic,
                          destination, source,
                          cyclic->period * cyclic->periods, cyclic->period);
}

static int start_fixed(struct dma_chan_s *chan, struct dma_event_s *event,
                       size_t units)
{
  int ret;

  ret = configure_mem2mem(chan, 4, 0, 0);
  if (ret < 0)
    {
      return ret;
    }

  event->callbacks = 0;
  event->result = 0;
  return DMA_START(chan, dma_callback, event,
                   (uintptr_t)&g_fixed_destination,
                   (uintptr_t)&g_fixed_source, units * 4u);
}

static int run_long_case(struct dma_dev_s *dev)
{
  static const unsigned int widths[] =
  {
    1, 1, 1, 2, 4
  };

  /* 3 segments with a last one of 1 unit, 3 full segments, then 4, 3 and
   * 2 segments
   */

  static const size_t lengths[] =
  {
    DMA_SEGMENT_UNITS * 2u + 1u, DMA_SEGMENT_UNITS * 3u,
    DMA_SEGMENT_UNITS * 3u + 100u, DMA_TEST_MAX_BYTES, DMA_TEST_MAX_BYTES
  };

  const unsigned int ntransfers = sizeof(widths) / sizeof(widths[0]);
  struct dma_chan_s *chan = DMA_GET_CHAN(dev, g_channels[0]);
  struct dma_chain_s chain;
  struct dma_event_s event;
  unsigned int i;
  int full_ret = OK;
  int over_ret = OK;
  int chain_ret = OK;
  int ret = OK;

  printf("[DMA-009] one-shot transfers over 4095 units\n");
  memset(&event, 0, sizeof(event));
  memset(&chain, 0, sizeof(chain));
  for (i = 0; ret >= 0 && i < ntransfers * 12u; i++)
    {
      unsigned int width = widths[i % ntransfers];
      size_t length = lengths[i % ntransfers];

      fill_pattern(g_source, length, (uint8_t)(0x10 + i));
      up_clean_dcache((uintptr_t)g_source, (uintptr_t)g_source + length);
      prepare_destination(g_destination, length);
      ret = configure_mem2mem(chan, width, width, width);
      if (ret >= 0)
        {
          ret = start_and_check(chan, &event, (uintptr_t)g_destination,
                                (uintptr_t)g_source, length);
        }

      if (ret >= 0)
        {
          ret = check_destination(g_source, g_destination, length);
        }

      if (ret < 0)
        {
          printf("  width=%u length=%lu round=%u ret=%d\n", width,
                 (unsigned long)length, i / ntransfers, ret);
        }
    }

  /* After 60 transfers the nodes must all be back: one transfer takes all
   * of them, one more segment is too many.
   */

  if (ret >= 0)
    {
      full_ret = start_fixed(chan, &event, DMA_FULL_POOL_UNITS);
      if (full_ret >= 0)
        {
          full_ret = wait_for_callback(&event);
        }

      if (full_ret >= 0 &&
          (event.callbacks != 1 ||
           event.result != (ssize_t)(DMA_FULL_POOL_UNITS * 4u) ||
           DMA_RESIDUAL(chan) != 0))
        {
          full_ret = -EIO;
        }

      over_ret = start_fixed(chan, &event, DMA_FULL_POOL_UNITS + 1u);
    }

  /* Transfers that take all nodes, each started from the callback of the
   * one before
   */

  if (ret >= 0 && full_ret >= 0)
    {
      chain.destination = (uintptr_t)&g_fixed_destination;
      chain.source = (uintptr_t)&g_fixed_source;
      chain.length = DMA_FULL_POOL_UNITS * 4u;
      chain.count = 20;
      chain_ret = DMA_START(chan, dma_chain_callback, &chain,
                            chain.destination, chain.source, chain.length);
      if (chain_ret >= 0)
        {
          chain_ret = wait_for_count(&chain.done, chain.count);
        }

      if (chain.error != 0)
        {
          chain_ret = chain.error;
        }
    }

  printf("  60 transfers ret=%d; %u nodes ret=%d; %u nodes ret=%d; "
         "%u chained ret=%d\n", ret, DMA_LLI_NODES, full_ret,
         DMA_LLI_NODES + 1u, over_ret, chain.done, chain_ret);
  DMA_PUT_CHAN(dev, chan);
  if (ret < 0 || full_ret < 0)
    {
      return ret < 0 ? ret : full_ret;
    }

  return over_ret != -E2BIG ? -EIO : chain_ret;
}

static int run_pool_case(struct dma_dev_s *dev)
{
  static uint32_t ring_word;
  struct dma_chan_s *holder = DMA_GET_CHAN(dev, g_channels[1]);
  struct dma_chan_s *chan = DMA_GET_CHAN(dev, g_channels[2]);
  struct dma_cyclic_s cyclic;
  struct dma_event_s event;
  int hold_ret;
  int short_ret = -EIO;
  int one_ret = -EIO;
  int again_ret = -EIO;
  int stop_ret = -EIO;
  int put_ret = -EIO;
  int ret;

  printf("[DMA-010] LLI nodes shared between channels\n");
  memset(&event, 0, sizeof(event));
  memset(&cyclic, 0, sizeof(cyclic));

  /* A paused cyclic transfer holds all nodes but one */

  cyclic.chan = holder;
  cyclic.period = DMA_SEGMENT_UNITS * 4u;
  cyclic.periods = DMA_LLI_NODES - 1u;
  hold_ret = configure_mem2mem(holder, 4, 0, 0);
  if (hold_ret >= 0)
    {
      hold_ret = start_cyclic(&cyclic, (uintptr_t)&ring_word,
                              (uintptr_t)&ring_word);
    }

  if (hold_ret >= 0)
    {
      hold_ret = DMA_PAUSE(holder);
    }

  /* Two segments fit in the last node, three do not, and a failed start
   * leaves the channel usable.
   */

  if (hold_ret >= 0)
    {
      short_ret = start_fixed(chan, &event, DMA_SEGMENT_UNITS * 2u + 1u);
      one_ret = start_fixed(chan, &event, DMA_SEGMENT_UNITS * 2u);
      if (one_ret >= 0)
        {
          one_ret = wait_for_callback(&event);
        }

      again_ret = start_fixed(chan, &event, DMA_SEGMENT_UNITS * 2u + 1u);
    }

  /* Stopping the holder gives its nodes back */

  ret = DMA_STOP(holder);
  if (ret >= 0)
    {
      stop_ret = start_fixed(chan, &event, DMA_FULL_POOL_UNITS);
    }

  if (stop_ret >= 0)
    {
      stop_ret = wait_for_callback(&event);
    }

  /* So does releasing it while its cyclic transfer runs */

  if (stop_ret >= 0)
    {
      put_ret = start_cyclic(&cyclic, (uintptr_t)&ring_word,
                             (uintptr_t)&ring_word);
    }

  DMA_PUT_CHAN(dev, holder);
  if (put_ret >= 0)
    {
      put_ret = start_fixed(chan, &event, DMA_FULL_POOL_UNITS);
    }

  if (put_ret >= 0)
    {
      put_ret = wait_for_callback(&event);
    }

  printf("  hold=%d 3-seg=%d 2-seg=%d 3-seg=%d after-stop=%d "
         "after-put=%d\n", hold_ret, short_ret, one_ret, again_ret,
         stop_ret, put_ret);
  DMA_PUT_CHAN(dev, chan);
  if (hold_ret < 0 || ret < 0)
    {
      return hold_ret < 0 ? hold_ret : ret;
    }

  if (short_ret != -ENOMEM || one_ret != OK || again_ret != -ENOMEM)
    {
      return -EIO;
    }

  return stop_ret < 0 ? stop_ret : put_ret;
}

/* Position of a cyclic transfer in its buffer */

static size_t cyclic_position(const struct dma_cyclic_s *cyclic)
{
  size_t length = cyclic->period * cyclic->periods;

  return (length - DMA_RESIDUAL(cyclic->chan)) % length;
}

/* Check a running cyclic transfer against what it did: from a paused
 * position, let the DMA move run bytes on, pausing and resuming around
 * every read of the position, and pause it there. The reads are frequent
 * enough to add up the distance over several rounds of the buffer. One
 * callback must have come for each period end passed, the last one with
 * the offset of that end. In a window shorter than the buffer, which is
 * cleared first, exactly the bytes passed must hold the source.
 */

static int check_cyclic_window(struct dma_cyclic_s *cyclic, size_t run,
                               unsigned int *toggles)
{
  size_t length = cyclic->period * cyclic->periods;
  size_t start;
  size_t at;
  size_t next;
  size_t moved = 0;
  size_t end;
  size_t i;
  unsigned int callbacks;
  unsigned int expected;
  unsigned int reads = 0;
  irqstate_t flags;
  int ret;

  ret = DMA_PAUSE(cyclic->chan);
  if (ret < 0)
    {
      return ret;
    }

  /* Let the interrupt of a period that ended before the pause come */

  usleep(5000);
  callbacks = cyclic->callbacks;
  start = cyclic_position(cyclic);
  if (run < length)
    {
      memset(g_destination, 0, length);
      up_flush_dcache((uintptr_t)g_destination,
                      (uintptr_t)g_destination + length);
    }

  /* Nothing may come between two reads long enough for the DMA to go
   * round the buffer.
   */

  flags = enter_critical_section();
  at = start;
  while (ret >= 0 && moved < run)
    {
      if (++reads > DMA_WINDOW_READS)
        {
          ret = -ETIMEDOUT;
          break;
        }

      ret = DMA_RESUME(cyclic->chan);
      next = cyclic_position(cyclic);
      moved += (next + length - at) % length;
      at = next;
      if (ret >= 0)
        {
          ret = DMA_PAUSE(cyclic->chan);
        }
    }

  leave_critical_section(flags);
  *toggles += reads;
  usleep(5000);
  next = cyclic_position(cyclic);
  moved += (next + length - at) % length;
  end = start + moved;
  if (ret < 0)
    {
      printf("  window %lu+%lu: ret=%d\n", (unsigned long)start,
             (unsigned long)moved, ret);
      return ret;
    }

  if (run < length)
    {
      up_invalidate_dcache((uintptr_t)g_destination,
                           (uintptr_t)g_destination + length);
      for (i = 0; i < length; i++)
        {
          bool written = (i + length - start) % length < moved;

          if (g_destination[i] != (written ? g_source[i] : 0))
            {
              printf("  window %lu+%lu: byte %lu is 0x%02x\n",
                     (unsigned long)start, (unsigned long)moved,
                     (unsigned long)i, g_destination[i]);
              return -EIO;
            }
        }
    }

  expected = end / cyclic->period - start / cyclic->period;
  if (cyclic->callbacks - callbacks != expected ||
      cyclic->last != (ssize_t)(end / cyclic->period * cyclic->period %
                                length))
    {
      printf("  window %lu+%lu: %u callbacks, last %ld, expected %u\n",
             (unsigned long)start, (unsigned long)moved,
             cyclic->callbacks - callbacks, (long)cyclic->last, expected);
      return -EIO;
    }

  return DMA_RESUME(cyclic->chan);
}

static int run_cyclic_ring(struct dma_cyclic_s *cyclic, unsigned int width,
                           bool stop_in_callback)
{
  size_t length = cyclic->period * cyclic->periods;
  unsigned int toggles = 0;
  unsigned int stopped;
  unsigned int i;
  size_t residual;
  int ret;

  /* Enough periods for more than 1024 LLI loads: a period loads at least
   * one node per DMA_SEGMENT_UNITS units.
   */

  unsigned int target = (DMA_CYCLIC_LOADS * DMA_SEGMENT_UNITS * width +
                         cyclic->period - 1u) / cyclic->period;

  fill_nonzero(g_source, length);
  up_clean_dcache((uintptr_t)g_source, (uintptr_t)g_source + length);
  prepare_destination(g_destination, length);
  cyclic->stop_at = 0;
  ret = configure_mem2mem(cyclic->chan, width, width, width);
  if (ret >= 0)
    {
      ret = start_cyclic(cyclic, (uintptr_t)g_destination,
                         (uintptr_t)g_source);
    }

  if (ret >= 0)
    {
      ret = wait_for_count(&cyclic->callbacks, target);
    }

  /* A window over more than 1024 LLI loads, so that the LLI counter
   * wraps in it, then short ones checked against memory
   */

  if (ret >= 0)
    {
      ret = check_cyclic_window(cyclic, DMA_CYCLIC_LOADS *
                                DMA_SEGMENT_UNITS * width, &toggles);
    }

  for (i = 0; ret >= 0 && i < DMA_CYCLIC_WINDOWS; i++)
    {
      ret = check_cyclic_window(cyclic, cyclic->period * 3u / 2u,
                                &toggles);
    }

  /* One more round, so that the whole buffer holds the source again */

  target = cyclic->callbacks + cyclic->periods + 1u;
  if (ret >= 0)
    {
      cyclic->stop_at = stop_in_callback ? target : 0;
      ret = wait_for_count(&cyclic->callbacks, target);
    }

  if (!stop_in_callback || ret < 0)
    {
      DMA_STOP(cyclic->chan);
    }

  /* No callback after the stop, and the residual stays put */

  stopped = cyclic->callbacks;
  residual = DMA_RESIDUAL(cyclic->chan);
  usleep(20000);
  if (ret >= 0 &&
      (cyclic->callbacks != stopped || DMA_RESIDUAL(cyclic->chan) !=
       residual || residual > length || cyclic->mismatches != 0 ||
       (stop_in_callback && stopped != target)))
    {
      ret = -EIO;
    }

  if (ret >= 0)
    {
      ret = check_destination(g_source, g_destination, length);
    }

  printf("  width=%u periods=%u x %lu callbacks=%u toggles=%u "
         "mismatches=%u residual=%lu stop=%s ret=%d\n", width,
         cyclic->periods, (unsigned long)cyclic->period, stopped, toggles,
         cyclic->mismatches, (unsigned long)residual,
         stop_in_callback ? "callback" : "task", ret);
  return ret;
}

static int run_cyclic_case(struct dma_dev_s *dev)
{
  struct dma_cyclic_s cyclic;
  int ret;

  printf("[DMA-011] cyclic transfers\n");
  memset(&cyclic, 0, sizeof(cyclic));
  cyclic.chan = DMA_GET_CHAN(dev, g_channels[3]);

  /* Four one-segment periods, stopped from the callback */

  cyclic.period = DMA_SEGMENT_UNITS;
  cyclic.periods = 4;
  ret = run_cyclic_ring(&cyclic, 1, true);

  /* Two periods of three segments, the last one of 36 units, stopped from
   * the task, then the same channel again
   */

  if (ret >= 0)
    {
      cyclic.period = DMA_SEGMENT_UNITS * 2u + 36u;
      cyclic.periods = 2;
      ret = run_cyclic_ring(&cyclic, 1, false);
    }

  /* Two periods of two 2-byte segments, then of one 4-byte segment */

  if (ret >= 0)
    {
      cyclic.period = DMA_TEST_MAX_BYTES / 2u;
      ret = run_cyclic_ring(&cyclic, 2, false);
    }

  if (ret >= 0)
    {
      ret = run_cyclic_ring(&cyclic, 4, false);
    }

  DMA_PUT_CHAN(dev, cyclic.chan);
  return ret;
}

static int run_pause_case(struct dma_dev_s *dev)
{
  struct dma_cyclic_s cyclic;
  struct dma_event_s event;
  size_t length = DMA_SEGMENT_UNITS * 4u;
  unsigned int paused;
  size_t residual = 0;
  int oneshot_ret = -EIO;
  int busy_start = OK;
  int busy_config = OK;
  int ret;

  printf("[DMA-012] pause and resume\n");
  memset(&cyclic, 0, sizeof(cyclic));
  memset(&event, 0, sizeof(event));
  cyclic.chan = DMA_GET_CHAN(dev, g_channels[4]);
  cyclic.period = DMA_SEGMENT_UNITS;
  cyclic.periods = 4;
  fill_pattern(g_source, length, 0x66);
  up_clean_dcache((uintptr_t)g_source, (uintptr_t)g_source + length);
  prepare_destination(g_destination, length);
  ret = configure_mem2mem(cyclic.chan, 1, 1, 1);
  if (ret >= 0)
    {
      ret = start_cyclic(&cyclic, (uintptr_t)g_destination,
                         (uintptr_t)g_source);
    }

  if (ret >= 0)
    {
      ret = wait_for_count(&cyclic.callbacks, 10);
    }

  /* A period that ended just before the pause may still be reported;
   * after that nothing moves.
   */

  if (ret >= 0)
    {
      ret = DMA_PAUSE(cyclic.chan);
    }

  usleep(2000);
  paused = cyclic.callbacks;
  residual = DMA_RESIDUAL(cyclic.chan);
  usleep(20000);
  if (ret >= 0 && (cyclic.callbacks != paused ||
                   DMA_RESIDUAL(cyclic.chan) != residual))
    {
      ret = -EIO;
    }

  if (ret >= 0)
    {
      ret = DMA_RESUME(cyclic.chan);
    }

  if (ret >= 0)
    {
      ret = wait_for_count(&cyclic.callbacks, paused + 20);
    }

  /* A running channel takes no new transfer and no new configuration */

  busy_start = DMA_START(cyclic.chan, dma_callback, &event,
                         (uintptr_t)g_destination, (uintptr_t)g_source, 64);
  busy_config = configure_mem2mem(cyclic.chan, 1, 1, 1);
  DMA_STOP(cyclic.chan);
  if (ret >= 0 && (busy_start != -EBUSY || busy_config != -EBUSY))
    {
      ret = -EIO;
    }

  if (ret >= 0 && cyclic.mismatches != 0)
    {
      ret = -EIO;
    }

  if (ret >= 0)
    {
      ret = check_destination(g_source, g_destination, length);
    }

  /* One-shot transfers are not paused; this one is still running at the
   * pause
   */

  if (ret >= 0)
    {
      ret = start_fixed(cyclic.chan, &event, DMA_SEGMENT_UNITS * 8u);
    }

  if (ret >= 0)
    {
      oneshot_ret = DMA_PAUSE(cyclic.chan);
      ret = wait_for_callback(&event);
    }

  printf("  paused at %u residual=%lu resumed to %u mismatches=%u "
         "busy=%d/%d one-shot pause=%d ret=%d\n", paused,
         (unsigned long)residual, cyclic.callbacks, cyclic.mismatches,
         busy_start, busy_config, oneshot_ret, ret);
  DMA_PUT_CHAN(dev, cyclic.chan);
  return ret < 0 ? ret : (oneshot_ret == -ENOTSUP ? OK : -EIO);
}

static int run_stop_case(struct dma_dev_s *dev)
{
  struct dma_chan_s *chan = DMA_GET_CHAN(dev, g_channels[0]);
  struct dma_event_s event;
  size_t length = DMA_TEST_MAX_BYTES;
  size_t residual = length;
  size_t copied;
  unsigned int i;
  int ret;

  printf("[DMA-013] stop in the middle of a long transfer\n");
  memset(&event, 0, sizeof(event));

  /* No source byte is 0, so the copied bytes are the leading non-zero
   * bytes of the destination.
   */

  fill_nonzero(g_source, length);
  up_clean_dcache((uintptr_t)g_source, (uintptr_t)g_source + length);
  prepare_destination(g_destination, length);
  ret = configure_mem2mem(chan, 1, 1, 1);
  if (ret >= 0)
    {
      ret = DMA_START(chan, dma_callback, &event, (uintptr_t)g_destination,
                      (uintptr_t)g_source, length);
    }

  while (ret >= 0 && residual > length / 2u)
    {
      residual = DMA_RESIDUAL(chan);
    }

  if (ret >= 0)
    {
      ret = DMA_STOP(chan);
      residual = DMA_RESIDUAL(chan);
    }

  usleep(20000);
  up_invalidate_dcache((uintptr_t)g_destination,
                       (uintptr_t)g_destination + length);
  for (copied = 0; copied < length && g_destination[copied] != 0; copied++)
    {
    }

  if (ret >= 0 &&
      (event.callbacks != 0 || residual == 0 ||
       copied != length - residual ||
       memcmp(g_source, g_destination, copied) != 0))
    {
      ret = -EIO;
    }

  for (i = copied; ret >= 0 && i < length; i++)
    {
      if (g_destination[i] != 0)
        {
          ret = -EIO;
        }
    }

  printf("  residual=%lu copied=%lu callbacks=%u\n",
         (unsigned long)residual, (unsigned long)copied, event.callbacks);

  /* The stopped channel takes a new transfer */

  if (ret >= 0)
    {
      prepare_destination(g_destination, length);
      ret = start_and_check(chan, &event, (uintptr_t)g_destination,
                            (uintptr_t)g_source, length);
    }

  if (ret >= 0)
    {
      ret = check_destination(g_source, g_destination, length);
    }

  /* All nodes are back, also those of the cases before */

  if (ret >= 0)
    {
      ret = start_fixed(chan, &event, DMA_FULL_POOL_UNITS);
    }

  if (ret >= 0)
    {
      ret = wait_for_callback(&event);
    }

  DMA_PUT_CHAN(dev, chan);
  return ret;
}
#endif

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
#if DMA_LLI_NODES >= DMA_LLI_TEST_NODES
    { "DMA-009", run_long_case },
    { "DMA-010", run_pool_case },
    { "DMA-011", run_cyclic_case },
    { "DMA-012", run_pause_case },
    { "DMA-013", run_stop_case },
#endif
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

  /* Globals keep their values between runs of a built-in app */

  g_nchannels = 0;
  for (i = 0; i < DMA_CHANNEL_COUNT; i++)
    {
      if ((BL616CL_DMA0_RESERVED & (1u << i)) == 0)
        {
          g_channels[g_nchannels++] = i;
        }
    }

  printf("DMA0 test: USB2 is command/log transport; "
         "DMA assertions are local\n");
  printf("DMA0 test: %u free channels, skipping assigned 0x%02x\n",
         g_nchannels, (unsigned int)BL616CL_DMA0_RESERVED);
  if (g_nchannels < DMA_TEST_MIN_CHANNELS)
    {
      printf("DMA0 test needs %u free channels\n", DMA_TEST_MIN_CHANNELS);
      return RESULT_FAIL;
    }

  if (DMA_LLI_NODES < DMA_LLI_TEST_NODES)
    {
      printf("DMA0 test: %u LLI nodes, DMA-009 to DMA-013 need %u\n",
             DMA_LLI_NODES, DMA_LLI_TEST_NODES);
    }

  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
      if (run_case(&tests[i], dev) != OK)
        {
          failures++;
        }
    }

  printf("DMA0 summary: passed=%u failed=%d skipped=%u\n",
         (unsigned int)(sizeof(tests) / sizeof(tests[0])) - failures,
         failures,
         DMA_LLI_NODES < DMA_LLI_TEST_NODES ? DMA_LLI_TEST_CASES : 0u);
  return failures == 0 ? RESULT_PASS : RESULT_FAIL;
}
