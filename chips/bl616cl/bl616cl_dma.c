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

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <sys/param.h>

#include <nuttx/arch.h>
#include <nuttx/cache.h>
#include <nuttx/dma/dma.h>
#include <nuttx/irq.h>
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

#define BL616CL_DMA_CHANNEL_COUNT     BL616CL_DMA0_NCHANNELS
#define BL616CL_DMA_TRANSFER_MAX \
  (DMA_TRANSFERSIZE_MASK >> DMA_TRANSFERSIZE_SHIFT)
#define BL616CL_DMA_DRQ_MAX \
  (DMA_SRCPERIPHERAL_MASK >> DMA_SRCPERIPHERAL_SHIFT)

/* A transfer longer than BL616CL_DMA_TRANSFER_MAX units is split into
 * segments of BL616CL_DMA_SEGMENT_UNITS units (the last one takes the
 * rest), linked by LLI nodes. 4064 units keep every segment 32-byte
 * aligned, as LHAL does.
 */

#define BL616CL_DMA_SEGMENT_UNITS     4064

#ifdef CONFIG_BL616CL_DMA0_LLI_NODES
#  define BL616CL_DMA_LLI_NODES       CONFIG_BL616CL_DMA0_LLI_NODES
#else
#  define BL616CL_DMA_LLI_NODES       0
#endif

/* LLI counter: LLI nodes loaded since CxControl was last written, modulo
 * 1024. The LHAL mask is a negative int (0x3ff << 22), so it is shifted
 * as unsigned.
 */

#define BL616CL_DMA_COUNTER_MASK      ((uint32_t)DMA_LLICOUNTER_MASK)
#define BL616CL_DMA_COUNTER(config) \
  (((config) & BL616CL_DMA_COUNTER_MASK) >> DMA_LLICOUNTER_SHIFT)
#define BL616CL_DMA_COUNTER_MAX \
  (BL616CL_DMA_COUNTER_MASK >> DMA_LLICOUNTER_SHIFT)

/* Reads of a consistent channel position before using the last one */

#define BL616CL_DMA_POSITION_TRIES    16

/* Fail the build when the DMA0 channel assignment menu names a channel
 * that does not exist or gives one channel to two users. A Kconfig range
 * would drop a bad value silently instead.
 */

static_assert((BL616CL_DMA0_APP >> BL616CL_DMA_CHANNEL_COUNT) == 0,
              "DMA0 application channels name a channel above CH7");

#define BL616CL_DMA_ON(n, channel, name)   + ((channel) == (n))
#define BL616CL_DMA_USERS(n) \
  (((BL616CL_DMA0_APP >> (n)) & 1) \
   BL616CL_DMA0_ASSIGNMENTS(BL616CL_DMA_ON, n))
#define BL616CL_DMA_CHECK(n) \
  static_assert(BL616CL_DMA_USERS(n) <= 1, \
                "DMA0 CH" #n " is assigned more than once, see the DMA0 " \
                "channel assignment menu")

BL616CL_DMA_CHECK(0);
BL616CL_DMA_CHECK(1);
BL616CL_DMA_CHECK(2);
BL616CL_DMA_CHECK(3);
BL616CL_DMA_CHECK(4);
BL616CL_DMA_CHECK(5);
BL616CL_DMA_CHECK(6);
BL616CL_DMA_CHECK(7);

#define BL616CL_DMA_ENTRY(arg, channel, name)  [channel] = name,

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

/* LLI node, in the order of the channel registers it is loaded into */

struct bl616cl_dma_lli_s
{
  uint32_t src;
  uint32_t dst;
  uint32_t next;
  uint32_t control;
};

/* A transfer is split into periods (one, unless cyclic) of equal length,
 * and each period into segments of at most BL616CL_DMA_TRANSFER_MAX units.
 * Segment n of the transfer is segment n % segments of period
 * n / segments.
 */

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
  size_t period_bytes;          /* Bytes of one period */
  uintptr_t src;                /* Source address of segment 0 */
  uintptr_t dst;                /* Destination address of segment 0 */
  uint32_t control;             /* CxControl without size and I bit */
  uint16_t segments;            /* Segments per period */
  uint16_t periods;             /* Periods of a cyclic transfer, else 0 */
  uint16_t segment;             /* Segment the hardware is on */
  uint16_t finished;            /* Cyclic: segments finished since the last
                                 * period counted in ready */
  uint16_t ready;               /* Cyclic: periods finished, not reported */
  uint16_t period;              /* Cyclic: period after the last one
                                 * reported */
  uint16_t counter;             /* LLI counter at the last position read */
  uint16_t done;                /* Cyclic: segments finished at the last
                                 * position read, modulo 1024 */
  uint8_t index;
  uint8_t width;
  uint8_t callbacks_inflight;
  bool configured;
  enum bl616cl_dma_state_e state;
  void *holder;                 /* Return address of the owner's get_chan */
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

