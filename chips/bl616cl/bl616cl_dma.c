/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_dma.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/dma/dma.h>
#include <nuttx/irq.h>
#include <nuttx/sched.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>

#include <arch/irq.h>

enum
{
  BL616CL_DMA_ENOTSUP = ENOTSUP
};

/* Keep the generic weak hook declaration from weakening this
 * implementation.
 */

#define riscv_dma_initialize \
  bl616cl_dma_weak_initialize
#include "riscv_internal.h"
#undef riscv_dma_initialize

#include "bflb_clock.h"
#include "bflb_peri.h"
#include "bl616cl_dma.h"

#ifdef CONFIG_BL616CL_DMA0_TEST
#include "bl616cl_dma_test.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_DMA0_BASE             0x2000c000
#define BL616CL_DMA_CHANNEL_OFFSET    0x100
#define BL616CL_DMA_CHANNEL_COUNT     8
#define BL616CL_DMA_TRANSFER_MAX      4095

#define BL616CL_DMA_INTTCSTATUS       0x004
#define BL616CL_DMA_INTTCCLEAR        0x008
#define BL616CL_DMA_INTERRORSTATUS    0x00c
#define BL616CL_DMA_INTERRCLR         0x010
#define BL616CL_DMA_TOP_CONFIG        0x040

#define BL616CL_DMA_SRCADDR           0x000
#define BL616CL_DMA_DSTADDR           0x004
#define BL616CL_DMA_CONTROL           0x00c
#define BL616CL_DMA_CONFIG            0x010

#define BL616CL_DMA_TOP_ENABLE        (1 << 0)
#define BL616CL_DMA_CHANNEL_ENABLE    (1 << 0)
#define BL616CL_DMA_CONFIG_IE         (1 << 16)
#define BL616CL_DMA_CONFIG_ITC        (1 << 17)

#define BL616CL_DMA_CONTROL_SIZE_MASK 0x0fff
#define BL616CL_DMA_CONTROL_SBSIZE    12
#define BL616CL_DMA_CONTROL_DBSIZE    15
#define BL616CL_DMA_CONTROL_SWIDTH    18
#define BL616CL_DMA_CONTROL_DWIDTH    21
#define BL616CL_DMA_CONTROL_SI        (1 << 26)
#define BL616CL_DMA_CONTROL_DI        (1 << 27)
#define BL616CL_DMA_CONTROL_I         (1u << 31)

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum bl616cl_dma_state_e
{
  BL616CL_DMA_FREE = 0,
  BL616CL_DMA_OWNED,
  BL616CL_DMA_READY,
  BL616CL_DMA_RUNNING,
  BL616CL_DMA_COMPLETE,
  BL616CL_DMA_ERROR,
  BL616CL_DMA_STOPPED,
  BL616CL_DMA_RELEASING
};

struct bl616cl_dma_chan_s
{
  struct dma_chan_s chan;
  sem_t available;
  sem_t callback_done;
  struct dma_config_s config;
  dma_callback_t callback;
  FAR void *arg;
  size_t request_bytes;
  size_t residual_bytes;
  uint8_t index;
  uint8_t width;
  uint8_t callbacks_inflight;
  bool configured;
  bool held;
  enum bl616cl_dma_state_e state;
};

