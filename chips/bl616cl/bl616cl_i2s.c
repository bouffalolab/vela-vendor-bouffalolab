/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_i2s.c
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
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <sys/param.h>

#include <nuttx/audio/audio.h>
#include <nuttx/audio/i2s.h>
#include <nuttx/cache.h>
#include <nuttx/compiler.h>
#include <nuttx/dma/dma.h>
#include <nuttx/mutex.h>
#include <nuttx/queue.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

#include <arch/chip/bl616cl_dma.h>

#include "bl616cl_sdk.h"
#include "bl616cl_glb.h"
#include "bl616cl_pm.h"
#include "bflb_clock.h"
#include "bflb_dma.h"
#include "bflb_gpio.h"
#include "bflb_i2s.h"
#include "bflb_name.h"
#include "bflb_peri.h"
#include "hardware/i2s_reg.h"
#include "bl616cl_i2s.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The I2S reference clock (MCLK) is WIFIPLL 320 MHz / (div + 1); the chip
 * has no audio PLL.
 */

#define BL616CL_I2S_PLL_FREQUENCY   320000000u
#define BL616CL_I2S_REFDIV_MAX      63u
#define BL616CL_I2S_DEFAULT_MCLK    12288000u
#define BL616CL_I2S_DEFAULT_RATE    48000u

/* Each frame has two 32-bit slots, so MCLK / LRCK is always a multiple of
 * 64, which matches the codec MCLK ratios (256, 384, 512, ...).
 */

#define BL616CL_I2S_FRAME_BITS      64u

/* DMA moves one 32-bit FIFO word per request; one transfer is limited to
 * 4095 words.
 */

#define BL616CL_I2S_DMA_WIDTH       4
#define BL616CL_I2S_DMA_CHUNK       (4095 * BL616CL_I2S_DMA_WIDTH)

/* Samples are 16 bits; LR merge packs one stereo frame into a FIFO word.
 * 32-bit samples are not supported: without LR merge the first RX FIFO
 * word of a stream is sometimes the right slot, which swaps the channels
 * of the whole stream.
 */

#define BL616CL_I2S_DATA_WIDTH      16
#define BL616CL_I2S_FRAME_BYTES     BL616CL_I2S_DMA_WIDTH

/* With the ring, each direction runs one cyclic DMA transfer over a fixed
 * buffer in non-cacheable RAM; a period of at most 4095 words takes one
 * LLI node.
 */

#ifdef CONFIG_BL616CL_I2S_DMA_RING
#  define BL616CL_I2S_PERIOD_BYTES  CONFIG_BL616CL_I2S_RING_PERIOD_BYTES
#  define BL616CL_I2S_RING_BYTES \
     (CONFIG_BL616CL_I2S_RING_PERIODS * BL616CL_I2S_PERIOD_BYTES)

#  if BL616CL_I2S_PERIOD_BYTES % BL616CL_I2S_FRAME_BYTES != 0
#    error "BL616CL_I2S_RING_PERIOD_BYTES must be a multiple of 4"
#  endif

#  if CONFIG_BL616CL_DMA0_LLI_NODES < 2 * CONFIG_BL616CL_I2S_RING_PERIODS
#    error "BL616CL_DMA0_LLI_NODES must be at least 2 x BL616CL_I2S_RING_PERIODS"
#  endif
#endif