/* SDK driver of each channel in the DMA0 channel assignment menu */

static const char * const g_bl616cl_dma_users[BL616CL_DMA_CHANNEL_COUNT] =
{
  BL616CL_DMA0_ASSIGNMENTS(BL616CL_DMA_ENTRY, 0)
};

static struct bl616cl_dma_chan_s
  g_bl616cl_dma_channels[BL616CL_DMA_CHANNEL_COUNT];
static spinlock_t g_bl616cl_dma_lock = SP_UNLOCKED;
static bool g_bl616cl_dma_initialized;

#if BL616CL_DMA_LLI_NODES > 0
/* LLI nodes shared by all channels. The DMA only reads them, so they are
 * cleaned after each update; the array fills whole 32-byte cache lines so
 * that the clean writes back nothing else. owner is the channel index plus
 * one, or 0 for a free node.
 */

static struct bl616cl_dma_lli_s
  g_bl616cl_dma_lli[(BL616CL_DMA_LLI_NODES + 1) & ~1] aligned_data(32);
static uint8_t g_bl616cl_dma_lli_owner[BL616CL_DMA_LLI_NODES];
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
 * Name: bl616cl_dma_segments
 *
 * Description:
 *   Return the number of segments a period of the given length is split
 *   into.
 *
 * Input Parameters:
 *   units - Period length in units.
 *
 * Returned Value:
 *   The number of segments.
 *
 ****************************************************************************/

static size_t bl616cl_dma_segments(size_t units)
{
  if (units <= BL616CL_DMA_TRANSFER_MAX)
    {
      return 1;
    }

  return (units + BL616CL_DMA_SEGMENT_UNITS - 1) /
         BL616CL_DMA_SEGMENT_UNITS;
}

/****************************************************************************
 * Name: bl616cl_dma_units_through
 *
 * Description:
 *   Return the units of a period up to the end of one of its segments.
 *
 * Input Parameters:
 *   channel - Channel state.
 *   segment - Segment in the period.
 *
 * Returned Value:
 *   The number of units.
 *
 ****************************************************************************/

static size_t bl616cl_dma_units_through(
  const struct bl616cl_dma_chan_s *channel, unsigned int segment)
{
  if (segment + 1 < channel->segments)
    {
      return (size_t)(segment + 1) * BL616CL_DMA_SEGMENT_UNITS;
    }

  return channel->period_bytes / channel->width;
}

/****************************************************************************
 * Name: bl616cl_dma_segment
 *
 * Description:
 *   Compute the addresses and the control word of one segment. Only the
 *   last segment of each period raises the terminal-count interrupt.
 *
 * Input Parameters:
 *   channel - Channel state.
 *   segment - Segment of the transfer.
 *   lli - Location that receives the source, destination and control.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616cl_dma_segment(const struct bl616cl_dma_chan_s *channel,
                                unsigned int segment,
                                struct bl616cl_dma_lli_s *lli)
{
  unsigned int part = segment % channel->segments;
  size_t offset = (segment / channel->segments) * channel->period_bytes +
                  (size_t)part * BL616CL_DMA_SEGMENT_UNITS * channel->width;
  size_t units = bl616cl_dma_units_through(channel, part) -
                 (size_t)part * BL616CL_DMA_SEGMENT_UNITS;

  lli->src = channel->src + (channel->config.src_step != 0 ? offset : 0);
  lli->dst = channel->dst + (channel->config.dst_step != 0 ? offset : 0);
  lli->control = channel->control | units |
                 (part + 1 == channel->segments ? DMA_I : 0);
}

#if BL616CL_DMA_LLI_NODES > 0

/****************************************************************************
 * Name: bl616cl_dma_lli_free
 *
 * Description:
 *   Return the LLI nodes of a channel to the pool. Must be called with
 *   the lock held and the channel stopped.
 *
 * Input Parameters:
 *   channel - Channel state.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616cl_dma_lli_free(const struct bl616cl_dma_chan_s *channel)
{
  unsigned int i;

  for (i = 0; i < BL616CL_DMA_LLI_NODES; i++)
    {
      if (g_bl616cl_dma_lli_owner[i] == channel->index + 1)
        {
          g_bl616cl_dma_lli_owner[i] = 0;
        }
    }
}

/****************************************************************************
 * Name: bl616cl_dma_lli_available
 *
 * Description:
 *   Count the free LLI nodes. Must be called with the lock held.
 *
 * Returned Value:
 *   The number of free nodes.
 *
 ****************************************************************************/