struct bl616cl_dma_dev_s
{
  struct dma_dev_s dev;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static FAR struct dma_chan_s *bl616cl_dma_get_chan(
  FAR struct dma_dev_s *dev, unsigned int ident);
static void bl616cl_dma_put_chan(FAR struct dma_dev_s *dev,
                                 FAR struct dma_chan_s *chan);
static int bl616cl_dma_config(FAR struct dma_chan_s *chan,
                              FAR const struct dma_config_s *config);
static int bl616cl_dma_start(FAR struct dma_chan_s *chan,
                             dma_callback_t callback, FAR void *arg,
                             uintptr_t dst, uintptr_t src, size_t len);
static int bl616cl_dma_start_cyclic(FAR struct dma_chan_s *chan,
                                    dma_callback_t callback, FAR void *arg,
                                    uintptr_t dst, uintptr_t src,
                                    size_t len, size_t period_len);
static int bl616cl_dma_stop(FAR struct dma_chan_s *chan);
static int bl616cl_dma_pause(FAR struct dma_chan_s *chan);
static int bl616cl_dma_resume(FAR struct dma_chan_s *chan);
static size_t bl616cl_dma_residual(FAR struct dma_chan_s *chan);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct dma_ops_s g_bl616cl_dma_ops =
{
  .config = bl616cl_dma_config,
  .start = bl616cl_dma_start,
  .start_cyclic = bl616cl_dma_start_cyclic,
  .stop = bl616cl_dma_stop,
  .pause = bl616cl_dma_pause,
  .resume = bl616cl_dma_resume,
  .residual = bl616cl_dma_residual,
};

static struct bl616cl_dma_dev_s g_bl616cl_dma_dev =
{
  .dev =
  {
    .get_chan = bl616cl_dma_get_chan,
    .put_chan = bl616cl_dma_put_chan,
  },
};

static struct bl616cl_dma_chan_s
  g_bl616cl_dma_channels[BL616CL_DMA_CHANNEL_COUNT];
static spinlock_t g_bl616cl_dma_lock = SP_UNLOCKED;
static bool g_bl616cl_dma_initialized;

#ifdef CONFIG_BL616CL_DMA0_TEST
static bool g_bl616cl_dma_hold_before_enable;
static bool g_bl616cl_dma_test_suppress_put_assert;
static pid_t g_bl616cl_dma_test_callback_tid = -1;
static struct bl616cl_dma_test_status_s g_bl616cl_dma_test_status;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uintptr_t bl616cl_dma_channel_base(
  FAR const struct bl616cl_dma_chan_s *channel)
{
  return BL616CL_DMA0_BASE +
         ((uintptr_t)channel->index + 1) * BL616CL_DMA_CHANNEL_OFFSET;
}

static void bl616cl_dma_mask_stop_clear(
  FAR const struct bl616cl_dma_chan_s *channel)
{
  uintptr_t base = bl616cl_dma_channel_base(channel);
  uint32_t config = getreg32(base + BL616CL_DMA_CONFIG);
  uint32_t bit = 1u << channel->index;

  config |= BL616CL_DMA_CONFIG_ITC | BL616CL_DMA_CONFIG_IE;
  config &= ~BL616CL_DMA_CHANNEL_ENABLE;
  putreg32(config, base + BL616CL_DMA_CONFIG);
  putreg32(bit, BL616CL_DMA0_BASE + BL616CL_DMA_INTTCCLEAR);
  putreg32(bit, BL616CL_DMA0_BASE + BL616CL_DMA_INTERRCLR);
}

static size_t bl616cl_dma_pending_bytes(
  FAR const struct bl616cl_dma_chan_s *channel)
{
  uintptr_t base = bl616cl_dma_channel_base(channel);
  size_t pending;

  pending = getreg32(base + BL616CL_DMA_CONTROL) &
            BL616CL_DMA_CONTROL_SIZE_MASK;
  pending *= channel->width;

  return pending > channel->request_bytes ? channel->request_bytes : pending;
}

static bool bl616cl_dma_step_valid(int step, unsigned int width)
{
  return step == 0 || step == (int)width;
}

static int bl616cl_dma_width_encode(unsigned int width, FAR uint8_t *encoded)
{
  switch (width)
    {
      case 1:
        *encoded = 0;
        return OK;

      case 2:
        *encoded = 1;
        return OK;

      case 4:
        *encoded = 2;
        return OK;

      default:
        return -EINVAL;
    }
}

static void bl616cl_dma_callback_done(
  FAR struct bl616cl_dma_chan_s *channel)
{
  irqstate_t flags = spin_lock_irqsave(&g_bl616cl_dma_lock);

  DEBUGASSERT(channel->callbacks_inflight > 0);
  channel->callbacks_inflight--;
  if (channel->state == BL616CL_DMA_RELEASING &&
      channel->callbacks_inflight == 0)
    {
      nxsem_post(&channel->callback_done);
    }

  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
}

static void bl616cl_dma_process_irq(uint8_t tc_status, uint8_t error_status,
                                    bool hardware_irq)
{
  uint8_t index;

#ifdef CONFIG_BL616CL_DMA0_TEST
  irqstate_t test_flags = spin_lock_irqsave(&g_bl616cl_dma_lock);

  g_bl616cl_dma_test_status.tc_status = tc_status;
  g_bl616cl_dma_test_status.error_status = error_status;
  if (hardware_irq)
    {
      g_bl616cl_dma_test_status.tc_clear_status = tc_status;
      g_bl616cl_dma_test_status.error_clear_status = error_status;
      g_bl616cl_dma_test_status.irq_count++;
    }
  else
    {
      g_bl616cl_dma_test_status.software_injection_count++;
    }

  spin_unlock_irqrestore(&g_bl616cl_dma_lock, test_flags);
#endif

  for (index = 0; index < BL616CL_DMA_CHANNEL_COUNT; index++)
    {
      FAR struct bl616cl_dma_chan_s *channel =
        &g_bl616cl_dma_channels[index];
      dma_callback_t callback = NULL;
      FAR void *arg = NULL;
      ssize_t result = 0;
      irqstate_t flags;

      if ((tc_status & (1u << index)) == 0 &&
          (error_status & (1u << index)) == 0)
        {
          continue;
        }

      flags = spin_lock_irqsave(&g_bl616cl_dma_lock);
      if (channel->state == BL616CL_DMA_RUNNING)
        {
          bool error = (error_status & (1u << index)) != 0;

          channel->residual_bytes = error ?
                                      bl616cl_dma_pending_bytes(channel) :
                                      0;
          channel->held = false;
          channel->state = error ? BL616CL_DMA_ERROR : BL616CL_DMA_COMPLETE;
          bl616cl_dma_mask_stop_clear(channel);

          callback = channel->callback;
          arg = channel->arg;
          result = error ? -EIO : (ssize_t)channel->request_bytes;
          if (callback != NULL)
            {
              channel->callbacks_inflight++;
            }
        }

      spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);

      if (callback != NULL)
        {
#ifdef CONFIG_BL616CL_DMA0_TEST
          flags = spin_lock_irqsave(&g_bl616cl_dma_lock);
          g_bl616cl_dma_test_status.callback_count++;
          spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
#endif
          callback(&channel->chan, arg, result);
          bl616cl_dma_callback_done(channel);
        }
    }
}

