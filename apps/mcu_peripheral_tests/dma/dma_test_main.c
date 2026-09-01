/****************************************************************************
 * apps/vendor/bouffalolab/apps/mcu_peripheral_tests/dma/dma_test_main.c
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
#include <arch/chip/bl616cl_dma_test.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DMA_CHANNEL_COUNT      8u
#define DMA_TRANSFER_MAX_UNITS 4095u
#define DMA_TEST_MAX_BYTES     (DMA_TRANSFER_MAX_UNITS * 4u)
#define DMA_SMALL_BYTES        128u
#define DMA_WAIT_LOOPS         2000u
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
  volatile bool stop_in_callback;
  volatile bool put_in_callback;
  bool block_callback;
  FAR sem_t *callback_entered;
  FAR sem_t *callback_release;
  FAR struct dma_dev_s *dev;
  FAR struct dma_chan_s *chan;
};

struct dma_get_context_s
{
  FAR struct dma_dev_s *dev;
  unsigned int ident;
  volatile bool started;
  volatile bool returned;
  FAR struct dma_chan_s *chan;
};

struct dma_put_context_s
{
  FAR struct dma_dev_s *dev;
  FAR struct dma_chan_s *chan;
  FAR sem_t *ready;
  FAR sem_t *start;
  volatile bool started;
  volatile bool returned;
};

struct dma_inject_context_s
{
  uint8_t tc_status;
  uint8_t error_status;
  volatile bool returned;
};

struct dma_case_s
{
  const char *name;
  int (*run)(FAR struct dma_dev_s *dev);
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint8_t g_source[DMA_TEST_MAX_BYTES] __attribute__((aligned(32)));
static uint8_t g_destination[DMA_TEST_MAX_BYTES]
  __attribute__((aligned(32)));
static uint8_t g_parallel_source[DMA_CHANNEL_COUNT][DMA_SMALL_BYTES]
  __attribute__((aligned(32)));
static uint8_t g_parallel_destination[DMA_CHANNEL_COUNT][DMA_SMALL_BYTES]
  __attribute__((aligned(32)));

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void dma_callback(FAR struct dma_chan_s *chan, FAR void *arg,
                         ssize_t len)
{
  FAR struct dma_event_s *event = arg;

  event->result = len;
  event->callbacks++;

  if (event->block_callback)
    {
      sem_post(event->callback_entered);
      while (sem_wait(event->callback_release) < 0 && errno == EINTR)
        {
        }
    }

  if (event->stop_in_callback)
    {
      (void)DMA_STOP(chan);
    }

  if (event->put_in_callback)
    {
      /* The adapter must reject this void API in callback context. */

      DMA_PUT_CHAN(event->dev, chan);
    }
}

static FAR void *dma_get_thread(FAR void *arg)
{
  FAR struct dma_get_context_s *context = arg;

  context->started = true;
  context->chan = DMA_GET_CHAN(context->dev, context->ident);
  context->returned = true;
  return NULL;
}