static size_t bl616cl_dma_lli_available(void)
{
  size_t available = 0;
  unsigned int i;

  for (i = 0; i < BL616CL_DMA_LLI_NODES; i++)
    {
      if (g_bl616cl_dma_lli_owner[i] == 0)
        {
          available++;
        }
    }

  return available;
}

/****************************************************************************
 * Name: bl616cl_dma_lli_build
 *
 * Description:
 *   Take free LLI nodes for consecutive segments, link them in order and
 *   clean them for the DMA. Must be called with the lock held, after
 *   checking that enough nodes are free.
 *
 * Input Parameters:
 *   channel - Channel state.
 *   segment - First segment.
 *   count - Number of segments (at least one).
 *   ring - Link the last node back to the first one.
 *
 * Returned Value:
 *   The first node.
 *
 ****************************************************************************/

static struct bl616cl_dma_lli_s *bl616cl_dma_lli_build(
  const struct bl616cl_dma_chan_s *channel, unsigned int segment,
  size_t count, bool ring)
{
  struct bl616cl_dma_lli_s *first = NULL;
  struct bl616cl_dma_lli_s *last = NULL;
  unsigned int i;

  for (i = 0; i < BL616CL_DMA_LLI_NODES && count > 0; i++)
    {
      struct bl616cl_dma_lli_s *lli = &g_bl616cl_dma_lli[i];

      if (g_bl616cl_dma_lli_owner[i] != 0)
        {
          continue;
        }

      g_bl616cl_dma_lli_owner[i] = channel->index + 1;
      bl616cl_dma_segment(channel, segment++, lli);
      lli->next = 0;
      if (last != NULL)
        {
          last->next = (uintptr_t)lli;
        }
      else
        {
          first = lli;
        }

      last = lli;
      count--;
    }

  DEBUGASSERT(count == 0 && first != NULL);
  if (ring)
    {
      last->next = (uintptr_t)first;
    }

  up_clean_dcache((uintptr_t)g_bl616cl_dma_lli,
                  (uintptr_t)g_bl616cl_dma_lli + sizeof(g_bl616cl_dma_lli));
  return first;
}

#else
#  define bl616cl_dma_lli_free(channel)
#  define bl616cl_dma_lli_available() 0
#endif

/****************************************************************************
 * Name: bl616cl_dma_position
 *
 * Description:
 *   Read where the hardware is: update the segment it is on from the LLI
 *   counter and, for a cyclic transfer, count the periods it finished.
 *   Progress is taken from the position only, so terminal-count
 *   interrupts that come together lose nothing. Must be called with the
 *   lock held.
 *
 * Input Parameters:
 *   channel - Channel state.
 *
 * Returned Value:
 *   The units of the current segment not yet transferred; 0 when it is
 *   finished.
 *
 ****************************************************************************/