#define BL616CL_I2S_GPIO_CFG \
  (GPIO_FUNC_I2S | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define BL616CL_I2S_MCLK_GPIO_CFG \
  (GPIO_FUNC_CLKOUT | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | \
   GPIO_DRV_0)

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* One queued audio buffer */

struct bl616cl_i2s_buffer_s
{
  sq_entry_t entry;
  struct ap_buffer_s *apb;
  i2s_callback_t callback;
  void *arg;
  uintptr_t addr;           /* Next byte to hand to the DMA or ring */
  size_t remaining;         /* Bytes not yet handed to the DMA or ring */
  size_t nbytes;            /* Bytes in the transfer */
#ifdef CONFIG_BL616CL_I2S_DMA_RING
  uint32_t end;             /* TX: stream position after the last byte */
#endif
  int result;
};

/* One direction: TX (memory to I2S) or RX (I2S to memory) */

struct bl616cl_i2s_transport_s
{
  struct dma_chan_s *chan;  /* Taken at initialization, never released */
  sq_queue_t pend;          /* Queued, not yet started */
#ifdef CONFIG_BL616CL_I2S_DMA_RING
  sq_queue_t flight;        /* TX: last buffer, in the ring, not played */
  uint8_t *ring;            /* Cyclic DMA buffer, non-cacheable */
  uint32_t written;         /* TX: stream bytes put into the ring */
  uint32_t played;          /* TX: stream bytes the DMA has finished */
  bool lapped;              /* Late period reported for this stream */
#else
  struct bl616cl_i2s_buffer_s *act; /* Being moved by the DMA */
#endif
  sq_queue_t done;          /* Finished, callback pending */
  dma_callback_t dma_callback;
  uintptr_t fifo;           /* I2S FIFO data register */
  unsigned int ident;       /* DMA0 channel */
  unsigned int drq;         /* DMA request */
  uint32_t enable;          /* I2S_ARG_CMD_TX or I2S_ARG_CMD_RX */
  bool tx;
  bool running;             /* Direction and its DMA request enabled */
};

struct bl616cl_i2s_s
{
  struct i2s_dev_s dev;     /* Must be first */
  struct bflb_device_s *i2s;
  struct dma_dev_s *dma;
  mutex_t lock;             /* Serializes transfers and configuration */
  sem_t bufsem;             /* Counts free buffer containers */
  spinlock_t spinlock;      /* Protects the queues from the DMA ISR */
  sq_queue_t freeq;
  struct bl616cl_i2s_transport_s tx;
  struct bl616cl_i2s_transport_s rx;
  struct work_s work;
  struct bl616cl_i2s_pins_s pins;
  uint32_t mclk;            /* Actual MCLK frequency */
  uint32_t samplerate;      /* Requested sample rate */
  bool initialized;
  struct bl616cl_i2s_buffer_s buffers[CONFIG_BL616CL_I2S_NBUFFERS];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static uint32_t bl616cl_i2s_samplerate(struct i2s_dev_s *dev,
                                       uint32_t rate);
static uint32_t bl616cl_i2s_datawidth(struct i2s_dev_s *dev, int bits);
static int bl616cl_i2s_receive(struct i2s_dev_s *dev,
                               struct ap_buffer_s *apb,
                               i2s_callback_t callback, void *arg,
                               uint32_t timeout);
static int bl616cl_i2s_send(struct i2s_dev_s *dev,
                            struct ap_buffer_s *apb,
                            i2s_callback_t callback, void *arg,
                            uint32_t timeout);
static uint32_t bl616cl_i2s_getmclkfrequency(struct i2s_dev_s *dev);
static uint32_t bl616cl_i2s_setmclkfrequency(struct i2s_dev_s *dev,
                                             uint32_t frequency);
static void bl616cl_i2s_txdma_callback(struct dma_chan_s *chan, void *arg,
                                       ssize_t len);
static void bl616cl_i2s_rxdma_callback(struct dma_chan_s *chan, void *arg,
                                       ssize_t len);
static void bl616cl_i2s_worker(void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct i2s_ops_s g_bl616cl_i2s_ops =
{
  .i2s_rxsamplerate = bl616cl_i2s_samplerate,
  .i2s_rxdatawidth = bl616cl_i2s_datawidth,
  .i2s_receive = bl616cl_i2s_receive,
  .i2s_txsamplerate = bl616cl_i2s_samplerate,
  .i2s_txdatawidth = bl616cl_i2s_datawidth,
  .i2s_send = bl616cl_i2s_send,
  .i2s_getmclkfrequency = bl616cl_i2s_getmclkfrequency,
  .i2s_setmclkfrequency = bl616cl_i2s_setmclkfrequency,
};

#ifdef CONFIG_BL616CL_I2S_DMA_RING
static uint8_t g_bl616cl_i2s_txring[BL616CL_I2S_RING_BYTES]
  locate_data(".nocache_noinit_ram") aligned_data(32);
static uint8_t g_bl616cl_i2s_rxring[BL616CL_I2S_RING_BYTES]
  locate_data(".nocache_noinit_ram") aligned_data(32);
#endif

static struct bl616cl_i2s_s g_bl616cl_i2s0 =
{
  .dev =
  {
    .ops = &g_bl616cl_i2s_ops,
  },
  .lock = NXMUTEX_INITIALIZER,
  .spinlock = SP_UNLOCKED,
  .tx =
  {
    .dma_callback = bl616cl_i2s_txdma_callback,
    .fifo = DMA_ADDR_I2S_TDR,
    .ident = CONFIG_BL616CL_I2S_TX_DMA_CHANNEL,
    .drq = DMA_REQUEST_I2S_TX,
    .enable = I2S_ARG_CMD_TX,
    .tx = true,
#ifdef CONFIG_BL616CL_I2S_DMA_RING
    .ring = g_bl616cl_i2s_txring,
#endif
  },
  .rx =
  {
    .dma_callback = bl616cl_i2s_rxdma_callback,
    .fifo = DMA_ADDR_I2S_RDR,
    .ident = CONFIG_BL616CL_I2S_RX_DMA_CHANNEL,
    .drq = DMA_REQUEST_I2S_RX,
    .enable = I2S_ARG_CMD_RX,
    .tx = false,
#ifdef CONFIG_BL616CL_I2S_DMA_RING
    .ring = g_bl616cl_i2s_rxring,
#endif
  },
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_i2s_bclk_divider
 *
 * Description:
 *   Return the MCLK / BCLK ratio closest to the requested sample rate,
 *   which is the BCLK divider plus two (at least 2).
 *
 * Input Parameters:
 *   priv - Driver state.
 *
 * Returned Value:
 *   The MCLK / BCLK ratio.
 *
 ****************************************************************************/

static uint32_t bl616cl_i2s_bclk_divider(const struct bl616cl_i2s_s *priv)
{
  uint32_t bclk = priv->samplerate * BL616CL_I2S_FRAME_BITS;
  uint32_t ratio = (priv->mclk + bclk / 2) / bclk;

  return ratio < 2 ? 2 : ratio;
}

/****************************************************************************
 * Name: bl616cl_i2s_configure
 *
 * Description:
 *   Program the controller for the current MCLK and sample rate: master,
 *   Philips I2S, two 32-bit slots per frame, 16-bit samples packed as one
 *   stereo frame per FIFO word. The controller is left disabled with empty
 *   FIFOs and no DMA request.
 *
 * Input Parameters:
 *   priv - Driver state.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_i2s_configure(struct bl616cl_i2s_s *priv)
{
  struct bflb_i2s_config_s config;
  uintptr_t io_config;

  memset(&config, 0, sizeof(config));
  config.bclk_freq_hz = priv->mclk / bl616cl_i2s_bclk_divider(priv);
  config.role = I2S_ROLE_MASTER;
  config.format_mode = I2S_MODE_LEFT_JUSTIFIED;
  config.channel_mode = I2S_CHANNEL_MODE_NUM_2;
  config.frame_width = I2S_SLOT_WIDTH_32;
  config.data_width = I2S_SLOT_WIDTH_16;

  /* Left-justified with a one BCLK data delay and FS inverted is Philips
   * I2S: left-justified sends the left slot while FS is high, Philips
   * while FS is low. bflb_i2s_init() clears the inversion bits.
   */

  config.fs_offset_cycle = 1;

  bflb_i2s_init(priv->i2s, &config);

  /* LHAL currently has no FS inversion command. Keep this write until
   * I2S_CMD_FS_INVERSE is available in the mirrored LHAL, then use it here.
   */

  io_config = priv->i2s->reg_base + I2S_IO_CONFIG_OFFSET;
  putreg32(getreg32(io_config) | I2S_CR_I2S_FS_INV, io_config);
  bflb_i2s_feature_control(priv->i2s, I2S_CMD_CHANNEL_LR_MERGE, true);
  priv->tx.running = false;
  priv->rx.running = false;
}

/****************************************************************************
 * Name: bl616cl_i2s_busy
 *
 * Description:
 *   Check whether buffers of one direction are queued or being
 *   transferred. Must be called with the spinlock held.
 *
 * Input Parameters:
 *   xpt - Transport (direction).
 *
 * Returned Value:
 *   True if a buffer is pending, active or, with the ring, still playing.
 *
 ****************************************************************************/

static bool bl616cl_i2s_busy(const struct bl616cl_i2s_transport_s *xpt)
{
#ifdef CONFIG_BL616CL_I2S_DMA_RING
  return !sq_empty(&xpt->pend) || !sq_empty(&xpt->flight);
#else
  return xpt->act != NULL || !sq_empty(&xpt->pend);
#endif
}

/****************************************************************************
 * Name: bl616cl_i2s_disable
 *
 * Description:
 *   Disable a running direction and its DMA request, and stop the bit
 *   clocks when the other direction is not running either. With the ring,
 *   also stop its cyclic DMA transfer. The DMA channel stays with the
 *   direction. Must be called with the spinlock held.
 *
 * Input Parameters:
 *   priv - Driver state.
 *   xpt - Transport (direction).
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_i2s_disable(struct bl616cl_i2s_s *priv,
                                struct bl616cl_i2s_transport_s *xpt)
{
  struct bl616cl_i2s_transport_s *other;
  uint32_t disable;

  other = xpt->tx ? &priv->rx : &priv->tx;
  disable = xpt->enable | (other->running ? 0 : I2S_ARG_CMD_MASTER);
  bflb_i2s_feature_control(priv->i2s, I2S_CMD_DISABLE_CONTROL, disable);
  if (xpt->tx)
    {
      bflb_i2s_link_txdma(priv->i2s, false);
    }
  else
    {
      bflb_i2s_link_rxdma(priv->i2s, false);
    }

#ifdef CONFIG_BL616CL_I2S_DMA_RING
  DMA_STOP(xpt->chan);
#endif
  xpt->running = false;
}

/****************************************************************************
 * Name: bl616cl_i2s_idle
 *
 * Description:
 *   If a direction has nothing left to do (see bl616cl_i2s_busy()),
 *   disable it. Everything is done under the spinlock, so the worker may
 *   call it without the mutex.
 *
 * Input Parameters:
 *   priv - Driver state.
 *   xpt - Transport (direction).
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_i2s_idle(struct bl616cl_i2s_s *priv,
                             struct bl616cl_i2s_transport_s *xpt)
{
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->spinlock);
  if (!bl616cl_i2s_busy(xpt) && xpt->running)
    {
      bl616cl_i2s_disable(priv, xpt);
    }

  spin_unlock_irqrestore(&priv->spinlock, flags);
}

#ifdef CONFIG_BL616CL_I2S_DMA_RING

/****************************************************************************
 * Name: bl616cl_i2s_fill
 *
 * Description:
 *   Fill one TX period of the ring from the pending buffers, in order, and
 *   pad the rest with silence. A buffer whose data is all in the ring moves
 *   to the flight queue with its end position; an empty buffer gets the
 *   position where it is reached, so it still finishes in queue order. It
 *   finishes as soon as the next buffer goes into the ring, so the client
 *   can queue more while the ring still plays it; the last buffer waits
 *   until it has been played, so a stream ends after its sound. Must be
 *   called with the spinlock held, or with the mutex held while TX is not
 *   running.
 *
 * Input Parameters:
 *   xpt - TX transport.
 *   period - First byte of the period in the ring.
 *
 * Returned Value:
 *   True if a buffer was moved to the done queue.
 *
 ****************************************************************************/

static bool bl616cl_i2s_fill(struct bl616cl_i2s_transport_s *xpt,
                             uint8_t *period)
{
  struct bl616cl_i2s_buffer_s *bfc;
  struct bl616cl_i2s_buffer_s *prev;
  size_t space = BL616CL_I2S_PERIOD_BYTES;
  size_t chunk;
  bool done = false;

  while ((bfc = (struct bl616cl_i2s_buffer_s *)sq_peek(&xpt->pend)) != NULL)
    {
      while ((prev = (struct bl616cl_i2s_buffer_s *)
                     sq_remfirst(&xpt->flight)) != NULL)
        {
          sq_addlast(&prev->entry, &xpt->done);
          done = true;
        }

      chunk = MIN(bfc->remaining, space);
      memcpy(period, (const void *)bfc->addr, chunk);
      period += chunk;
      space -= chunk;
      bfc->addr += chunk;
      bfc->remaining -= chunk;
      xpt->written += chunk;
      if (bfc->remaining > 0)
        {
          break;
        }

      bfc->end = xpt->written;
      sq_remfirst(&xpt->pend);
      sq_addlast(&bfc->entry, &xpt->flight);
    }

  memset(period, 0, space);
  xpt->written += space;
  return done;
}

/****************************************************************************
 * Name: bl616cl_i2s_fail
 *
 * Description:
 *   Finish all queued buffers of one direction with an error, in order.
 *   Must be called with the spinlock held.
 *
 * Input Parameters:
 *   xpt - Transport (direction).
 *   result - Negated errno value for the buffers.
 *
 * Returned Value:
 *   True if a buffer was moved to the done queue.
 *
 ****************************************************************************/

static bool bl616cl_i2s_fail(struct bl616cl_i2s_transport_s *xpt,
                             int result)
{
  struct bl616cl_i2s_buffer_s *bfc;
  bool done = false;

  while ((bfc = (struct bl616cl_i2s_buffer_s *)
                sq_remfirst(&xpt->flight)) != NULL ||
         (bfc = (struct bl616cl_i2s_buffer_s *)
                sq_remfirst(&xpt->pend)) != NULL)
    {
      bfc->result = result;
      sq_addlast(&bfc->entry, &xpt->done);
      done = true;
    }

  return done;
}

/****************************************************************************
 * Name: bl616cl_i2s_lap
 *
 * Description:
 *   Warn once per stream when the DMA is already back in the period that
 *   the interrupt is about to refill (TX) or copy out (RX): the interrupt
 *   came later than the other periods of the ring last, and TX replayed
 *   old samples or RX lost some.
 *
 * Input Parameters:
 *   xpt - Transport (direction).
 *   offset - Ring offset of the period.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_i2s_lap(struct bl616cl_i2s_transport_s *xpt,
                            size_t offset)
{
  size_t position;

  if (xpt->lapped)
    {
      return;
    }

  position = (BL616CL_I2S_RING_BYTES - DMA_RESIDUAL(xpt->chan)) %
             BL616CL_I2S_RING_BYTES;
  if (position - offset < BL616CL_I2S_PERIOD_BYTES)
    {
      xpt->lapped = true;
      syslog(LOG_WARNING, "I2S0 %s: period interrupt late, DMA lapped "
             "the ring\n", xpt->tx ? "TX" : "RX");
    }
}

/****************************************************************************
 * Name: bl616cl_i2s_dma_done
 *
 * Description:
 *   Handle a finished DMA period of one direction (interrupt context).
 *   TX finishes the last buffer once it is played and refills the period
 *   (see bl616cl_i2s_fill()); RX
 *   copies the period into the pending buffers, finishes the full ones and
 *   drops samples no buffer is queued for. On a DMA error all queued
 *   buffers finish with the error and the direction is disabled. The
 *   worker is scheduled for finished buffers.
 *
 * Input Parameters:
 *   priv - Driver state.
 *   xpt - Transport (direction).
 *   len - Ring offset at the end of the period (0 after the last one), or
 *         a negated errno value.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_i2s_dma_done(struct bl616cl_i2s_s *priv,
                                 struct bl616cl_i2s_transport_s *xpt,
                                 ssize_t len)
{
  struct bl616cl_i2s_buffer_s *bfc;
  irqstate_t flags;
  uint8_t *period;
  size_t offset;
  size_t chunk;
  size_t left;
  bool done = false;

  flags = spin_lock_irqsave(&priv->spinlock);
  DEBUGASSERT(xpt->running);
  if (len < 0)
    {
      done = bl616cl_i2s_fail(xpt, len);
      bl616cl_i2s_disable(priv, xpt);
    }
  else
    {
      offset = (len == 0 ? BL616CL_I2S_RING_BYTES : len) -
               BL616CL_I2S_PERIOD_BYTES;
      period = xpt->ring + offset;
      bl616cl_i2s_lap(xpt, offset);
      if (xpt->tx)
        {
          xpt->played += BL616CL_I2S_PERIOD_BYTES;
          while ((bfc = (struct bl616cl_i2s_buffer_s *)
                        sq_peek(&xpt->flight)) != NULL &&
                 (int32_t)(xpt->played - bfc->end) >= 0)
            {
              sq_remfirst(&xpt->flight);
              sq_addlast(&bfc->entry, &xpt->done);
              done = true;
            }

          done |= bl616cl_i2s_fill(xpt, period);
        }
      else
        {
          left = BL616CL_I2S_PERIOD_BYTES;
          while (left > 0 && (bfc = (struct bl616cl_i2s_buffer_s *)
                                    sq_peek(&xpt->pend)) != NULL)
            {
              chunk = MIN(bfc->remaining, left);
              memcpy((void *)bfc->addr, period, chunk);
              period += chunk;
              left -= chunk;
              bfc->addr += chunk;
              bfc->remaining -= chunk;
              if (bfc->remaining == 0)
                {
                  sq_remfirst(&xpt->pend);
                  sq_addlast(&bfc->entry, &xpt->done);
                  done = true;
                }
            }
        }
    }

  spin_unlock_irqrestore(&priv->spinlock, flags);

  if (done && work_available(&priv->work))
    {
      work_queue(HPWORK, &priv->work, bl616cl_i2s_worker, priv, 0);
    }
}

#else /* CONFIG_BL616CL_I2S_DMA_RING */

/****************************************************************************
 * Name: bl616cl_i2s_next
 *
 * Description:
 *   Start the next DMA chunk of one direction: the rest of the active
 *   buffer, or the first pending buffer. An empty buffer is finished
 *   without DMA, and a buffer whose DMA cannot start is finished with the
 *   error. Must be called with the spinlock held.
 *
 * Input Parameters:
 *   priv - Driver state.
 *   xpt - Transport (direction).
 *
 * Returned Value:
 *   True if a buffer was moved to the done queue.
 *
 ****************************************************************************/

static bool bl616cl_i2s_next(struct bl616cl_i2s_s *priv,
                             struct bl616cl_i2s_transport_s *xpt)
{
  struct bl616cl_i2s_buffer_s *bfc;
  bool done = false;
  size_t chunk;
  int ret;

  for (; ; )
    {
      if (xpt->act == NULL)
        {
          xpt->act = (struct bl616cl_i2s_buffer_s *)sq_remfirst(&xpt->pend);
          if (xpt->act == NULL)
            {
              return done;
            }
        }

      bfc = xpt->act;
      chunk = MIN(bfc->remaining, BL616CL_I2S_DMA_CHUNK);
      ret = OK;
      if (chunk > 0)
        {
          if (xpt->tx)
            {
              ret = DMA_START(xpt->chan, xpt->dma_callback, priv, xpt->fifo,
                              bfc->addr, chunk);
            }
          else
            {
              ret = DMA_START(xpt->chan, xpt->dma_callback, priv, bfc->addr,
                              xpt->fifo, chunk);
            }

          if (ret == OK)
            {
              bfc->addr += chunk;
              bfc->remaining -= chunk;
              return done;
            }
        }

      /* An empty buffer, or one whose DMA cannot start, is finished here
       * in queue order; an empty one may carry AUDIO_APB_FINAL.
       */

      bfc->result = ret;
      sq_addlast(&bfc->entry, &xpt->done);
      xpt->act = NULL;
      done = true;
    }
}

/****************************************************************************
 * Name: bl616cl_i2s_dma_done
 *
 * Description:
 *   Handle a DMA completion of one direction (interrupt context). Finish
 *   the active buffer when its last chunk is done, start the next chunk or
 *   pending buffer right away so that the FIFO does not run dry or
 *   overflow, and schedule the worker for finished buffers.
 *
 * Input Parameters:
 *   priv - Driver state.
 *   xpt - Transport (direction).
 *   len - Bytes transferred, or a negated errno value.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_i2s_dma_done(struct bl616cl_i2s_s *priv,
                                 struct bl616cl_i2s_transport_s *xpt,
                                 ssize_t len)
{
  struct bl616cl_i2s_buffer_s *bfc;
  irqstate_t flags;
  bool done = false;

  flags = spin_lock_irqsave(&priv->spinlock);
  bfc = xpt->act;
  DEBUGASSERT(bfc != NULL);
  if (len < 0)
    {
      bfc->result = len;
      bfc->remaining = 0;
    }

  if (bfc->remaining == 0)
    {
      sq_addlast(&bfc->entry, &xpt->done);
      xpt->act = NULL;
      done = true;
    }

  done |= bl616cl_i2s_next(priv, xpt);
  spin_unlock_irqrestore(&priv->spinlock, flags);

  if (done && work_available(&priv->work))
    {
      work_queue(HPWORK, &priv->work, bl616cl_i2s_worker, priv, 0);
    }
}

#endif /* CONFIG_BL616CL_I2S_DMA_RING */

/****************************************************************************
 * Name: bl616cl_i2s_txdma_callback
 *
 * Description:
 *   TX DMA callback (interrupt context), see bl616cl_i2s_dma_done().
 *
 * Input Parameters:
 *   chan - DMA channel (unused).
 *   arg - Driver state.
 *   len - DMA result, passed to bl616cl_i2s_dma_done().
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_i2s_txdma_callback(struct dma_chan_s *chan, void *arg,
                                       ssize_t len)
{
  struct bl616cl_i2s_s *priv = arg;

  UNUSED(chan);
  bl616cl_i2s_dma_done(priv, &priv->tx, len);
}

/****************************************************************************
 * Name: bl616cl_i2s_rxdma_callback
 *
 * Description:
 *   RX DMA callback (interrupt context), see bl616cl_i2s_dma_done().
 *
 * Input Parameters:
 *   chan - DMA channel (unused).
 *   arg - Driver state.
 *   len - DMA result, passed to bl616cl_i2s_dma_done().
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_i2s_rxdma_callback(struct dma_chan_s *chan, void *arg,
                                       ssize_t len)
{
  struct bl616cl_i2s_s *priv = arg;

  UNUSED(chan);
  bl616cl_i2s_dma_done(priv, &priv->rx, len);
}

/****************************************************************************
 * Name: bl616cl_i2s_return
 *
 * Description:
 *   Return the finished buffers of one direction to the client (task
 *   context). A received buffer gets the received byte count in nbytes;
 *   without the ring it is invalidated first, the ring copy goes through
 *   the cache. The container is freed before the callback, so the
 *   callback may queue the next buffer even when all containers are in
 *   use; then the buffer reference is dropped.
 *
 * Input Parameters:
 *   priv - Driver state.
 *   xpt - Transport (direction).
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_i2s_return(struct bl616cl_i2s_s *priv,
                               struct bl616cl_i2s_transport_s *xpt)
{
  struct bl616cl_i2s_buffer_s *bfc;
  struct ap_buffer_s *apb;
  i2s_callback_t callback;
  irqstate_t flags;
  void *arg;
  int result;

  for (; ; )
    {
      flags = spin_lock_irqsave(&priv->spinlock);
      bfc = (struct bl616cl_i2s_buffer_s *)sq_remfirst(&xpt->done);
      spin_unlock_irqrestore(&priv->spinlock, flags);
      if (bfc == NULL)
        {
          break;
        }

      apb = bfc->apb;
      callback = bfc->callback;
      arg = bfc->arg;
      result = bfc->result;
      if (!xpt->tx)
        {
#ifndef CONFIG_BL616CL_I2S_DMA_RING
          up_invalidate_dcache((uintptr_t)apb->samp,
                               (uintptr_t)apb->samp + bfc->nbytes);
#endif
          apb->nbytes = result == OK ? bfc->nbytes : 0;
          apb->curbyte = 0;
        }

      flags = spin_lock_irqsave(&priv->spinlock);
      sq_addlast(&bfc->entry, &priv->freeq);
      spin_unlock_irqrestore(&priv->spinlock, flags);
      nxsem_post(&priv->bufsem);

      callback(&priv->dev, apb, arg, result);
      apb_free(apb);
    }
}

/****************************************************************************
 * Name: bl616cl_i2s_worker
 *
 * Description:
 *   Return finished buffers to the client, then stop each direction that
 *   has nothing left to do.
 *
 * Input Parameters:
 *   arg - Driver state.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_i2s_worker(void *arg)
{
  struct bl616cl_i2s_s *priv = arg;

  bl616cl_i2s_return(priv, &priv->tx);
  bl616cl_i2s_return(priv, &priv->rx);
  bl616cl_i2s_idle(priv, &priv->tx);
  bl616cl_i2s_idle(priv, &priv->rx);
}

/****************************************************************************
 * Name: bl616cl_i2s_reconfigure
 *
 * Description:
 *   Prepare for a new MCLK or sample rate. Allowed only while no buffer is
 *   queued in either direction, because reprogramming disables the
 *   controller and clears its FIFOs. Must be called with the mutex held.
 *
 * Input Parameters:
 *   priv - Driver state.
 *
 * Returned Value:
 *   Zero (OK) if the controller may be reprogrammed; -EBUSY if buffers
 *   are queued.
 *
 ****************************************************************************/

static int bl616cl_i2s_reconfigure(struct bl616cl_i2s_s *priv)
{
  irqstate_t flags;
  bool busy;

  flags = spin_lock_irqsave(&priv->spinlock);
  busy = bl616cl_i2s_busy(&priv->tx) || bl616cl_i2s_busy(&priv->rx);
  spin_unlock_irqrestore(&priv->spinlock, flags);
  if (busy)
    {
      return -EBUSY;
    }

  bl616cl_i2s_idle(priv, &priv->tx);
  bl616cl_i2s_idle(priv, &priv->rx);
  return OK;
}

/****************************************************************************
 * Name: bl616cl_i2s_samplerate
 *
 * Description:
 *   Implement i2s_ops_s txsamplerate and rxsamplerate; both directions
 *   share the bit clocks. The bit clock is MCLK divided by an integer, so
 *   the resulting rate is the one closest to the request.
 *
 * Input Parameters:
 *   dev - I2S device.
 *   rate - Requested sample rate in Hz.
 *
 * Returned Value:
 *   The resulting sample rate in Hz; 0 if the rate is 0 or buffers are
 *   queued.
 *
 ****************************************************************************/

static uint32_t bl616cl_i2s_samplerate(struct i2s_dev_s *dev,
                                       uint32_t rate)
{
  struct bl616cl_i2s_s *priv = (struct bl616cl_i2s_s *)dev;
  uint32_t actual = 0;

  if (rate == 0)
    {
      return 0;
    }

  nxmutex_lock(&priv->lock);
  if (bl616cl_i2s_reconfigure(priv) == OK)
    {
      priv->samplerate = rate;
      bl616cl_i2s_configure(priv);
      actual = priv->mclk /
               (bl616cl_i2s_bclk_divider(priv) * BL616CL_I2S_FRAME_BITS);
    }

  nxmutex_unlock(&priv->lock);
  return actual;
}

/****************************************************************************
 * Name: bl616cl_i2s_datawidth
 *
 * Description:
 *   Implement i2s_ops_s txdatawidth and rxdatawidth. Only 16-bit samples
 *   are supported (see BL616CL_I2S_DATA_WIDTH).
 *
 * Input Parameters:
 *   dev - I2S device.
 *   bits - Bits per sample.
 *
 * Returned Value:
 *   16; or -EINVAL cast to uint32_t for other widths, the negated errno
 *   that codec drivers check for.
 *
 ****************************************************************************/

static uint32_t bl616cl_i2s_datawidth(struct i2s_dev_s *dev, int bits)
{
  UNUSED(dev);
  return bits == BL616CL_I2S_DATA_WIDTH ? BL616CL_I2S_DATA_WIDTH :
                                          (uint32_t)-EINVAL;
}

#ifdef CONFIG_BL616CL_I2S_DMA_RING

/****************************************************************************
 * Name: bl616cl_i2s_start
 *
 * Description:
 *   Start the cyclic DMA of an idle direction with its first buffer
 *   queued. TX fills the whole ring first, so that the DMA feeds the FIFO
 *   before the bit clocks start; the fill needs no spinlock because the
 *   interrupt and the worker leave an idle direction alone. If the DMA
 *   cannot start, the queued buffers finish with its error. Must be called
 *   with the mutex held.
 *
 * Input Parameters:
 *   priv - Driver state.
 *   xpt - Transport (direction).
 *
 * Returned Value:
 *   True if a buffer was moved to the done queue.
 *
 ****************************************************************************/

static bool bl616cl_i2s_start(struct bl616cl_i2s_s *priv,
                              struct bl616cl_i2s_transport_s *xpt)
{
  irqstate_t flags;
  unsigned int i;
  bool done = false;
  int ret;

  xpt->lapped = false;
  if (xpt->tx)
    {
      xpt->written = 0;
      xpt->played = 0;
      for (i = 0; i < CONFIG_BL616CL_I2S_RING_PERIODS; i++)
        {
          done |= bl616cl_i2s_fill(xpt,
                                   xpt->ring + i * BL616CL_I2S_PERIOD_BYTES);
        }
    }

  flags = spin_lock_irqsave(&priv->spinlock);
  if (xpt->tx)
    {
      bflb_i2s_feature_control(priv->i2s, I2S_CMD_CLEAR_TX_FIFO, 0);
      bflb_i2s_link_txdma(priv->i2s, true);
      ret = DMA_START_CYCLIC(xpt->chan, xpt->dma_callback, priv, xpt->fifo,
                             (uintptr_t)xpt->ring, BL616CL_I2S_RING_BYTES,
                             BL616CL_I2S_PERIOD_BYTES);
    }
  else
    {
      bflb_i2s_feature_control(priv->i2s, I2S_CMD_CLEAR_RX_FIFO, 0);
      bflb_i2s_link_rxdma(priv->i2s, true);
      ret = DMA_START_CYCLIC(xpt->chan, xpt->dma_callback, priv,
                             (uintptr_t)xpt->ring, xpt->fifo,
                             BL616CL_I2S_RING_BYTES,
                             BL616CL_I2S_PERIOD_BYTES);
    }

  if (ret == OK)
    {
      bflb_i2s_feature_control(priv->i2s, I2S_CMD_ENABLE_CONTROL,
                               I2S_ARG_CMD_MASTER | xpt->enable);
      xpt->running = true;
    }
  else
    {
      if (xpt->tx)
        {
          bflb_i2s_link_txdma(priv->i2s, false);
        }
      else
        {
          bflb_i2s_link_rxdma(priv->i2s, false);
        }

      done |= bl616cl_i2s_fail(xpt, ret);
    }

  spin_unlock_irqrestore(&priv->spinlock, flags);
  return done;
}

/****************************************************************************
 * Name: bl616cl_i2s_enqueue
 *
 * Description:
 *   Queue a transfer of one direction and start the stream if it is idle;
 *   an empty buffer on an idle direction finishes right away. The data is
 *   copied through the cache, so the buffer needs no cache maintenance.
 *
 * Input Parameters:
 *   priv - Driver state.
 *   xpt - Transport (direction).
 *   apb - Audio buffer.
 *   callback - Called from the worker when the buffer is done.
 *   arg - Callback argument.
 *   addr - First byte to transfer (word-aligned).
 *   nbytes - Bytes to transfer (whole frames).
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_i2s_enqueue(struct bl616cl_i2s_s *priv,
                                struct bl616cl_i2s_transport_s *xpt,
                                struct ap_buffer_s *apb,
                                i2s_callback_t callback, void *arg,
                                uintptr_t addr, size_t nbytes)
{
  struct bl616cl_i2s_buffer_s *bfc;
  irqstate_t flags;
  bool start;
  bool done = false;

  apb_reference(apb);

  flags = spin_lock_irqsave(&priv->spinlock);
  bfc = (struct bl616cl_i2s_buffer_s *)sq_remfirst(&priv->freeq);
  DEBUGASSERT(bfc != NULL);
  bfc->apb = apb;
  bfc->callback = callback;
  bfc->arg = arg;
  bfc->addr = addr;
  bfc->remaining = nbytes;
  bfc->nbytes = nbytes;
  bfc->result = OK;
  start = !xpt->running && nbytes > 0;
  if (xpt->running || nbytes > 0)
    {
      sq_addlast(&bfc->entry, &xpt->pend);
    }
  else
    {
      sq_addlast(&bfc->entry, &xpt->done);
      done = true;
    }

  spin_unlock_irqrestore(&priv->spinlock, flags);

  if (start)
    {
      done = bl616cl_i2s_start(priv, xpt);
    }

  if (done && work_available(&priv->work))
    {
      work_queue(HPWORK, &priv->work, bl616cl_i2s_worker, priv, 0);
    }
}

#else /* CONFIG_BL616CL_I2S_DMA_RING */

/****************************************************************************
 * Name: bl616cl_i2s_enqueue
 *
 * Description:
 *   Queue a transfer of one direction and start the stream if it is idle.
 *
 * Input Parameters:
 *   priv - Driver state.
 *   xpt - Transport (direction).
 *   apb - Audio buffer.
 *   callback - Called from the worker when the buffer is done.
 *   arg - Callback argument.
 *   addr - First byte to transfer (word-aligned).
 *   nbytes - Bytes to transfer (whole frames).
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_i2s_enqueue(struct bl616cl_i2s_s *priv,
                                struct bl616cl_i2s_transport_s *xpt,
                                struct ap_buffer_s *apb,
                                i2s_callback_t callback, void *arg,
                                uintptr_t addr, size_t nbytes)
{
  struct bl616cl_i2s_buffer_s *bfc;
  irqstate_t flags;
  bool done = false;

  /* TX data must reach memory before the DMA reads it. RX lines must not
   * be dirty, or a later eviction would overwrite what the DMA wrote.
   */

  apb_reference(apb);
  if (nbytes > 0 && xpt->tx)
    {
      up_clean_dcache(addr, addr + nbytes);
    }
  else if (nbytes > 0)
    {
      up_invalidate_dcache(addr, addr + nbytes);
    }

  flags = spin_lock_irqsave(&priv->spinlock);
  bfc = (struct bl616cl_i2s_buffer_s *)sq_remfirst(&priv->freeq);
  DEBUGASSERT(bfc != NULL);
  bfc->apb = apb;
  bfc->callback = callback;
  bfc->arg = arg;
  bfc->addr = addr;
  bfc->remaining = nbytes;
  bfc->nbytes = nbytes;
  bfc->result = OK;
  sq_addlast(&bfc->entry, &xpt->pend);

  if (xpt->act == NULL)
    {
      /* For TX, fill the FIFO from DMA before the bit clocks start */

      if (!xpt->running && nbytes > 0)
        {
          if (xpt->tx)
            {
              bflb_i2s_feature_control(priv->i2s, I2S_CMD_CLEAR_TX_FIFO, 0);
              bflb_i2s_link_txdma(priv->i2s, true);
            }
          else
            {
              bflb_i2s_feature_control(priv->i2s, I2S_CMD_CLEAR_RX_FIFO, 0);
              bflb_i2s_link_rxdma(priv->i2s, true);
            }
        }

      done = bl616cl_i2s_next(priv, xpt);
      if (!xpt->running && xpt->act != NULL)
        {
          bflb_i2s_feature_control(priv->i2s, I2S_CMD_ENABLE_CONTROL,
                                   I2S_ARG_CMD_MASTER | xpt->enable);
          xpt->running = true;
        }
    }

  spin_unlock_irqrestore(&priv->spinlock, flags);

  if (done && work_available(&priv->work))
    {
      work_queue(HPWORK, &priv->work, bl616cl_i2s_worker, priv, 0);
    }
}

#endif /* CONFIG_BL616CL_I2S_DMA_RING */

/****************************************************************************
 * Name: bl616cl_i2s_receive
 *
 * Description:
 *   Implement i2s_ops_s receive: queue the buffer and start receiving if
 *   RX is idle. The buffer is filled up to nmaxbytes, rounded down to
 *   whole D-cache lines and frames, and nbytes is set on completion.
 *   Samples between buffers are lost when no buffer is queued. The timeout
 *   is not supported.
 *
 * Input Parameters:
 *   dev - I2S device.
 *   apb - Audio buffer; samp must be word-aligned.
 *   callback - Called from the worker when the buffer is done.
 *   arg - Callback argument.
 *   timeout - Ignored.
 *
 * Returned Value:
 *   Zero (OK) if the buffer was queued; -EINVAL for a misaligned or too
 *   small buffer.
 *
 ****************************************************************************/

static int bl616cl_i2s_receive(struct i2s_dev_s *dev,
                               struct ap_buffer_s *apb,
                               i2s_callback_t callback, void *arg,
                               uint32_t timeout)
{
  struct bl616cl_i2s_s *priv = (struct bl616cl_i2s_s *)dev;
  size_t line = MAX(up_get_dcache_linesize(), 1);
  uintptr_t addr;
  size_t nbytes;
  int ret;

  UNUSED(timeout);
  DEBUGASSERT(apb != NULL && callback != NULL);

  addr = (uintptr_t)apb->samp;
  nxsem_wait_uninterruptible(&priv->bufsem);
  nxmutex_lock(&priv->lock);

  /* Whole cache lines keep the CPU off the lines the DMA writes */

  nbytes = apb->nmaxbytes / line * line;
  nbytes -= nbytes % BL616CL_I2S_FRAME_BYTES;
  if (nbytes == 0 || addr % BL616CL_I2S_DMA_WIDTH != 0)
    {
      nxsem_post(&priv->bufsem);
      ret = -EINVAL;
    }
  else
    {
      bl616cl_i2s_enqueue(priv, &priv->rx, apb, callback, arg, addr, nbytes);
      ret = OK;
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

/****************************************************************************
 * Name: bl616cl_i2s_send
 *
 * Description:
 *   Implement i2s_ops_s send: queue the buffer and start transmitting if
 *   TX is idle. The timeout is not supported.
 *
 * Input Parameters:
 *   dev - I2S device.
 *   apb - Audio buffer; bytes curbyte..nbytes are sent. Incomplete final
 *         frames are dropped. Nonempty transfers must start word-aligned.
 *         Empty buffers complete in queue order without starting DMA.
 *   callback - Called from the worker when the buffer is done.
 *   arg - Callback argument.
 *   timeout - Ignored.
 *
 * Returned Value:
 *   Zero (OK) if the buffer was queued; -EINVAL for an invalid or misaligned
 *   buffer.
 *
 ****************************************************************************/

static int bl616cl_i2s_send(struct i2s_dev_s *dev,
                            struct ap_buffer_s *apb,
                            i2s_callback_t callback, void *arg,
                            uint32_t timeout)
{
  struct bl616cl_i2s_s *priv = (struct bl616cl_i2s_s *)dev;
  uintptr_t addr;
  size_t nbytes;
  int ret;

  UNUSED(timeout);
  DEBUGASSERT(apb != NULL && callback != NULL);

  if (apb->nbytes < apb->curbyte)
    {
      return -EINVAL;
    }

  addr = (uintptr_t)apb->samp + apb->curbyte;
  nbytes = apb->nbytes - apb->curbyte;

  nxsem_wait_uninterruptible(&priv->bufsem);
  nxmutex_lock(&priv->lock);

  nbytes -= nbytes % BL616CL_I2S_FRAME_BYTES;
  if (nbytes > 0 && addr % BL616CL_I2S_DMA_WIDTH != 0)
    {
      nxsem_post(&priv->bufsem);
      ret = -EINVAL;
    }
  else
    {
      bl616cl_i2s_enqueue(priv, &priv->tx, apb, callback, arg, addr, nbytes);
      ret = OK;
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

/****************************************************************************
 * Name: bl616cl_i2s_getmclkfrequency
 *
 * Description:
 *   Implement i2s_ops_s getmclkfrequency.
 *
 * Input Parameters:
 *   dev - I2S device.
 *
 * Returned Value:
 *   The current MCLK frequency in Hz.
 *
 ****************************************************************************/

static uint32_t bl616cl_i2s_getmclkfrequency(struct i2s_dev_s *dev)
{
  return ((struct bl616cl_i2s_s *)dev)->mclk;
}

/****************************************************************************
 * Name: bl616cl_i2s_mclk_apply
 *
 * Description:
 *   Set the I2S reference clock divider closest to the requested MCLK and
 *   record the resulting frequency.
 *
 * Input Parameters:
 *   priv - Driver state.
 *   frequency - Requested MCLK in Hz (non-zero).
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_i2s_mclk_apply(struct bl616cl_i2s_s *priv,
                                   uint32_t frequency)
{
  uint32_t div;

  div = (BL616CL_I2S_PLL_FREQUENCY + frequency / 2) / frequency;
  div = div > 0 ? div - 1 : 0;
  div = MIN(div, BL616CL_I2S_REFDIV_MAX);

  GLB_Set_I2S_CLK(ENABLE, div, GLB_I2S_DI_SEL_I2S_DI_INPUT,
                  GLB_I2S_DO_SEL_I2S_DO_OUTPT);
  priv->mclk = bflb_clk_get_peripheral_clock(BFLB_DEVICE_TYPE_I2S,
                                             priv->i2s->idx);
}

/****************************************************************************
 * Name: bl616cl_i2s_setmclkfrequency
 *
 * Description:
 *   Implement i2s_ops_s setmclkfrequency. MCLK is WIFIPLL 320 MHz divided
 *   by 1..64, so the resulting frequency is the one closest to the
 *   request (12.288 MHz gives 12.3077 MHz, 11.2896 MHz gives 11.4286 MHz).
 *   The bit clock divider is recomputed for the current sample rate.
 *
 * Input Parameters:
 *   dev - I2S device.
 *   frequency - Requested MCLK in Hz.
 *
 * Returned Value:
 *   The resulting MCLK in Hz; -EINVAL for 0; -EBUSY if buffers are
 *   queued.
 *
 ****************************************************************************/

static uint32_t bl616cl_i2s_setmclkfrequency(struct i2s_dev_s *dev,
                                             uint32_t frequency)
{
  struct bl616cl_i2s_s *priv = (struct bl616cl_i2s_s *)dev;
  uint32_t ret = (uint32_t)-EBUSY;

  if (frequency == 0)
    {
      return (uint32_t)-EINVAL;
    }

  nxmutex_lock(&priv->lock);
  if (bl616cl_i2s_reconfigure(priv) == OK)
    {
      bl616cl_i2s_mclk_apply(priv, frequency);
      bl616cl_i2s_configure(priv);
      ret = priv->mclk;
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

/****************************************************************************
 * Name: bl616cl_i2s_pins_valid
 *
 * Description:
 *   Check the pins against the fixed GPIO % 4 function layout.
 *
 * Input Parameters:
 *   pins - Pin assignment.
 *
 * Returned Value:
 *   True if every pin exists and sits on its function slot.
 *
 ****************************************************************************/

static bool bl616cl_i2s_pins_valid(const struct bl616cl_i2s_pins_s *pins)
{
  return pins->mclk < GPIO_PIN_MAX && pins->bclk < GPIO_PIN_MAX &&
         pins->fs < GPIO_PIN_MAX && pins->di < GPIO_PIN_MAX &&
         pins->dout < GPIO_PIN_MAX && pins->bclk % 4 == 0 &&
         pins->fs % 4 == 1 && pins->di % 4 == 2 && pins->dout % 4 == 3;
}

/****************************************************************************
 * Name: bl616cl_i2s_clkout_select
 *
 * Description:
 *   Route the I2S reference clock to CLKOUT channel 'channel' (enabling
 *   it).
 *
 * Input Parameters:
 *   channel - CLKOUT channel, 0..3.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_i2s_clkout_select(unsigned int channel)
{
  switch (channel)
    {
      case 0:
        GLB_Set_Chip_Clock_Out0_Sel(GLB_CHIP_CLK_OUT_0_I2S_REF_CLK);
        break;

      case 1:
        GLB_Set_Chip_Clock_Out1_Sel(GLB_CHIP_CLK_OUT_1_I2S_REF_CLK);
        break;

      case 2:
        GLB_Set_Chip_Clock_Out2_Sel(GLB_CHIP_CLK_OUT_2_I2S_REF_CLK);
        break;

      default:
        GLB_Set_Chip_Clock_Out3_Sel(GLB_CHIP_CLK_OUT_3_I2S_REF_CLK);
        break;
    }
}

/****************************************************************************
 * Name: bl616cl_i2s_dma_setup
 *
 * Description:
 *   Take and configure the DMA0 channel of one direction. The channel is
 *   kept for the life of the driver.
 *
 * Input Parameters:
 *   priv - Driver state.
 *   xpt - Transport (direction).
 *
 * Returned Value:
 *   Zero (OK) on success; -ENODEV if the channel cannot be configured.
 *
 ****************************************************************************/

static int bl616cl_i2s_dma_setup(struct bl616cl_i2s_s *priv,
                                 struct bl616cl_i2s_transport_s *xpt)
{
  struct dma_config_s config;

  memset(&config, 0, sizeof(config));
  config.direction = xpt->tx ? DMA_MEM_TO_DEV : DMA_DEV_TO_MEM;
  config.src_width = BL616CL_I2S_DMA_WIDTH;
  config.dst_width = BL616CL_I2S_DMA_WIDTH;
  if (xpt->tx)
    {
      config.src_step = BL616CL_I2S_DMA_WIDTH;
      config.dst_drq = xpt->drq;
    }
  else
    {
      config.dst_step = BL616CL_I2S_DMA_WIDTH;
      config.src_drq = xpt->drq;
    }

  xpt->chan = DMA_GET_CHAN(priv->dma, xpt->ident);
  if (xpt->chan == NULL || DMA_CONFIG(xpt->chan, &config) < 0)
    {
      if (xpt->chan != NULL)
        {
          DMA_PUT_CHAN(priv->dma, xpt->chan);
          xpt->chan = NULL;
        }

      return -ENODEV;
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_i2sbus_initialize
 *
 * Description:
 *   Initialize I2S0 as an I2S master: configure the pins, start MCLK at
 *   about 12.288 MHz and set up 48 kHz, 16-bit stereo Philips I2S.
 *   Transfers use DMA0; each direction takes its channel here and keeps
 *   it. A later call with the same pins returns the same device.
 *
 * Input Parameters:
 *   pins - I2S0 pin assignment.
 *
 * Returned Value:
 *   An I2S device on success; NULL if a pin is invalid, the pins differ
 *   from an earlier call, DMA0 or a DMA0 channel is not available, or a
 *   device lookup fails.
 *
 ****************************************************************************/

struct i2s_dev_s *bl616cl_i2sbus_initialize(
  const struct bl616cl_i2s_pins_s *pins)
{
  struct bl616cl_i2s_s *priv = &g_bl616cl_i2s0;
  struct bflb_device_s *gpio;
  uint8_t i2s_pins[4];
  unsigned int i;

  DEBUGASSERT(pins != NULL);
  if (!bl616cl_i2s_pins_valid(pins))
    {
      return NULL;
    }

  nxmutex_lock(&priv->lock);
  if (priv->initialized)
    {
      nxmutex_unlock(&priv->lock);
      return memcmp(&priv->pins, pins, sizeof(*pins)) == 0 ?
             &priv->dev : NULL;
    }

  gpio = bflb_device_get_by_name(BFLB_NAME_GPIO);
  priv->i2s = bflb_device_get_by_name(BFLB_NAME_I2S0);
  priv->dma = bl616cl_dma0_device();
  if (gpio == NULL || priv->i2s == NULL || priv->dma == NULL)
    {
      nxmutex_unlock(&priv->lock);
      return NULL;
    }

  if (bl616cl_i2s_dma_setup(priv, &priv->tx) < 0 ||
      bl616cl_i2s_dma_setup(priv, &priv->rx) < 0)
    {
      if (priv->tx.chan != NULL)
        {
          DMA_PUT_CHAN(priv->dma, priv->tx.chan);
          priv->tx.chan = NULL;
        }

      nxmutex_unlock(&priv->lock);
      return NULL;
    }

  nxsem_init(&priv->bufsem, 0, CONFIG_BL616CL_I2S_NBUFFERS);
  sq_init(&priv->freeq);
  sq_init(&priv->tx.pend);
  sq_init(&priv->tx.done);
  sq_init(&priv->rx.pend);
  sq_init(&priv->rx.done);
#ifdef CONFIG_BL616CL_I2S_DMA_RING
  sq_init(&priv->tx.flight);
  sq_init(&priv->rx.flight);
#endif
  for (i = 0; i < CONFIG_BL616CL_I2S_NBUFFERS; i++)
    {
      sq_addlast(&priv->buffers[i].entry, &priv->freeq);
    }

  bflb_peripheral_clock_control(BFLB_PERIPHERAL_I2S0, true);
  priv->samplerate = BL616CL_I2S_DEFAULT_RATE;
  bl616cl_i2s_mclk_apply(priv, BL616CL_I2S_DEFAULT_MCLK);
  bl616cl_i2s_configure(priv);

  i2s_pins[0] = pins->bclk;
  i2s_pins[1] = pins->fs;
  i2s_pins[2] = pins->di;
  i2s_pins[3] = pins->dout;
  for (i = 0; i < nitems(i2s_pins); i++)
    {
      pm_disable_gpio_keep(i2s_pins[i]);
      bflb_gpio_init(gpio, i2s_pins[i], BL616CL_I2S_GPIO_CFG);
    }

  bl616cl_i2s_clkout_select(pins->mclk % 4);
  pm_disable_gpio_keep(pins->mclk);
  bflb_gpio_init(gpio, pins->mclk, BL616CL_I2S_MCLK_GPIO_CFG);

  priv->pins = *pins;
  priv->initialized = true;
  nxmutex_unlock(&priv->lock);
  return &priv->dev;
}