static FAR void *dma_put_thread(FAR void *arg)
{
  FAR struct dma_put_context_s *context = arg;

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

static FAR void *dma_inject_thread(FAR void *arg)
{
  FAR struct dma_inject_context_s *context = arg;

  bl616cl_dma_test_inject_irq(context->tc_status, context->error_status);
  context->returned = true;
  return NULL;
}

static int wait_sem(FAR sem_t *sem)
{
  int ret;

  do
    {
      ret = sem_wait(sem);
    }
  while (ret < 0 && errno == EINTR);

  return ret < 0 ? -errno : OK;
}

static int wait_for_flag(FAR const volatile bool *flag)
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

static int wait_for_irq_count(uint32_t count)
{
  struct bl616cl_dma_test_status_s status;
  unsigned int i;

  for (i = 0; i < DMA_WAIT_LOOPS; i++)
    {
      bl616cl_dma_test_get_status(&status);
      if (status.irq_count >= count)
        {
          return OK;
        }

      usleep(1000);
    }

  return -ETIMEDOUT;
}

static void fill_pattern(FAR uint8_t *buffer, size_t length, uint8_t seed)
{
  size_t i;

  for (i = 0; i < length; i++)
    {
      buffer[i] = (uint8_t)(seed + i * 13u);
    }
}

static int wait_for_callback(FAR const struct dma_event_s *event)
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

static void print_irq_status(const char *label)
{
  struct bl616cl_dma_test_status_s status;

  bl616cl_dma_test_get_status(&status);
  printf("  %s irq: tc=0x%02x/tc_clear=0x%02x "
         "error=0x%02x/error_clear=0x%02x irq_count=%lu "
         "injection_count=%lu callback_count=%lu rejected_puts=%lu\n",
         label,
         status.tc_status, status.tc_clear_status, status.error_status,
         status.error_clear_status,
         (unsigned long)status.irq_count,
         (unsigned long)status.software_injection_count,
         (unsigned long)status.callback_count,
         (unsigned long)status.rejected_puts);
}

static int configure_mem2mem(FAR struct dma_chan_s *chan, unsigned int width,
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

static int start_and_check(FAR struct dma_chan_s *chan,
                           FAR struct dma_event_s *event,
                           uintptr_t destination, uintptr_t source,
                           size_t length, uint8_t channel_bit, bool callback)
{
  struct bl616cl_dma_test_status_s before;
  struct bl616cl_dma_test_status_s after;
  int ret;

  bl616cl_dma_test_get_status(&before);
  event->callbacks = 0;
  event->result = 0;
  event->chan = chan;
  ret = DMA_START(chan, callback ? dma_callback : NULL, event,
                  destination, source, length);
  if (ret < 0)
    {
      return ret;
    }

  if (!callback)
    {
      return OK;
    }

  ret = wait_for_callback(event);
  if (ret < 0)
    {
      return ret;
    }

  if (event->callbacks != 1 || event->result != (ssize_t)length)
    {
      return -EIO;
    }

  bl616cl_dma_test_get_status(&after);
  if (after.irq_count != before.irq_count + 1u ||
      after.callback_count != before.callback_count + 1u ||
      (after.tc_status & channel_bit) == 0 ||
      (after.tc_clear_status & channel_bit) == 0 ||
      (after.error_status & channel_bit) != 0 ||
      (after.error_clear_status & channel_bit) != 0)
    {
      return -EIO;
    }

  return OK;
}

static int run_width_step_case(FAR struct dma_dev_s *dev)
{
  static const unsigned int widths[] = {
    1, 2, 4
  };

  FAR struct dma_chan_s *fixed_chan;
  struct dma_event_s fixed_event;
  unsigned int i;
  int ret = OK;

  printf("[DMA-001] width/step, TC callback and data\n");
  for (i = 0; i < sizeof(widths) / sizeof(widths[0]); i++)
    {
      FAR struct dma_chan_s *chan = DMA_GET_CHAN(dev, 0);
      struct dma_event_s event;
      size_t length = 256u * widths[i];

      memset(&event, 0, sizeof(event));
      fill_pattern(g_source, length, (uint8_t)(0x20u + i));
      memset(g_destination, 0, length);
      up_clean_dcache((uintptr_t)g_source, (uintptr_t)g_source + length);
      up_invalidate_dcache((uintptr_t)g_destination,
                           (uintptr_t)g_destination + length);
      ret = configure_mem2mem(chan, widths[i], (int)widths[i],
                              (int)widths[i]);
      if (ret >= 0)
        {
          ret = start_and_check(chan, &event, (uintptr_t)g_destination,
                                (uintptr_t)g_source, length, 1u << 0, true);
        }

      up_invalidate_dcache((uintptr_t)g_destination,
                           (uintptr_t)g_destination + length);
      if (ret >= 0 && memcmp(g_source, g_destination, length) != 0)
        {
          ret = -EIO;
        }

      printf("  width=%u step=width ret=%d callbacks=%u residual=%lu\n",
             widths[i], ret, event.callbacks,
             (unsigned long)DMA_RESIDUAL(chan));
      print_irq_status("DMA-001");
      DMA_PUT_CHAN(dev, chan);
      if (ret < 0)
        {
          return ret;
        }
    }

  fixed_chan = DMA_GET_CHAN(dev, 0);
  memset(&fixed_event, 0, sizeof(fixed_event));
  g_source[0] = 0xa5;
  memset(g_destination, 0, DMA_SMALL_BYTES);
  up_clean_dcache((uintptr_t)g_source, (uintptr_t)g_source + 1);
  up_invalidate_dcache((uintptr_t)g_destination,
                       (uintptr_t)g_destination + DMA_SMALL_BYTES);
  ret = configure_mem2mem(fixed_chan, 1, 0, 1);
  if (ret >= 0)
    {
      ret = start_and_check(fixed_chan, &fixed_event,
                            (uintptr_t)g_destination,
                            (uintptr_t)g_source, DMA_SMALL_BYTES, 1u << 0,
                            true);
    }

  up_invalidate_dcache((uintptr_t)g_destination,
                       (uintptr_t)g_destination + DMA_SMALL_BYTES);
  for (i = 0; ret >= 0 && i < DMA_SMALL_BYTES; i++)
    {
      if (g_destination[i] != 0xa5)
        {
          ret = -EIO;
        }
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

static int run_boundary_case(FAR struct dma_dev_s *dev)
{
  FAR struct dma_chan_s *chan = DMA_GET_CHAN(dev, 1);
  struct dma_event_s event;
  size_t length = DMA_TRANSFER_MAX_UNITS * 4u;
  int ret;
  int reject_ret;

  printf("[DMA-002] 4095-unit boundary and 4096 rejection\n");
  memset(&event, 0, sizeof(event));
  fill_pattern(g_source, length, 0x41);
  memset(g_destination, 0, length);
  up_clean_dcache((uintptr_t)g_source, (uintptr_t)g_source + length);
  up_invalidate_dcache((uintptr_t)g_destination,
                       (uintptr_t)g_destination + length);
  ret = configure_mem2mem(chan, 4, 4, 4);
  if (ret >= 0)
    {
      ret = start_and_check(chan, &event, (uintptr_t)g_destination,
                            (uintptr_t)g_source, length, 1u << 1, true);
    }

  up_invalidate_dcache((uintptr_t)g_destination,
                       (uintptr_t)g_destination + length);
  if (ret >= 0 && memcmp(g_source, g_destination, length) != 0)
    {
      ret = -EIO;
    }

  reject_ret = DMA_START(chan, dma_callback, &event,
                         (uintptr_t)g_destination, (uintptr_t)g_source,
                         length + 4u);
  printf("  units=4095 ret=%d callbacks=%u data=%s; units=4096 ret=%d\n",
         ret, event.callbacks, ret == OK ? "match" : "mismatch", reject_ret);
  print_irq_status("DMA-002");
  DMA_PUT_CHAN(dev, chan);
  return ret < 0 ? ret : (reject_ret == -E2BIG ? OK : -EIO);
}

static int run_invalid_atomic_case(FAR struct dma_dev_s *dev)
{
  FAR struct dma_chan_s *chan = DMA_GET_CHAN(dev, 2);
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
  memset(g_destination, 0, 64);
  up_clean_dcache(source, source + 64);
  up_invalidate_dcache(destination, destination + 64);
  ret = contract_failures == 0 ?
          start_and_check(chan, &event, destination, source, 64,
                          1u << 2, true) :
          -EIO;
  up_invalidate_dcache(destination, destination + 64);
  if (ret >= 0 && memcmp(g_source, g_destination, 64) != 0)
    {
      ret = -EIO;
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

static int run_parallel_case(FAR struct dma_dev_s *dev)
{
  FAR struct dma_chan_s *channels[DMA_CHANNEL_COUNT];
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
      events[i].chan = channels[i];
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
  print_irq_status("DMA-004");
  return ret;
}

static int run_callback_and_injection_case(FAR struct dma_dev_s *dev)
{
  FAR struct dma_chan_s *chan = DMA_GET_CHAN(dev, 3);
  struct dma_event_s event;
  struct bl616cl_dma_test_status_s before;
  struct bl616cl_dma_test_status_s after;
  struct bl616cl_dma_test_status_s null_before;
  struct bl616cl_dma_test_status_s null_after;
  int ret;
  int error_residual = -1;

  printf("[DMA-005] NULL callback, callback stop/put guard, "
         "software IRQ injection\n");
  memset(&event, 0, sizeof(event));
  memset(&before, 0, sizeof(before));
  memset(&after, 0, sizeof(after));
  memset(&null_before, 0, sizeof(null_before));
  memset(&null_after, 0, sizeof(null_after));
  ret = configure_mem2mem(chan, 1, 1, 1);
  fill_pattern(g_source, DMA_SMALL_BYTES, 0x73);
  memset(g_destination, 0, DMA_SMALL_BYTES);
  up_clean_dcache((uintptr_t)g_source,
                  (uintptr_t)g_source + DMA_SMALL_BYTES);
  up_invalidate_dcache((uintptr_t)g_destination,
                       (uintptr_t)g_destination + DMA_SMALL_BYTES);
  if (ret >= 0)
    {
      bl616cl_dma_test_get_status(&null_before);
      ret = start_and_check(chan, &event, (uintptr_t)g_destination,
                            (uintptr_t)g_source, DMA_SMALL_BYTES, 1u << 3,
                            false);
    }

  /* A NULL callback must reach a terminal state without a NULL call. */

  if (ret >= 0)
    {
      ret = wait_for_irq_count(null_before.irq_count + 1u);
      bl616cl_dma_test_get_status(&null_after);
    }

  if (ret >= 0 &&
      (DMA_RESIDUAL(chan) != 0 ||
       null_after.irq_count != null_before.irq_count + 1u ||
       null_after.callback_count != null_before.callback_count ||
       (null_after.tc_status & (1u << 3)) == 0 ||
       (null_after.tc_clear_status & (1u << 3)) == 0 ||
       (null_after.error_status & (1u << 3)) != 0))
    {
      ret = -EIO;
    }

  if (ret >= 0)
    {
      event.stop_in_callback = true;
      event.put_in_callback = true;
      event.dev = dev;
      bl616cl_dma_test_get_status(&before);
      bl616cl_dma_test_suppress_put_assert(true);
      up_invalidate_dcache((uintptr_t)g_destination,
                           (uintptr_t)g_destination + DMA_SMALL_BYTES);
      ret = start_and_check(chan, &event, (uintptr_t)g_destination,
                            (uintptr_t)g_source, DMA_SMALL_BYTES, 1u << 3,
                            true);
      bl616cl_dma_test_suppress_put_assert(false);
      bl616cl_dma_test_get_status(&after);
      if (ret >= 0 && after.rejected_puts != before.rejected_puts + 1u)
        {
          ret = -EIO;
        }
    }

  /* This covers the adapter contract, not a hardware error. */

  if (ret >= 0)
    {
      memset(&event, 0, sizeof(event));
      bl616cl_dma_test_set_hold_before_enable(true);
      ret = DMA_START(chan, dma_callback, &event, (uintptr_t)g_destination,
                      (uintptr_t)g_source, DMA_SMALL_BYTES);
      if (ret >= 0)
        {
          bl616cl_dma_test_inject_irq(1u << 3, 1u << 3);
          ret = wait_for_callback(&event);
          if (ret >= 0 &&
              (event.callbacks != 1 || event.result != -EIO ||
               DMA_RESIDUAL(chan) != DMA_SMALL_BYTES))
            {
              ret = -EIO;
            }

          error_residual = DMA_RESIDUAL(chan);
        }

      bl616cl_dma_test_release_hold();
      bl616cl_dma_test_set_hold_before_enable(false);
    }

  if (ret >= 0)
    {
      memset(&event, 0, sizeof(event));
      up_invalidate_dcache((uintptr_t)g_destination,
                           (uintptr_t)g_destination + DMA_SMALL_BYTES);
      ret = start_and_check(chan, &event, (uintptr_t)g_destination,
                            (uintptr_t)g_source, DMA_SMALL_BYTES, 1u << 3,
                            true);
    }

  print_irq_status("DMA-005 injected TC|error");
  printf("  ret=%d error-residual=%d restart-callback=%u "
         "rejected-put-delta=%lu\n",
         ret, error_residual, event.callbacks,
         (unsigned long)(after.rejected_puts - before.rejected_puts));
  DMA_PUT_CHAN(dev, chan);
  return ret;
}

static int run_hold_and_cache_case(FAR struct dma_dev_s *dev)
{
  FAR struct dma_chan_s *chan = DMA_GET_CHAN(dev, 4);
  FAR uint8_t *source_nc;
  FAR uint8_t *destination_nc;
  struct dma_event_s event;
  uintptr_t source = (uintptr_t)g_source;
  uintptr_t destination = (uintptr_t)g_destination;
  size_t length = 128;
  int config_busy = 0;
  int start_busy = 0;
  int repeat_stop = 0;
  int ret;

  printf("[DMA-006] pre-enable stop/residual and cached/nocache aliases\n");
  memset(&event, 0, sizeof(event));
  if (source < DMA_CACHED_RAM_START ||
      source + length > DMA_CACHED_RAM_END ||
      destination < DMA_CACHED_RAM_START ||
      destination + length > DMA_CACHED_RAM_END)
    {
      printf("  buffers outside cached OCRAM alias window\n");
      DMA_PUT_CHAN(dev, chan);
      return -ERANGE;
    }

  source_nc = (FAR uint8_t *)(source - DMA_CACHE_ALIAS);
  destination_nc = (FAR uint8_t *)(destination - DMA_CACHE_ALIAS);
  fill_pattern(g_source, length, 0x84);
  memset(g_destination, 0, length);
  up_clean_dcache(source, source + length);
  up_invalidate_dcache(destination, destination + length);
  ret = configure_mem2mem(chan, 1, 1, 1);
  bl616cl_dma_test_set_hold_before_enable(true);
  if (ret >= 0)
    {
      ret = DMA_START(chan, dma_callback, &event, (uintptr_t)destination_nc,
                      (uintptr_t)source_nc, length);
    }

  if (ret >= 0 && DMA_RESIDUAL(chan) != length)
    {
      ret = -EIO;
    }

  if (ret >= 0)
    {
      config_busy = configure_mem2mem(chan, 1, 1, 1);
      start_busy = DMA_START(chan, dma_callback, &event,
                             (uintptr_t)destination_nc,
                             (uintptr_t)source_nc, length);
      if (config_busy != -EBUSY || start_busy != -EBUSY)
        {
          ret = -EIO;
        }
    }

  if (ret >= 0)
    {
      ret = DMA_STOP(chan);
      repeat_stop = DMA_STOP(chan);
      if (ret >= 0 && repeat_stop < 0)
        {
          ret = repeat_stop;
        }
    }

  bl616cl_dma_test_release_hold();
  bl616cl_dma_test_set_hold_before_enable(false);
  if (ret >= 0 && DMA_RESIDUAL(chan) != length)
    {
      ret = -EIO;
    }

  /* Use the required cache maintenance while checking alias visibility. */

  if (ret >= 0)
    {
      ret = start_and_check(chan, &event, (uintptr_t)destination_nc, source,
                            length, 1u << 4, true);
      up_invalidate_dcache(destination, destination + length);
      if (ret >= 0 && memcmp(g_source, g_destination, length) != 0)
        {
          ret = -EIO;
        }
    }

  printf("  pre-enable residual=%lu busy=%d/%d repeat-stop=%d ret=%d "
         "aliases=%p/%p\n",
         (unsigned long)DMA_RESIDUAL(chan), config_busy, start_busy,
         repeat_stop, ret, source_nc, destination_nc);
  print_irq_status("DMA-006");
  DMA_PUT_CHAN(dev, chan);
  return ret;
}

static int run_put_reuse_case(FAR struct dma_dev_s *dev)
{
  FAR struct dma_chan_s *channels[DMA_CHANNEL_COUNT];
  FAR struct dma_chan_s *owned;
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

static int run_callback_drain_case(FAR struct dma_dev_s *dev)
{
  FAR struct dma_chan_s *chan = DMA_GET_CHAN(dev, 7);
  FAR struct dma_chan_s *reused;
  struct dma_event_s event;
  struct dma_inject_context_s inject;
  struct dma_put_context_s put;
  pthread_t inject_thread;
  pthread_t put_thread;
  sem_t callback_entered;
  sem_t callback_release;
  bool inject_created = false;
  bool put_created = false;
  int thread_ret;
  int ret;

  printf("[DMA-008] task put waits for in-flight callback drain\n");
  memset(&event, 0, sizeof(event));
  memset(&inject, 0, sizeof(inject));
  memset(&put, 0, sizeof(put));
  if (sem_init(&callback_entered, 0, 0) < 0)
    {
      DMA_PUT_CHAN(dev, chan);
      return -errno;
    }

  if (sem_init(&callback_release, 0, 0) < 0)
    {
      ret = -errno;
      sem_destroy(&callback_entered);
      DMA_PUT_CHAN(dev, chan);
      return ret;
    }

  event.block_callback = true;
  event.callback_entered = &callback_entered;
  event.callback_release = &callback_release;
  ret = configure_mem2mem(chan, 1, 1, 1);
  bl616cl_dma_test_set_hold_before_enable(true);
  if (ret >= 0)
    {
      ret = DMA_START(chan, dma_callback, &event, (uintptr_t)g_destination,
                      (uintptr_t)g_source, DMA_SMALL_BYTES);
    }

  inject.tc_status = 1u << 7;
  if (ret >= 0)
    {
      thread_ret = pthread_create(&inject_thread, NULL, dma_inject_thread,
                                  &inject);
      if (thread_ret != 0)
        {
          ret = -thread_ret;
        }
      else
        {
          inject_created = true;
        }
    }

  if (ret >= 0)
    {
      ret = wait_sem(&callback_entered);
      if (ret >= 0)
        {
          put.dev = dev;
          put.chan = chan;
          thread_ret = pthread_create(&put_thread, NULL, dma_put_thread,
                                      &put);
          if (thread_ret != 0)
            {
              ret = -thread_ret;
            }
          else
            {
              put_created = true;
            }
        }
    }

  if (ret >= 0)
    {
      ret = wait_for_flag(&put.started);
      usleep(20000);
      if (ret >= 0 && put.returned)
        {
          ret = -EIO;
        }
    }

  sem_post(&callback_release);
  if (inject_created)
    {
      pthread_join(inject_thread, NULL);
    }

  if (put_created)
    {
      pthread_join(put_thread, NULL);
    }

  bl616cl_dma_test_set_hold_before_enable(false);
  sem_destroy(&callback_release);
  sem_destroy(&callback_entered);
  if (ret >= 0 && (!put.returned || event.callbacks != 1 ||
                   event.result != DMA_SMALL_BYTES))
    {
      ret = -EIO;
    }

  if (!put.returned)
    {
      DMA_PUT_CHAN(dev, chan);
    }

  reused = DMA_GET_CHAN(dev, 7);
  if (ret >= 0 && reused != chan)
    {
      ret = -EIO;
    }

  DMA_PUT_CHAN(dev, reused);
  printf("  callback=%u put-returned=%u reused=%p ret=%d\n",
         event.callbacks, put.returned, reused, ret);
  return ret;
}

static int run_case(FAR const struct dma_case_s *test,
                    FAR struct dma_dev_s *dev)
{
  int ret = test->run(dev);

  printf("[%s] %s ret=%d\n", test->name,
         ret == OK ? "PASS" : "FAIL", ret);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  static const struct dma_case_s tests[] =
  {
    { "DMA-001", run_width_step_case },
    { "DMA-002", run_boundary_case },
    { "DMA-003", run_invalid_atomic_case },
    { "DMA-004", run_parallel_case },
    { "DMA-005", run_callback_and_injection_case },
    { "DMA-006", run_hold_and_cache_case },
    { "DMA-007", run_put_reuse_case },
    { "DMA-008", run_callback_drain_case },
  };

  FAR struct dma_dev_s *dev;
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