static uint32_t bl616cl_dma_position(struct bl616cl_dma_chan_s *channel)
{
  uintptr_t base = channel->dev->reg_base;
  uint32_t before;
  uint32_t control;
  uint32_t counter;
  uint32_t remaining;
  uint32_t loaded;
  unsigned int tries = 0;

  /* The registers are unstable while the next node is being loaded. Take
   * a control word read between two equal counter values and with
   * LLI_valid set.
   */

  do
    {
      before = getreg32(base + DMA_CxCONFIG_OFFSET);
      control = getreg32(base + DMA_CxCONTROL_OFFSET);
      counter = BL616CL_DMA_COUNTER(getreg32(base + DMA_CxCONFIG_OFFSET));
    }
  while ((BL616CL_DMA_COUNTER(before) != counter ||
          (control & DMA_LLI_VALID) == 0) &&
         ++tries < BL616CL_DMA_POSITION_TRIES);

  /* The count is right while fewer than 1024 nodes are loaded between two
   * reads: a one-shot transfer has at most BL616CL_DMA_LLI_NODES + 1
   * segments, and a cyclic transfer is read at every period interrupt;
   * missing 1024 loads there means the buffer was overrun anyway.
   */

  remaining = (control & DMA_TRANSFERSIZE_MASK) >> DMA_TRANSFERSIZE_SHIFT;
  loaded = (counter - channel->counter) & BL616CL_DMA_COUNTER_MAX;
  channel->counter = counter;
  if (channel->periods == 0)
    {
      channel->segment = MIN(channel->segment + loaded,
                             channel->segments - 1u);
    }
  else
    {
      unsigned int total = channel->periods * channel->segments;
      uint32_t done;

      /* A segment is finished once the next one is loaded, or already
       * when nothing of it remains: the hardware can sit there between
       * the terminal count and the next load. Counting both ways as one
       * mark avoids counting a segment twice.
       */

      done = (counter + (remaining == 0 ? 1 : 0)) &
             BL616CL_DMA_COUNTER_MAX;
      channel->segment = (channel->segment + loaded) % total;
      loaded = channel->finished +
               ((done - channel->done) & BL616CL_DMA_COUNTER_MAX);
      channel->done = done;
      channel->ready += loaded / channel->segments;
      channel->finished = loaded % channel->segments;
    }

  return remaining;
}

/****************************************************************************
 * Name: bl616cl_dma_pending_bytes
 *
 * Description:
 *   Compute the bytes not yet transferred from the hardware position: to
 *   the end of the transfer, or for a cyclic transfer to the end of the
 *   buffer. Must be called with the lock held.
 *
 * Input Parameters:
 *   channel - Channel state.
 *
 * Returned Value:
 *   The number of bytes not yet transferred.
 *
 ****************************************************************************/