static int bl616cl_dma_interrupt(int irq, FAR void *context, FAR void *arg)
{
  uint8_t tc_status;
  uint8_t error_status;

  UNUSED(irq);
  UNUSED(context);
  UNUSED(arg);

  tc_status = getreg32(BL616CL_DMA0_BASE + BL616CL_DMA_INTTCSTATUS);
  error_status = getreg32(BL616CL_DMA0_BASE + BL616CL_DMA_INTERRORSTATUS);

  putreg32(tc_status, BL616CL_DMA0_BASE + BL616CL_DMA_INTTCCLEAR);
  putreg32(error_status, BL616CL_DMA0_BASE + BL616CL_DMA_INTERRCLR);
  bl616cl_dma_process_irq(tc_status, error_status, true);
  return OK;
}

static FAR struct bl616cl_dma_chan_s *bl616cl_dma_from_chan(
  FAR struct dma_chan_s *chan)
{
  unsigned int index;

  for (index = 0; index < BL616CL_DMA_CHANNEL_COUNT; index++)
    {
      if (chan == &g_bl616cl_dma_channels[index].chan)
        {
          return &g_bl616cl_dma_channels[index];
        }
    }

  return NULL;
}

static bool bl616cl_dma_in_callback_context(void)
{
#ifdef CONFIG_BL616CL_DMA0_TEST
  pid_t callback_tid;
  irqstate_t flags;

  if (up_interrupt_context())
    {
      return true;
    }

  flags = spin_lock_irqsave(&g_bl616cl_dma_lock);
  callback_tid = g_bl616cl_dma_test_callback_tid;
  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
  return callback_tid == nxsched_gettid();
#else
  return up_interrupt_context();
#endif
}

