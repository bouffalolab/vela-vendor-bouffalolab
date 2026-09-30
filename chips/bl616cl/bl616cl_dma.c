/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_dma.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
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

/* Keep the generic weak hook declaration from weakening this
 * implementation.
 */

#define riscv_dma_initialize \
  bl616cl_dma_weak_initialize
#include "riscv_internal.h"
#undef riscv_dma_initialize

#include "bl616cl_lhal.h"
#include "bflb_clock.h"
#include "bflb_dma.h"
#include "bflb_name.h"
#include "bflb_peri.h"
#include <arch/chip/bl616cl_dma.h>
#include "hardware/dma_reg.h"
#include "hardware/bl616cl_memorymap.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* DMA base address comes from hardware/bl616cl_memorymap.h. */

#define BL616CL_DMA_CHANNEL_COUNT     8
#define BL616CL_DMA_TRANSFER_MAX \
  (DMA_TRANSFERSIZE_MASK >> DMA_TRANSFERSIZE_SHIFT)

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
  struct bflb_device_s *dev;
  sem_t available;
  sem_t callback_done;
  struct dma_config_s config;
  dma_callback_t callback;
  void *arg;
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

static struct dma_chan_s *bl616cl_dma_get_chan(
  struct dma_dev_s *dev, unsigned int ident);
static void bl616cl_dma_put_chan(struct dma_dev_s *dev,
                                 struct dma_chan_s *chan);
static int bl616cl_dma_config(struct dma_chan_s *chan,
                              const struct dma_config_s *config);
static int bl616cl_dma_start(struct dma_chan_s *chan,
                             dma_callback_t callback, void *arg,
                             uintptr_t dst, uintptr_t src, size_t len);
static int bl616cl_dma_start_cyclic(struct dma_chan_s *chan,
                                    dma_callback_t callback, void *arg,
                                    uintptr_t dst, uintptr_t src,
                                    size_t len, size_t period_len);
static int bl616cl_dma_stop(struct dma_chan_s *chan);
static int bl616cl_dma_pause(struct dma_chan_s *chan);
static int bl616cl_dma_resume(struct dma_chan_s *chan);
static size_t bl616cl_dma_residual(struct dma_chan_s *chan);

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