static size_t bl616cl_dma_pending_bytes(struct bl616cl_dma_chan_s *channel)
{
  size_t units = bl616cl_dma_position(channel);
  unsigned int part = channel->segment % channel->segments;
  size_t pending;

  units += channel->period_bytes / channel->width -
           bl616cl_dma_units_through(channel, part);
  pending = units * channel->width;
  if (channel->periods > 0)
    {
      pending += (size_t)(channel->periods - 1 -
                          channel->segment / channel->segments) *
                 channel->period_bytes;
    }

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
 * Name: bl616cl_dma_direction_valid
 *
 * Description:
 *   Check the direction and the DMA request IDs. Memory-to-memory
 *   transfers take no request. A peripheral transfer takes the LHAL
 *   request number (DMA_REQUEST_*) on the peripheral side only; zero is a
 *   valid request there (UART0 RX).
 *
 * Input Parameters:
 *   config - Channel configuration.
 *
 * Returned Value:
 *   True if the direction and request IDs are valid; false otherwise.
 *
 ****************************************************************************/

static bool bl616cl_dma_direction_valid(const struct dma_config_s *config)
{
  switch (config->direction)
    {
      case DMA_MEM_TO_MEM:
        return config->src_drq == 0 && config->dst_drq == 0;

      case DMA_MEM_TO_DEV:
        return config->src_drq == 0 &&
               config->dst_drq <= BL616CL_DMA_DRQ_MAX;

      case DMA_DEV_TO_MEM:
        return config->dst_drq == 0 &&
               config->src_drq <= BL616CL_DMA_DRQ_MAX;

      default:
        return false;
    }
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
 * Name: bl616cl_dma_report_periods
 *
 * Description:
 *   Invoke the callback of a running cyclic transfer once for each
 *   finished period, in order, with the buffer offset at the end of the
 *   period. The lock is held only to take one period at a time, so the
 *   callback may stop or pause the channel.
 *
 * Input Parameters:
 *   channel - Channel state.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616cl_dma_report_periods(struct bl616cl_dma_chan_s *channel)
{
  for (; ; )
    {
      dma_callback_t callback;
      void *arg;
      ssize_t result;
      irqstate_t flags;

      flags = spin_lock_irqsave(&g_bl616cl_dma_lock);
      if (channel->state != BL616CL_DMA_RUNNING || channel->periods == 0 ||
          channel->ready == 0)
        {
          spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
          return;
        }

      channel->ready--;
      channel->period = (channel->period + 1) % channel->periods;
      result = (ssize_t)(channel->period * channel->period_bytes);
      callback = channel->callback;
      arg = channel->arg;
      if (callback != NULL)
        {
          channel->callbacks_inflight++;
        }

      spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);

      if (callback != NULL)
        {
          callback(&channel->chan, arg, result);
          bl616cl_dma_callback_done(channel);
        }
    }
}

/****************************************************************************
 * Name: bl616cl_dma_process_irq
 *
 * Description:
 *   Handle terminal-count and error status bits. An error stops the
 *   channel and reports -EIO with the residual. A terminal count finishes
 *   a one-shot transfer, which raises it only at its last segment, and
 *   reports the byte count; for a cyclic transfer it reports the finished
 *   periods.
 *
 * Input Parameters:
 *   tc_status - Terminal-count status bit mask, one bit per channel.
 *   error_status - Error status bit mask, one bit per channel.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616cl_dma_process_irq(uint8_t tc_status, uint8_t error_status)
{
  uint8_t index;

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
      if (channel->state == BL616CL_DMA_RUNNING &&
          ((error_status & (1u << index)) != 0 || channel->periods == 0))
        {
          bool error = (error_status & (1u << index)) != 0;

          bl616cl_dma_mask_stop_clear(channel);
          channel->residual_bytes = error ?
                                      bl616cl_dma_pending_bytes(channel) :
                                      0;
          channel->state = error ? BL616CL_DMA_ERROR : BL616CL_DMA_COMPLETE;
          bl616cl_dma_lli_free(channel);

          callback = channel->callback;
          arg = channel->arg;
          result = error ? -EIO : (ssize_t)channel->request_bytes;
          if (callback != NULL)
            {
              channel->callbacks_inflight++;
            }
        }
      else if (channel->state == BL616CL_DMA_RUNNING)
        {
          bl616cl_dma_position(channel);
        }

      spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);

      if (callback != NULL)
        {
          callback(&channel->chan, arg, result);
          bl616cl_dma_callback_done(channel);
        }

      bl616cl_dma_report_periods(channel);
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
  bl616cl_dma_process_irq(tc_status, error_status);
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
 * Name: bl616cl_dma_assignment
 *
 * Description:
 *   Return the user a channel is assigned to in the DMA0 channel assignment
 *   menu.
 *
 * Input Parameters:
 *   ident - Channel index.
 *
 * Returned Value:
 *   The SDK driver name, "application code", or "nothing".
 *
 ****************************************************************************/

static const char *bl616cl_dma_assignment(unsigned int ident)
{
  if (g_bl616cl_dma_users[ident] != NULL)
    {
      return g_bl616cl_dma_users[ident];
    }

  if ((BL616CL_DMA0_APP >> ident) & 1)
    {
      return "application code";
    }

  return "nothing";
}

/****************************************************************************
 * Name: bl616cl_dma_get_chan
 *
 * Description:
 *   Implement the dma_dev_s get_chan method. Wait for the requested
 *   channel to become available and take ownership of it, resetting its
 *   state. Cannot be called from interrupt context. Channels are not
 *   shared, so a channel that is already held is logged before the wait,
 *   which may never end.
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
  if (nxsem_trywait(&channel->available) < 0)
    {
      syslog(LOG_ERR, "DMA0 CH%u is held by code at %p (assigned to %s);"
             " waiting, DMA0 channels are not shared\n",
             ident, channel->holder, bl616cl_dma_assignment(ident));
      if (nxsem_wait_uninterruptible(&channel->available) < 0)
        {
          return NULL;
        }
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
  channel->callback = NULL;
  channel->arg = NULL;
  channel->request_bytes = 0;
  channel->residual_bytes = 0;
  channel->holder = __builtin_return_address(0);
  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
  return &channel->chan;
}

/****************************************************************************
 * Name: bl616cl_dma_put_chan
 *
 * Description:
 *   Implement the dma_dev_s put_chan method. Stop the channel, wait for
 *   callbacks in flight, clear its state and make it available again.
 *   Calling it from interrupt context, which includes the completion
 *   callbacks, is rejected (DEBUGASSERT).
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

  if (up_interrupt_context())
    {
      DEBUGASSERT(false);
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
  bl616cl_dma_lli_free(channel);
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
  channel->callback = NULL;
  channel->arg = NULL;
  channel->request_bytes = 0;
  channel->residual_bytes = 0;
  channel->holder = NULL;
  channel->state = BL616CL_DMA_FREE;
  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);

  nxsem_post(&channel->available);
}

/****************************************************************************
 * Name: bl616cl_dma_config
 *
 * Description:
 *   Implement the dma_ops_s config method. Memory-to-memory, memory-to-
 *   peripheral and peripheral-to-memory transfers with equal 1, 2 or 4
 *   byte source and destination widths and fixed or width-sized steps are
 *   accepted. See bl616cl_dma_direction_valid() for the request IDs.
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

  if (!bl616cl_dma_direction_valid(config) ||
      config->src_width != config->dst_width ||
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
 * Name: bl616cl_dma_submit
 *
 * Description:
 *   Validate and start a one-shot or cyclic transfer. A one-shot transfer
 *   of at most BL616CL_DMA_TRANSFER_MAX units uses no LLI node; a longer
 *   one takes a node for each segment after the first. A cyclic transfer
 *   takes a node for every segment, the last linked back to the first.
 *   Peripheral transfers use single (INCR1) bursts.
 *
 * Input Parameters:
 *   channel - Channel state.
 *   callback - Completion or period callback; may be NULL.
 *   arg - Argument passed to the callback.
 *   dst - Destination address.
 *   src - Source address.
 *   len - Transfer (or buffer) length in bytes.
 *   period_len - Period length in bytes; len for a one-shot transfer.
 *   cyclic - Whether the transfer is cyclic.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure: -EINVAL for a
 *   bad state, length or alignment, -EBUSY if a transfer is in progress,
 *   -E2BIG if the transfer needs more LLI nodes than there are, -ENOMEM if
 *   not enough of them are free.
 *
 ****************************************************************************/

static int bl616cl_dma_submit(struct bl616cl_dma_chan_s *channel,
                              dma_callback_t callback, void *arg,
                              uintptr_t dst, uintptr_t src, size_t len,
                              size_t period_len, bool cyclic)
{
  struct bl616cl_dma_lli_s first;
  uint32_t config;
  uint32_t burst;
  size_t segments;
  size_t periods;
  size_t nodes;
  uint8_t width;
  uint8_t encoded_width;
  irqstate_t flags;
  int ret;

  /* A completion callback runs in the DMA interrupt with the channel
   * already COMPLETE or ERROR, so it may start the next transfer here.
   * put_chan marks the channel RELEASING first, which is rejected below.
   */

  flags = spin_lock_irqsave(&g_bl616cl_dma_lock);
  if (!channel->configured ||
      (channel->state != BL616CL_DMA_READY &&
       channel->state != BL616CL_DMA_COMPLETE &&
       channel->state != BL616CL_DMA_ERROR &&
       channel->state != BL616CL_DMA_STOPPED))
    {
      spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
      return channel->state == BL616CL_DMA_RUNNING ? -EBUSY : -EINVAL;
    }

  width = channel->width;
  ret = bl616cl_dma_width_encode(width, &encoded_width);
  if (ret < 0 || len == 0 || len > SSIZE_MAX || len % width != 0 ||
      period_len == 0 || period_len % width != 0 || len % period_len != 0 ||
      src % width != 0 || dst % width != 0 ||
      src > UINTPTR_MAX - len || dst > UINTPTR_MAX - len)
    {
      spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
      return ret < 0 ? ret : -EINVAL;
    }

  segments = bl616cl_dma_segments(period_len / width);
  periods = len / period_len;
  nodes = cyclic ? periods * segments : segments - 1;
  if (nodes > BL616CL_DMA_LLI_NODES)
    {
      spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
      return -E2BIG;
    }

  if (nodes > bl616cl_dma_lli_available())
    {
      spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
      return -ENOMEM;
    }

  config = 0;
  burst = DMA_BURST_INCR4;
  if (channel->config.direction == DMA_MEM_TO_DEV)
    {
      config = ((uint32_t)channel->config.dst_drq <<
                DMA_DSTPERIPHERAL_SHIFT) |
               ((uint32_t)DMA_MEMORY_TO_PERIPH << DMA_FLOWCNTRL_SHIFT);
      burst = DMA_BURST_INCR1;
    }
  else if (channel->config.direction == DMA_DEV_TO_MEM)
    {
      config = ((uint32_t)channel->config.src_drq <<
                DMA_SRCPERIPHERAL_SHIFT) |
               ((uint32_t)DMA_PERIPH_TO_MEMORY << DMA_FLOWCNTRL_SHIFT);
      burst = DMA_BURST_INCR1;
    }

  channel->control = (burst << DMA_SBSIZE_SHIFT) |
                     (burst << DMA_DBSIZE_SHIFT) |
                     ((uint32_t)encoded_width << DMA_SWIDTH_SHIFT) |
                     ((uint32_t)encoded_width << DMA_DWIDTH_SHIFT);
  if (channel->config.src_step != 0)
    {
      channel->control |= DMA_SI;
    }

  if (channel->config.dst_step != 0)
    {
      channel->control |= DMA_DI;
    }

  channel->src = src;
  channel->dst = dst;
  channel->period_bytes = period_len;
  channel->segments = segments;
  channel->periods = cyclic ? periods : 0;
  channel->segment = 0;
  channel->finished = 0;
  channel->ready = 0;
  channel->period = 0;
  channel->counter = 0;
  channel->done = 0;

  /* Segment 0 goes to the channel registers. A one-shot transfer links
   * it to the nodes of the other segments; a cyclic one also has a node
   * for segment 0, which the last node links back to.
   */

  bl616cl_dma_mask_stop_clear(channel);
  bl616cl_dma_segment(channel, 0, &first);
  first.next = 0;
#if BL616CL_DMA_LLI_NODES > 0
  if (cyclic)
    {
      first.next = bl616cl_dma_lli_build(channel, 0, nodes, true)->next;
    }
  else if (nodes > 0)
    {
      first.next = (uintptr_t)bl616cl_dma_lli_build(channel, 1, nodes,
                                                    false);
    }
#endif

  putreg32(first.src, channel->dev->reg_base + DMA_CxSRCADDR_OFFSET);
  putreg32(first.dst, channel->dev->reg_base + DMA_CxDSTADDR_OFFSET);
  putreg32(first.next, channel->dev->reg_base + DMA_CxLLI_OFFSET);
  putreg32(first.control, channel->dev->reg_base + DMA_CxCONTROL_OFFSET);
  putreg32(1u << channel->index,
           DMA_BASE + DMA_INTTCCLEAR_OFFSET);
  putreg32(1u << channel->index,
           DMA_BASE + DMA_INTERRCLR_OFFSET);

  putreg32(config, channel->dev->reg_base + DMA_CxCONFIG_OFFSET);
  channel->callback = callback;
  channel->arg = arg;
  channel->request_bytes = len;
  channel->residual_bytes = len;
  channel->state = BL616CL_DMA_RUNNING;
  bflb_dma_channel_start(channel->dev);
  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
  return OK;
}

/****************************************************************************
 * Name: bl616cl_dma_start
 *
 * Description:
 *   Implement the dma_ops_s start method. A transfer longer than
 *   BL616CL_DMA_TRANSFER_MAX units is split into LLI segments and still
 *   completes with one callback. The callback is invoked on completion
 *   with the byte count or a negated errno value, and may start the next
 *   transfer on its own channel.
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
 *   Zero (OK) on success; a negated errno value on failure, see
 *   bl616cl_dma_submit().
 *
 ****************************************************************************/

static int bl616cl_dma_start(struct dma_chan_s *chan,
                             dma_callback_t callback, void *arg,
                             uintptr_t dst, uintptr_t src, size_t len)
{
  struct bl616cl_dma_chan_s *channel = bl616cl_dma_from_chan(chan);

  if (channel == NULL)
    {
      return -EINVAL;
    }

  return bl616cl_dma_submit(channel, callback, arg, dst, src, len, len,
                            false);
}

/****************************************************************************
 * Name: bl616cl_dma_start_cyclic
 *
 * Description:
 *   Implement the dma_ops_s start_cyclic method. The DMA goes round the
 *   buffer until stopped. The callback is invoked once per finished
 *   period, in order, with the buffer offset at the end of that period
 *   (0 after the last period), or once with a negated errno value on
 *   error.
 *
 * Input Parameters:
 *   chan - DMA channel handle returned by get_chan.
 *   callback - Period callback; may be NULL.
 *   arg - Argument passed to the callback.
 *   dst - Destination address.
 *   src - Source address.
 *   len - Buffer length in bytes, a multiple of period_len.
 *   period_len - Period length in bytes.
 *
 * Returned Value:
 *   Zero (OK) on success; -ENOTSUP without LLI nodes; another negated
 *   errno value on failure, see bl616cl_dma_submit().
 *
 ****************************************************************************/

static int bl616cl_dma_start_cyclic(struct dma_chan_s *chan,
                                    dma_callback_t callback, void *arg,
                                    uintptr_t dst, uintptr_t src,
                                    size_t len, size_t period_len)
{
  struct bl616cl_dma_chan_s *channel = bl616cl_dma_from_chan(chan);

  if (channel == NULL)
    {
      return -EINVAL;
    }

  if (BL616CL_DMA_LLI_NODES == 0)
    {
      return -ENOTSUP;
    }

  return bl616cl_dma_submit(channel, callback, arg, dst, src, len,
                            period_len, true);
}

/****************************************************************************
 * Name: bl616cl_dma_stop
 *
 * Description:
 *   Implement the dma_ops_s stop method. If a transfer is running, disable
 *   the channel, record the residual bytes, mark the channel stopped and
 *   return its LLI nodes. No callback is invoked.
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
      bl616cl_dma_mask_stop_clear(channel);
      channel->residual_bytes = bl616cl_dma_pending_bytes(channel);
      channel->state = BL616CL_DMA_STOPPED;
      bl616cl_dma_lli_free(channel);
    }

  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
  return OK;
}

/****************************************************************************
 * Name: bl616cl_dma_halt
 *
 * Description:
 *   Set or clear the halt bit of a channel running a cyclic transfer. With
 *   the bit set the channel ignores further DMA requests and stays where
 *   it is; clearing it continues from there. One-shot transfers are not
 *   paused: the hardware clears the enable bit in the same register when
 *   it finishes, and the read-modify-write could enable it again.
 *
 * Input Parameters:
 *   chan - DMA channel handle returned by get_chan.
 *   halt - Set (pause) or clear (resume) the halt bit.
 *
 * Returned Value:
 *   Zero (OK) on success; -EINVAL for a bad handle or no running transfer;
 *   -EPERM if the channel is not owned; -ENOTSUP for a one-shot transfer.
 *
 ****************************************************************************/

static int bl616cl_dma_halt(struct dma_chan_s *chan, bool halt)
{
  struct bl616cl_dma_chan_s *channel = bl616cl_dma_from_chan(chan);
  uintptr_t config;
  irqstate_t flags;
  int ret = OK;

  if (channel == NULL)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&g_bl616cl_dma_lock);
  if (channel->state == BL616CL_DMA_FREE)
    {
      ret = -EPERM;
    }
  else if (channel->state != BL616CL_DMA_RUNNING)
    {
      ret = -EINVAL;
    }
  else if (channel->periods == 0)
    {
      ret = -ENOTSUP;
    }
  else
    {
      config = channel->dev->reg_base + DMA_CxCONFIG_OFFSET;
      putreg32(halt ? getreg32(config) | DMA_H : getreg32(config) & ~DMA_H,
               config);
    }

  spin_unlock_irqrestore(&g_bl616cl_dma_lock, flags);
  return ret;
}