static FAR struct dma_chan_s *bl616cl_dma_get_chan(
  FAR struct dma_dev_s *dev, unsigned int ident)
{
  FAR struct bl616cl_dma_chan_s *channel;
  irqstate_t flags;

  UNUSED(dev);
  if (ident >= BL616CL_DMA_CHANNEL_COUNT || up_interrupt_context())
    {
      DEBUGASSERT(ident < BL616CL_DMA_CHANNEL_COUNT);
      return NULL;
    }

  channel = &g_bl616cl_dma_channels[ident];
  if (nxsem_wait_uninterruptible(&channel->available) < 0)
    {
      return NULL;
    }

  flags = spin_lock_irqsave(&g_bl616cl_dma_lock);
  if (channel->state != BL616CL_DMA_FREE)
    {
      spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
      nxsem_post(&channel->available);
      return NULL;
    }

  channel->state = BL616CL_DMA_OWNED;
  channel->configured = false;
  channel->held = false;
  channel->callback = NULL;
  channel->arg = NULL;
  channel->request_bytes = 0;
  channel->residual_bytes = 0;
  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
  return &channel->chan;
}

static void bl616cl_dma_put_chan(FAR struct dma_dev_s *dev,
                                 FAR struct dma_chan_s *chan)
{
  FAR struct bl616cl_dma_chan_s *channel = bl616cl_dma_from_chan(chan);
  bool wait_for_callback = false;
  irqstate_t flags;

  UNUSED(dev);
  if (channel == NULL)
    {
      return;
    }

  if (bl616cl_dma_in_callback_context())
    {
#ifdef CONFIG_BL616CL_DMA0_TEST
      bool suppress_assert;

      flags = spin_lock_irqsave(&g_bl616cl_dma_lock);
      g_bl616cl_dma_test_status.rejected_puts++;
      suppress_assert = g_bl616cl_dma_test_suppress_put_assert;
      spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
      if (!suppress_assert)
        {
          DEBUGASSERT(false);
        }
#else
      DEBUGASSERT(false);
#endif
      return;
    }

  flags = spin_lock_irqsave(&g_bl616cl_dma_lock);
  if (channel->state == BL616CL_DMA_FREE ||
      channel->state == BL616CL_DMA_RELEASING)
    {
      spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
      return;
    }

  channel->state = BL616CL_DMA_RELEASING;
  bl616cl_dma_mask_stop_clear(channel);
  wait_for_callback = channel->callbacks_inflight != 0;
  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);

  if (wait_for_callback)
    {
      (void)nxsem_wait_uninterruptible(&channel->callback_done);
    }

  flags = spin_lock_irqsave(&g_bl616cl_dma_lock);
  DEBUGASSERT(channel->callbacks_inflight == 0);
  memset(&channel->config, 0, sizeof(channel->config));
  channel->configured = false;
  channel->held = false;
  channel->callback = NULL;
  channel->arg = NULL;
  channel->request_bytes = 0;
  channel->residual_bytes = 0;
  channel->state = BL616CL_DMA_FREE;
  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);

  nxsem_post(&channel->available);
}