static const char * const g_bl616cl_dma_names[BL616CL_DMA_CHANNEL_COUNT] =
{
  BFLB_NAME_DMA0_CH0, BFLB_NAME_DMA0_CH1, BFLB_NAME_DMA0_CH2,
  BFLB_NAME_DMA0_CH3, BFLB_NAME_DMA0_CH4, BFLB_NAME_DMA0_CH5,
  BFLB_NAME_DMA0_CH6, BFLB_NAME_DMA0_CH7
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

/****************************************************************************
 * Name: bl616cl_dma_mask_stop_clear
 *
 * Description:
 *   Mask the channel interrupts, disable the channel and clear its
 *   terminal-count and error interrupt status.
 *
 * Input Parameters:
 *   channel - Channel state.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616cl_dma_mask_stop_clear(
  const struct bl616cl_dma_chan_s *channel)
{
  uintptr_t base = channel->dev->reg_base;
  uint32_t config = getreg32(base + DMA_CxCONFIG_OFFSET);
  uint32_t bit = 1u << channel->index;

  putreg32(config | DMA_ITC | DMA_IE, base + DMA_CxCONFIG_OFFSET);
  bflb_dma_channel_stop(channel->dev);
  putreg32(bit, DMA_BASE + DMA_INTTCCLEAR_OFFSET);
  putreg32(bit, DMA_BASE + DMA_INTERRCLR_OFFSET);
}

/****************************************************************************
 * Name: bl616cl_dma_pending_bytes
 *
 * Description:
 *   Read the remaining transfer size (in units) with
 *   DMA_CMD_GET_TRANSFER_PENDING and convert it to bytes, capped at the
 *   requested length.
 *
 * Input Parameters:
 *   channel - Channel state.
 *
 * Returned Value:
 *   The number of bytes not yet transferred.
 *
 ****************************************************************************/

static size_t bl616cl_dma_pending_bytes(
  const struct bl616cl_dma_chan_s *channel)
{
  size_t pending;

  pending = bflb_dma_feature_control(channel->dev,
                                     DMA_CMD_GET_TRANSFER_PENDING, 0);
  pending *= channel->width;

  return pending > channel->request_bytes ? channel->request_bytes : pending;
}

/****************************************************************************
 * Name: bl616cl_dma_step_valid
 *
 * Description:
 *   Check an address step: the hardware supports either a fixed address
 *   (0) or an increment equal to the transfer width.
 *
 * Input Parameters:
 *   step - Address step in bytes.
 *   width - Transfer width in bytes.
 *
 * Returned Value:
 *   True if the step is valid; false otherwise.
 *
 ****************************************************************************/

static bool bl616cl_dma_step_valid(int step, unsigned int width)
{
  return step == 0 || step == (int)width;
}

/****************************************************************************
 * Name: bl616cl_dma_width_encode
 *
 * Description:
 *   Convert a transfer width in bytes to the hardware width encoding (1,
 *   2 and 4 bytes are supported).
 *
 * Input Parameters:
 *   width - Transfer width in bytes.
 *   encoded - Location that receives the hardware encoding.
 *
 * Returned Value:
 *   Zero (OK) on success; -EINVAL if the width is not supported.
 *
 ****************************************************************************/

static int bl616cl_dma_width_encode(unsigned int width, uint8_t *encoded)
{
  switch (width)
    {
      case 1:
        *encoded = DMA_DATA_WIDTH_8BIT;
        return OK;

      case 2:
        *encoded = DMA_DATA_WIDTH_16BIT;
        return OK;

      case 4:
        *encoded = DMA_DATA_WIDTH_32BIT;
        return OK;

      default:
        return -EINVAL;
    }
}

/****************************************************************************
 * Name: bl616cl_dma_callback_done
 *
 * Description:
 *   Mark a completion callback as finished. If the channel is being
 *   released and this was the last callback in flight, wake the releasing
 *   thread.
 *
 * Input Parameters:
 *   channel - Channel state.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616cl_dma_callback_done(
  struct bl616cl_dma_chan_s *channel)
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

/****************************************************************************
 * Name: bl616cl_dma_process_irq
 *
 * Description:
 *   Handle terminal-count and error status bits. For each running channel
 *   it records the residual, updates the state, stops the channel and
 *   invokes the client callback with the byte count or -EIO. Also used by
 *   the test injection path.
 *
 * Input Parameters:
 *   tc_status - Terminal-count status bit mask, one bit per channel.
 *   error_status - Error status bit mask, one bit per channel.
 *   hardware_irq - True if called from the hardware interrupt; false for
 *     test injection (only used for test statistics).
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

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
      struct bl616cl_dma_chan_s *channel =
        &g_bl616cl_dma_channels[index];
      dma_callback_t callback = NULL;
      void *arg = NULL;
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

/****************************************************************************
 * Name: bl616cl_dma_interrupt
 *
 * Description:
 *   DMA0 interrupt handler. Read and clear the terminal-count and error
 *   status, then dispatch them to bl616cl_dma_process_irq().
 *
 * Input Parameters:
 *   irq - IRQ number (unused).
 *   context - Interrupt register context (unused).
 *   arg - Argument passed to irq_attach (unused).
 *
 * Returned Value:
 *   OK, always.
 *
 ****************************************************************************/

static int bl616cl_dma_interrupt(int irq, void *context, void *arg)
{
  uint8_t tc_status;
  uint8_t error_status;

  UNUSED(irq);
  UNUSED(context);
  UNUSED(arg);

  tc_status = getreg32(DMA_BASE + DMA_INTTCSTATUS_OFFSET);
  error_status = getreg32(DMA_BASE + DMA_INTERRORSTATUS_OFFSET);

  putreg32(tc_status, DMA_BASE + DMA_INTTCCLEAR_OFFSET);
  putreg32(error_status, DMA_BASE + DMA_INTERRCLR_OFFSET);
  bl616cl_dma_process_irq(tc_status, error_status, true);
  return OK;
}

/****************************************************************************
 * Name: bl616cl_dma_from_chan
 *
 * Description:
 *   Find the driver channel state that embeds the given dma_chan_s.
 *
 * Input Parameters:
 *   chan - DMA channel handle.
 *
 * Returned Value:
 *   The channel state; NULL if chan is not a channel of this driver.
 *
 ****************************************************************************/

static struct bl616cl_dma_chan_s *bl616cl_dma_from_chan(
  struct dma_chan_s *chan)
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

/****************************************************************************
 * Name: bl616cl_dma_in_callback_context
 *
 * Description:
 *   Report whether the caller runs in a completion callback context,
 *   where releasing a channel is not allowed. With
 *   CONFIG_BL616CL_DMA0_TEST, a thread running injected test callbacks
 *   also counts.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   True in interrupt (or injected callback) context; false otherwise.
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: bl616cl_dma_get_chan
 *
 * Description:
 *   Implement the dma_dev_s get_chan method. Wait for the requested
 *   channel to become available and take ownership of it, resetting its
 *   state. Cannot be called from interrupt context.
 *
 * Input Parameters:
 *   dev - DMA device (unused).
 *   ident - Channel index, 0 to BL616CL_DMA_CHANNEL_COUNT - 1.
 *
 * Returned Value:
 *   The channel handle on success; NULL if ident is invalid, called from
 *   interrupt context, the wait was interrupted or the channel is not
 *   free.
 *
 ****************************************************************************/

static struct dma_chan_s *bl616cl_dma_get_chan(
  struct dma_dev_s *dev, unsigned int ident)
{
  struct bl616cl_dma_chan_s *channel;
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

/****************************************************************************
 * Name: bl616cl_dma_put_chan
 *
 * Description:
 *   Implement the dma_dev_s put_chan method. Stop the channel, wait for
 *   callbacks in flight, clear its state and make it available again.
 *   Calling it from a callback context is rejected (DEBUGASSERT).
 *
 * Input Parameters:
 *   dev - DMA device (unused).
 *   chan - Channel handle to release.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616cl_dma_put_chan(struct dma_dev_s *dev,
                                 struct dma_chan_s *chan)
{
  struct bl616cl_dma_chan_s *channel = bl616cl_dma_from_chan(chan);
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
      nxsem_wait_uninterruptible(&channel->callback_done);
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

/****************************************************************************
 * Name: bl616cl_dma_config
 *
 * Description:
 *   Implement the dma_ops_s config method. Only memory-to-memory
 *   transfers with equal 1, 2 or 4 byte source and destination widths, no
 *   DRQ, and fixed or width-sized steps are accepted.
 *
 * Input Parameters:
 *   chan - DMA channel handle returned by get_chan.
 *   config - Channel configuration.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure: -EINVAL for a
 *   bad handle or unsupported configuration, -ENOTSUP if priority,
 *   timeout or option is set, -EBUSY if the channel is running, releasing
 *   or has callbacks in flight, -EPERM if the channel is not owned.
 *
 ****************************************************************************/

static int bl616cl_dma_config(struct dma_chan_s *chan,
                              const struct dma_config_s *config)
{
  struct bl616cl_dma_chan_s *channel = bl616cl_dma_from_chan(chan);
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
      return -ENOTSUP;
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

/****************************************************************************
 * Name: bl616cl_dma_start
 *
 * Description:
 *   Implement the dma_ops_s start method. Validate the alignment and
 *   length, program the source, destination and control registers, then
 *   enable the channel. The callback is invoked on completion with the
 *   byte count or a negated errno value.
 *
 * Input Parameters:
 *   chan - DMA channel handle returned by get_chan.
 *   callback - Completion callback; may be NULL.
 *   arg - Argument passed to the callback.
 *   dst - Destination address.
 *   src - Source address.
 *   len - Transfer length in bytes.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure: -EINVAL for a
 *   bad state, length or alignment, -EBUSY if a transfer or callback is
 *   in progress, -E2BIG if the length exceeds the hardware limit.
 *
 ****************************************************************************/

static int bl616cl_dma_start(struct dma_chan_s *chan,
                             dma_callback_t callback, void *arg,
                             uintptr_t dst, uintptr_t src, size_t len)
{
  struct bl616cl_dma_chan_s *channel = bl616cl_dma_from_chan(chan);
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

  base = channel->dev->reg_base;
  bl616cl_dma_mask_stop_clear(channel);
  control = units |
            ((uint32_t)DMA_BURST_INCR4 << DMA_SBSIZE_SHIFT) |
            ((uint32_t)DMA_BURST_INCR4 << DMA_DBSIZE_SHIFT) |
            ((uint32_t)encoded_width << DMA_SWIDTH_SHIFT) |
            ((uint32_t)encoded_width << DMA_DWIDTH_SHIFT) |
            DMA_I;
  if (channel->config.src_step != 0)
    {
      control |= DMA_SI;
    }

  if (channel->config.dst_step != 0)
    {
      control |= DMA_DI;
    }

  putreg32(src, base + DMA_CxSRCADDR_OFFSET);
  putreg32(dst, base + DMA_CxDSTADDR_OFFSET);
  putreg32(control, base + DMA_CxCONTROL_OFFSET);
  putreg32(1u << channel->index,
           DMA_BASE + DMA_INTTCCLEAR_OFFSET);
  putreg32(1u << channel->index,
           DMA_BASE + DMA_INTERRCLR_OFFSET);

  config = 0;
  putreg32(config, base + DMA_CxCONFIG_OFFSET);
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
      bflb_dma_channel_start(channel->dev);
    }

  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
  return OK;
}

/****************************************************************************
 * Name: bl616cl_dma_start_cyclic
 *
 * Description:
 *   Implement the dma_ops_s start_cyclic method. Cyclic transfers are not
 *   supported.
 *
 * Input Parameters:
 *   chan - DMA channel handle returned by get_chan.
 *   callback - Completion callback (unused).
 *   arg - Callback argument (unused).
 *   dst - Destination address (unused).
 *   src - Source address (unused).
 *   len - Total length (unused).
 *   period_len - Period length (unused).
 *
 * Returned Value:
 *   -ENOTSUP, always.
 *
 ****************************************************************************/

static int bl616cl_dma_start_cyclic(struct dma_chan_s *chan,
                                    dma_callback_t callback, void *arg,
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
  return -ENOTSUP;
}

/****************************************************************************
 * Name: bl616cl_dma_stop
 *
 * Description:
 *   Implement the dma_ops_s stop method. If a transfer is running, record
 *   the residual bytes, mark the channel stopped and disable it.
 *
 * Input Parameters:
 *   chan - DMA channel handle returned by get_chan.
 *
 * Returned Value:
 *   Zero (OK) on success; -EINVAL for a bad handle; -EPERM if the channel
 *   is not owned.
 *
 ****************************************************************************/

static int bl616cl_dma_stop(struct dma_chan_s *chan)
{
  struct bl616cl_dma_chan_s *channel = bl616cl_dma_from_chan(chan);
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

/****************************************************************************
 * Name: bl616cl_dma_pause
 *
 * Description:
 *   Implement the dma_ops_s pause method. Pausing is not supported.
 *
 * Input Parameters:
 *   chan - DMA channel handle returned by get_chan.
 *
 * Returned Value:
 *   -ENOTSUP, always.
 *
 ****************************************************************************/

static int bl616cl_dma_pause(struct dma_chan_s *chan)
{
  UNUSED(chan);
  return -ENOTSUP;
}

/****************************************************************************
 * Name: bl616cl_dma_resume
 *
 * Description:
 *   Implement the dma_ops_s resume method. Resuming is not supported.
 *
 * Input Parameters:
 *   chan - DMA channel handle returned by get_chan.
 *
 * Returned Value:
 *   -ENOTSUP, always.
 *
 ****************************************************************************/

static int bl616cl_dma_resume(struct dma_chan_s *chan)
{
  UNUSED(chan);
  return -ENOTSUP;
}

/****************************************************************************
 * Name: bl616cl_dma_residual
 *
 * Description:
 *   Implement the dma_ops_s residual method. For a running channel the
 *   value is refreshed from the hardware.
 *
 * Input Parameters:
 *   chan - DMA channel handle returned by get_chan.
 *
 * Returned Value:
 *   The number of bytes not yet transferred; 0 for an invalid handle.
 *
 ****************************************************************************/

static size_t bl616cl_dma_residual(struct dma_chan_s *chan)
{
  struct bl616cl_dma_chan_s *channel = bl616cl_dma_from_chan(chan);
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

/****************************************************************************
 * Name: bl616cl_dma0_device
 *
 * Description:
 *   Return the DMA0 controller device, for clients to call get_chan and
 *   put_chan on.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The DMA device if riscv_dma_initialize() succeeded; NULL otherwise.
 *
 ****************************************************************************/

struct dma_dev_s *bl616cl_dma0_device(void)
{
  return g_bl616cl_dma_initialized ? &g_bl616cl_dma_dev.dev : NULL;
}

/****************************************************************************
 * Name: riscv_dma_initialize
 *
 * Description:
 *   Initialize the DMA0 controller: enable its clock and the controller,
 *   set up the eight channels and their semaphores, and attach and enable
 *   the DMA0 interrupt. On failure, everything is undone and the
 *   controller stays unavailable.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

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

  config = getreg32(DMA_BASE + DMA_TOP_CONFIG_OFFSET);
  putreg32(config | DMA_E, DMA_BASE + DMA_TOP_CONFIG_OFFSET);

  for (index = 0; index < BL616CL_DMA_CHANNEL_COUNT; index++)
    {
      struct bl616cl_dma_chan_s *channel =
        &g_bl616cl_dma_channels[index];

      channel->chan.ops = &g_bl616cl_dma_ops;
      channel->dev = bflb_device_get_by_name(g_bl616cl_dma_names[index]);
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
      if (channel->dev == NULL)
        {
          goto errout;
        }

      bl616cl_dma_mask_stop_clear(channel);
    }

  ret = irq_attach(BL616CL_IRQ_NUM_DMA0_ALL, bl616cl_dma_interrupt, NULL);
  if (ret < 0)
    {
      goto errout;
    }

  up_enable_irq(BL616CL_IRQ_NUM_DMA0_ALL);
  g_bl616cl_dma_initialized = true;
  return;

errout:
  while (initialized > 0)
    {
      initialized--;
      nxsem_destroy(&g_bl616cl_dma_channels[initialized].callback_done);
      nxsem_destroy(&g_bl616cl_dma_channels[initialized].available);
    }

  config = getreg32(DMA_BASE + DMA_TOP_CONFIG_OFFSET);
  putreg32(config & ~DMA_E, DMA_BASE + DMA_TOP_CONFIG_OFFSET);
  bflb_peripheral_clock_control(BFLB_PERIPHERAL_DMA0, false);
}

#ifdef CONFIG_BL616CL_DMA0_TEST
/****************************************************************************
 * Name: bl616cl_dma_test_inject_irq
 *
 * Description:
 *   Test hook: feed software terminal-count and error status into the
 *   interrupt processing path, marking the caller as running in callback
 *   context.
 *
 * Input Parameters:
 *   tc_status - Terminal-count status bit mask.
 *   error_status - Error status bit mask.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: bl616cl_dma_test_set_hold_before_enable
 *
 * Description:
 *   Test hook: when set, later transfers are programmed but not enabled
 *   until bl616cl_dma_test_release_hold() is called.
 *
 * Input Parameters:
 *   hold - True to hold new transfers before enable.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_dma_test_set_hold_before_enable(bool hold)
{
  irqstate_t flags = spin_lock_irqsave(&g_bl616cl_dma_lock);

  g_bl616cl_dma_hold_before_enable = hold;
  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
}

/****************************************************************************
 * Name: bl616cl_dma_test_suppress_put_assert
 *
 * Description:
 *   Test hook: control whether put_chan called from a callback context
 *   triggers DEBUGASSERT. Rejected calls are counted either way.
 *
 * Input Parameters:
 *   suppress - True to suppress the assertion.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_dma_test_suppress_put_assert(bool suppress)
{
  irqstate_t flags = spin_lock_irqsave(&g_bl616cl_dma_lock);

  g_bl616cl_dma_test_suppress_put_assert = suppress;
  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
}

/****************************************************************************
 * Name: bl616cl_dma_test_release_hold
 *
 * Description:
 *   Test hook: enable all running channels that are being held before
 *   enable.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_dma_test_release_hold(void)
{
  unsigned int index;
  irqstate_t flags = spin_lock_irqsave(&g_bl616cl_dma_lock);

  for (index = 0; index < BL616CL_DMA_CHANNEL_COUNT; index++)
    {
      struct bl616cl_dma_chan_s *channel =
        &g_bl616cl_dma_channels[index];

      if (channel->state == BL616CL_DMA_RUNNING && channel->held)
        {
          channel->held = false;
          bflb_dma_channel_start(channel->dev);
        }
    }

  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
}

/****************************************************************************
 * Name: bl616cl_dma_test_get_status
 *
 * Description:
 *   Test hook: copy the interrupt and callback statistics collected by
 *   the driver.
 *
 * Input Parameters:
 *   status - Location that receives the statistics; nothing is done if
 *     NULL.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_dma_test_get_status(
  struct bl616cl_dma_test_status_s *status)
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