/****************************************************************************
 * Name: bl616cl_dma_pause
 *
 * Description:
 *   Implement the dma_ops_s pause method for cyclic transfers, see
 *   bl616cl_dma_halt(). No period callback is invoked while paused.
 *
 * Input Parameters:
 *   chan - DMA channel handle returned by get_chan.
 *
 * Returned Value:
 *   See bl616cl_dma_halt().
 *
 ****************************************************************************/

static int bl616cl_dma_pause(struct dma_chan_s *chan)
{
  return bl616cl_dma_halt(chan, true);
}

/****************************************************************************
 * Name: bl616cl_dma_resume
 *
 * Description:
 *   Implement the dma_ops_s resume method for cyclic transfers, see
 *   bl616cl_dma_halt().
 *
 * Input Parameters:
 *   chan - DMA channel handle returned by get_chan.
 *
 * Returned Value:
 *   See bl616cl_dma_halt().
 *
 ****************************************************************************/

static int bl616cl_dma_resume(struct dma_chan_s *chan)
{
  return bl616cl_dma_halt(chan, false);
}

/****************************************************************************
 * Name: bl616cl_dma_residual
 *
 * Description:
 *   Implement the dma_ops_s residual method. For a running channel the
 *   value is computed from the hardware position; for a cyclic transfer
 *   it counts to the end of the buffer, so the DMA is at offset len minus
 *   the residual.
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