static int bl616cl_dma_config(FAR struct dma_chan_s *chan,
                              FAR const struct dma_config_s *config)
{
  FAR struct bl616cl_dma_chan_s *channel = bl616cl_dma_from_chan(chan);
  uint8_t width;
  irqstate_t flags;
  int ret;

  if (channel == NULL || config == NULL)
    {
      return -EINVAL;
    }

  if (config->direction != DMA_MEM_TO_MEM ||
      config->src_width != config->dst_width ||
      config->src_drq != 0 || config->dst_drq != 0 ||
      !bl616cl_dma_step_valid(config->src_step, config->src_width) ||
      !bl616cl_dma_step_valid(config->dst_step, config->dst_width))
    {
      return -EINVAL;
    }

  if (config->priority != 0 || config->timeout != 0 || config->option != 0)
    {
      return -BL616CL_DMA_ENOTSUP;
    }

  ret = bl616cl_dma_width_encode(config->src_width, &width);
  if (ret < 0)
    {
      return ret;
    }

  flags = spin_lock_irqsave(&g_bl616cl_dma_lock);
  if (channel->state == BL616CL_DMA_RUNNING ||
      channel->state == BL616CL_DMA_RELEASING ||
      channel->callbacks_inflight != 0)
    {
      spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
      return -EBUSY;
    }

  if (channel->state == BL616CL_DMA_FREE)
    {
      spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
      return -EPERM;
    }

  channel->config = *config;
  channel->width = config->src_width;
  channel->configured = true;
  channel->state = BL616CL_DMA_READY;
  channel->request_bytes = 0;
  channel->residual_bytes = 0;
  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
  return OK;
}

static int bl616cl_dma_start(FAR struct dma_chan_s *chan,
                             dma_callback_t callback, FAR void *arg,
                             uintptr_t dst, uintptr_t src, size_t len)
{
  FAR struct bl616cl_dma_chan_s *channel = bl616cl_dma_from_chan(chan);
  uintptr_t base;
  uint32_t control;
  uint32_t config;
  uint32_t units;
  uint8_t width;
  uint8_t encoded_width;
  irqstate_t flags;
  int ret;

  if (channel == NULL)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&g_bl616cl_dma_lock);
  if (!channel->configured ||
      (channel->state != BL616CL_DMA_READY &&
       channel->state != BL616CL_DMA_COMPLETE &&
       channel->state != BL616CL_DMA_ERROR &&
       channel->state != BL616CL_DMA_STOPPED) ||
      channel->callbacks_inflight != 0)
    {
      spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
      return channel->state == BL616CL_DMA_RUNNING ||
                 channel->callbacks_inflight != 0 ?
               -EBUSY :
               -EINVAL;
    }

  width = channel->width;
  ret = bl616cl_dma_width_encode(width, &encoded_width);
  if (ret < 0 || len == 0 || len > SSIZE_MAX || len % width != 0 ||
      src % width != 0 || dst % width != 0 ||
      src > UINTPTR_MAX - len || dst > UINTPTR_MAX - len)
    {
      spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
      return ret < 0 ? ret : -EINVAL;
    }

  units = len / width;
  if (units > BL616CL_DMA_TRANSFER_MAX)
    {
      spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
      return -E2BIG;
    }

  base = bl616cl_dma_channel_base(channel);
  bl616cl_dma_mask_stop_clear(channel);
  control = units |
            (1u << BL616CL_DMA_CONTROL_SBSIZE) |
            (1u << BL616CL_DMA_CONTROL_DBSIZE) |
            ((uint32_t)encoded_width << BL616CL_DMA_CONTROL_SWIDTH) |
            ((uint32_t)encoded_width << BL616CL_DMA_CONTROL_DWIDTH) |
            BL616CL_DMA_CONTROL_I;
  if (channel->config.src_step != 0)
    {
      control |= BL616CL_DMA_CONTROL_SI;
    }

  if (channel->config.dst_step != 0)
    {
      control |= BL616CL_DMA_CONTROL_DI;
    }

  putreg32(src, base + BL616CL_DMA_SRCADDR);
  putreg32(dst, base + BL616CL_DMA_DSTADDR);
  putreg32(control, base + BL616CL_DMA_CONTROL);
  putreg32(1u << channel->index,
           BL616CL_DMA0_BASE + BL616CL_DMA_INTTCCLEAR);
  putreg32(1u << channel->index,
           BL616CL_DMA0_BASE + BL616CL_DMA_INTERRCLR);

  config = 0;
  putreg32(config, base + BL616CL_DMA_CONFIG);
  channel->callback = callback;
  channel->arg = arg;
  channel->request_bytes = len;
  channel->residual_bytes = len;
  channel->state = BL616CL_DMA_RUNNING;

#ifdef CONFIG_BL616CL_DMA0_TEST
  channel->held = g_bl616cl_dma_hold_before_enable;
#else
  channel->held = false;
#endif

  if (!channel->held)
    {
      putreg32(config | BL616CL_DMA_CHANNEL_ENABLE,
               base + BL616CL_DMA_CONFIG);
    }

  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
  return OK;
}

static int bl616cl_dma_start_cyclic(FAR struct dma_chan_s *chan,
                                    dma_callback_t callback, FAR void *arg,
                                    uintptr_t dst, uintptr_t src,
                                    size_t len, size_t period_len)
{
  UNUSED(chan);
  UNUSED(callback);
  UNUSED(arg);
  UNUSED(dst);
  UNUSED(src);
  UNUSED(len);
  UNUSED(period_len);
  return -BL616CL_DMA_ENOTSUP;
}

static int bl616cl_dma_stop(FAR struct dma_chan_s *chan)
{
  FAR struct bl616cl_dma_chan_s *channel = bl616cl_dma_from_chan(chan);
  irqstate_t flags;

  if (channel == NULL)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&g_bl616cl_dma_lock);
  if (channel->state == BL616CL_DMA_FREE)
    {
      spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
      return -EPERM;
    }

  if (channel->state == BL616CL_DMA_RUNNING)
    {
      channel->residual_bytes = bl616cl_dma_pending_bytes(channel);
      channel->state = BL616CL_DMA_STOPPED;
      channel->held = false;
      bl616cl_dma_mask_stop_clear(channel);
    }

  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
  return OK;
}

static int bl616cl_dma_pause(FAR struct dma_chan_s *chan)
{
  UNUSED(chan);
  return -BL616CL_DMA_ENOTSUP;
}

static int bl616cl_dma_resume(FAR struct dma_chan_s *chan)
{
  UNUSED(chan);
  return -BL616CL_DMA_ENOTSUP;
}

static size_t bl616cl_dma_residual(FAR struct dma_chan_s *chan)
{
  FAR struct bl616cl_dma_chan_s *channel = bl616cl_dma_from_chan(chan);
  size_t residual;
  irqstate_t flags;

  if (channel == NULL)
    {
      return 0;
    }

  flags = spin_lock_irqsave(&g_bl616cl_dma_lock);
  if (channel->state == BL616CL_DMA_RUNNING)
    {
      channel->residual_bytes = bl616cl_dma_pending_bytes(channel);
    }

  residual = channel->residual_bytes;
  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
  return residual;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR struct dma_dev_s *bl616cl_dma0_device(void)
{
  return g_bl616cl_dma_initialized ? &g_bl616cl_dma_dev.dev : NULL;
}

void riscv_dma_initialize(void)
{
  unsigned int initialized = 0;
  unsigned int index;
  uint32_t config;
  int ret;

  if (g_bl616cl_dma_initialized)
    {
      return;
    }

  ret = bflb_peripheral_clock_control(BFLB_PERIPHERAL_DMA0, true);
  if (ret < 0)
    {
      return;
    }

  config = getreg32(BL616CL_DMA0_BASE + BL616CL_DMA_TOP_CONFIG);
  putreg32(config | BL616CL_DMA_TOP_ENABLE,
           BL616CL_DMA0_BASE + BL616CL_DMA_TOP_CONFIG);

  for (index = 0; index < BL616CL_DMA_CHANNEL_COUNT; index++)
    {
      FAR struct bl616cl_dma_chan_s *channel =
        &g_bl616cl_dma_channels[index];

      channel->chan.ops = &g_bl616cl_dma_ops;
      channel->index = index;
      channel->state = BL616CL_DMA_FREE;
      ret = nxsem_init(&channel->available, 0, 1);
      if (ret < 0)
        {
          goto errout;
        }

      ret = nxsem_init(&channel->callback_done, 0, 0);
      if (ret < 0)
        {
          nxsem_destroy(&channel->available);
          goto errout;
        }

      initialized++;
      bl616cl_dma_mask_stop_clear(channel);
    }

  ret = irq_attach(BL616CL_IRQ_DMA0_ALL, bl616cl_dma_interrupt, NULL);
  if (ret < 0)
    {
      goto errout;
    }

  up_enable_irq(BL616CL_IRQ_DMA0_ALL);
  g_bl616cl_dma_initialized = true;
  return;

errout:
  while (initialized > 0)
    {
      initialized--;
      nxsem_destroy(&g_bl616cl_dma_channels[initialized].callback_done);
      nxsem_destroy(&g_bl616cl_dma_channels[initialized].available);
    }

  config = getreg32(BL616CL_DMA0_BASE + BL616CL_DMA_TOP_CONFIG);
  putreg32(config & ~BL616CL_DMA_TOP_ENABLE,
           BL616CL_DMA0_BASE + BL616CL_DMA_TOP_CONFIG);
  (void)bflb_peripheral_clock_control(BFLB_PERIPHERAL_DMA0, false);
}

#ifdef CONFIG_BL616CL_DMA0_TEST
void bl616cl_dma_test_inject_irq(uint8_t tc_status, uint8_t error_status)
{
  irqstate_t flags = spin_lock_irqsave(&g_bl616cl_dma_lock);

  DEBUGASSERT(g_bl616cl_dma_test_callback_tid < 0);
  g_bl616cl_dma_test_callback_tid = nxsched_gettid();
  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
  bl616cl_dma_process_irq(tc_status, error_status, false);

  flags = spin_lock_irqsave(&g_bl616cl_dma_lock);
  g_bl616cl_dma_test_callback_tid = -1;
  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
}

void bl616cl_dma_test_set_hold_before_enable(bool hold)
{
  irqstate_t flags = spin_lock_irqsave(&g_bl616cl_dma_lock);

  g_bl616cl_dma_hold_before_enable = hold;
  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
}

void bl616cl_dma_test_suppress_put_assert(bool suppress)
{
  irqstate_t flags = spin_lock_irqsave(&g_bl616cl_dma_lock);

  g_bl616cl_dma_test_suppress_put_assert = suppress;
  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
}

void bl616cl_dma_test_release_hold(void)
{
  unsigned int index;
  irqstate_t flags = spin_lock_irqsave(&g_bl616cl_dma_lock);

  for (index = 0; index < BL616CL_DMA_CHANNEL_COUNT; index++)
    {
      FAR struct bl616cl_dma_chan_s *channel =
        &g_bl616cl_dma_channels[index];

      if (channel->state == BL616CL_DMA_RUNNING && channel->held)
        {
          uintptr_t base = bl616cl_dma_channel_base(channel);
          uint32_t config = getreg32(base + BL616CL_DMA_CONFIG);

          channel->held = false;
          putreg32(config | BL616CL_DMA_CHANNEL_ENABLE,
                   base + BL616CL_DMA_CONFIG);
        }
    }

  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
}

void bl616cl_dma_test_get_status(
  FAR struct bl616cl_dma_test_status_s *status)
{
  irqstate_t flags;

  if (status == NULL)
    {
      return;
    }

  flags = spin_lock_irqsave(&g_bl616cl_dma_lock);
  *status = g_bl616cl_dma_test_status;
  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
}
#endif
