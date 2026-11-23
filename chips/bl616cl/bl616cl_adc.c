/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_adc.c
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

/* The driver is split into three parts so that later transfer modes only
 * add code to the first two:
 *
 * - Pacing starts and stops conversions: one scan per ANIOC_TRIGGER
 *   (CONFIG_BL616CL_ADC_POLL) or a continuous scan (CONFIG_BL616CL_ADC_IRQ).
 * - Transfer moves raw FIFO words to a staging buffer: the trigger itself
 *   when polling, a high priority worker after FIFO_RDY otherwise.
 * - Delivery converts the words to millivolts, checks each channel tag
 *   against the scan order and queues them to the upper half.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>
#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

#include <arch/irq.h>

#include "bl616cl_sdk.h"
#include "bl616cl_glb.h"
#include "bflb_adc_v2.h"
#include "bflb_clock.h"
#include "bflb_gpio.h"
#include "bflb_name.h"
#include "bflb_peri.h"
#include "bl616cl_adc.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Depth of the regular FIFO (measured). A sample that finds it full is
 * dropped and the scan goes on with the next channel.
 */

#define BL616CL_ADC_FIFO_DEPTH 16

#define BL616CL_ADC_GPIO_CFG   (GPIO_ANALOG | GPIO_SMT_EN | GPIO_DRV_0)

#ifdef CONFIG_BL616CL_ADC_POLL
/* Longest wait for one scan; 12 channels take about 4.6 ms at XCLK/64 */

#  define BL616CL_ADC_SCAN_TIMEOUT_MS 10

/* Upper-half queue: two scans of all channels plus the slot that a ring
 * buffer keeps free.
 */

#  define BL616CL_ADC_QUEUE_SIZE (2 * BL616CL_ADC_NCHANNELS + 1)
#else
/* FIFO_RDY fires once the FIFO holds this many samples. The worker then
 * has the time of the remaining 8 samples to drain it: about 1 ms at
 * XCLK/20 and 3 ms at XCLK/64.
 */

#  define BL616CL_ADC_RDY_LEVEL       8
#  define BL616CL_ADC_QUEUE_SIZE      64
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

#ifdef CONFIG_BL616CL_ADC_IRQ
/* Continuous scan state, changed under the upper-half ad_spinlock */

enum bl616cl_adc_state_e
{
  BL616CL_ADC_IDLE = 0,             /* Stopped; ANIOC_TRIGGER may start */
  BL616CL_ADC_RUNNING,              /* Scanning; the worker enables the IRQ */
  BL616CL_ADC_STOPPING              /* The worker or the last close stops it */
};
#endif

struct bl616cl_adc_priv_s
{
  const struct adc_callback_s *cb;  /* Upper-half callbacks */
  struct bflb_device_s *adc;        /* LHAL GPADC device */
  uint8_t channels[BL616CL_ADC_NCHANNELS]; /* Scan order */
  uint8_t nchannels;                /* Entries in channels */
  uint8_t next;                     /* Scan index of the next sample */
#ifdef CONFIG_BL616CL_ADC_IRQ
  struct work_s work;               /* Drains the FIFO after FIFO_RDY */
  uint8_t state;                    /* enum bl616cl_adc_state_e */
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bl616cl_adc_bind(struct adc_dev_s *dev,
                            const struct adc_callback_s *callback);
static void bl616cl_adc_reset(struct adc_dev_s *dev);
static int bl616cl_adc_setup(struct adc_dev_s *dev);
static void bl616cl_adc_shutdown(struct adc_dev_s *dev);
static void bl616cl_adc_rxint(struct adc_dev_s *dev, bool enable);
static int bl616cl_adc_ioctl(struct adc_dev_s *dev, int cmd,
                             unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* GPIO of each external channel */

static const uint8_t g_bl616cl_adc_pins[BL616CL_ADC_NCHANNELS] =
{
  0, 1, 2, 3, 4, 5, 6, 7, 12, 13, 32, 33
};

static const struct adc_ops_s g_bl616cl_adc_ops =
{
  .ao_bind     = bl616cl_adc_bind,
  .ao_reset    = bl616cl_adc_reset,
  .ao_setup    = bl616cl_adc_setup,
  .ao_shutdown = bl616cl_adc_shutdown,
  .ao_rxint    = bl616cl_adc_rxint,
  .ao_ioctl    = bl616cl_adc_ioctl,
};

static struct bl616cl_adc_priv_s g_bl616cl_adc_priv;

static uint8_t g_bl616cl_adc_queue_channel[BL616CL_ADC_QUEUE_SIZE];
static uint32_t g_bl616cl_adc_queue_data[BL616CL_ADC_QUEUE_SIZE];

static struct adc_dev_s g_bl616cl_adc_dev =
{
  .ad_ops  = &g_bl616cl_adc_ops,
  .ad_priv = &g_bl616cl_adc_priv,
  .ad_recv =
  {
    .af_channel  = g_bl616cl_adc_queue_channel,
    .af_data     = g_bl616cl_adc_queue_data,
    .af_fifosize = BL616CL_ADC_QUEUE_SIZE,
  },
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_adc_configure
 *
 * Description:
 *   Configure the GPADC for the scan of priv->channels: single-ended
 *   16-bit conversions against the internal reference, one pass per start
 *   when polling, looping otherwise. The first call after power-on also
 *   calibrates against the efuse trims (about 7 ms).
 *
 * Input Parameters:
 *   priv - Driver state
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value from the LHAL.
 *
 ****************************************************************************/

static int bl616cl_adc_configure(struct bl616cl_adc_priv_s *priv)
{
  struct bflb_adc_channel_s scan[BL616CL_ADC_NCHANNELS];
  struct bflb_adc_config_s config =
  {
    .scan_conv_mode       = true,
#ifdef CONFIG_BL616CL_ADC_IRQ
    .continuous_conv_mode = true,
#else
    .continuous_conv_mode = false,
#endif
    .differential_mode    = false,
    .resolution           = ADC_RESOLUTION_16B,
    .vref                 = ADC_VREF_INTERNAL_1P25,
  };

  unsigned int i;
  int ret;

  for (i = 0; i < priv->nchannels; i++)
    {
      scan[i].pos_chan = priv->channels[i];
      scan[i].neg_chan = ADC_EXTERNAL_CHANNEL_NULL;
    }

  ret = bflb_adc_init(priv->adc, &config);
  if (ret < 0)
    {
      return ret;
    }

  ret = bflb_adc_channel_config_external(priv->adc, scan, priv->nchannels);
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_BL616CL_ADC_IRQ
  bflb_adc_feature_control(priv->adc, ADC_CMD_SET_THRE_RDY_REGULAR,
                           BL616CL_ADC_RDY_LEVEL);
#endif

  return OK;
}

/****************************************************************************
 * Name: bl616cl_adc_halt
 *
 * Description:
 *   Stop conversions and empty the FIFO.
 *
 * Input Parameters:
 *   priv - Driver state
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_adc_halt(struct bl616cl_adc_priv_s *priv)
{
  bflb_adc_stop_conversion(priv->adc);
  bflb_adc_clear_fifo(priv->adc);
}

/****************************************************************************
 * Name: bl616cl_adc_convert
 *
 * Description:
 *   Convert raw FIFO words to millivolts. Each word carries the channel it
 *   was converted on, which must be the next channel in the scan order;
 *   conversion stops at the first word that is not. Runs in task context:
 *   bflb_adc_parse_result() uses the FPU, whose registers are not saved
 *   on interrupt entry.
 *
 * Input Parameters:
 *   priv    - Driver state; priv->next is advanced past the converted words
 *   raw     - Raw FIFO words, oldest first
 *   count   - Number of words, at most BL616CL_ADC_FIFO_DEPTH
 *   channel - Receives the channel of each converted word
 *   data    - Receives the millivolts of each converted word
 *
 * Returned Value:
 *   The number of leading words that follow the scan order and were
 *   converted.
 *
 ****************************************************************************/

static unsigned int bl616cl_adc_convert(struct bl616cl_adc_priv_s *priv,
                                        uint32_t *raw, unsigned int count,
                                        uint8_t *channel, uint32_t *data)
{
  struct bflb_adc_result_s result[BL616CL_ADC_FIFO_DEPTH];
  unsigned int i;

  bflb_adc_parse_result(priv->adc, raw, result, count);

  for (i = 0; i < count; i++)
    {
      if (result[i].pos_chan != priv->channels[priv->next])
        {
          break;
        }

      channel[i] = result[i].pos_chan;
      data[i]    = result[i].millivolt;

      if (++priv->next >= priv->nchannels)
        {
          priv->next = 0;
        }
    }

  return i;
}

/****************************************************************************
 * Name: bl616cl_adc_queue
 *
 * Description:
 *   Queue converted samples to the upper half, all or none, and wake its
 *   readers. A call with no samples only wakes the readers.
 *
 * Input Parameters:
 *   dev     - ADC device
 *   channel - Channel of each sample
 *   data    - Millivolts of each sample
 *   count   - Number of samples
 *
 * Returned Value:
 *   Zero (OK) on success; -ENOMEM if the queue has no room for them.
 *
 ****************************************************************************/

static int bl616cl_adc_queue(struct adc_dev_s *dev, const uint8_t *channel,
                             const uint32_t *data, unsigned int count)
{
  struct bl616cl_adc_priv_s *priv = dev->ad_priv;
  irqstate_t flags;
  int ret;

  /* The upper half reads its queue under ad_spinlock; the callbacks expect
   * the caller to hold it, as an interrupt handler would.
   */

  flags = spin_lock_irqsave(&dev->ad_spinlock);
  ret = priv->cb->au_receive_batch(dev, channel, data, count);
  spin_unlock_irqrestore(&dev->ad_spinlock, flags);

  return ret;
}

#ifdef CONFIG_BL616CL_ADC_POLL
/****************************************************************************
 * Name: bl616cl_adc_scan
 *
 * Description:
 *   Convert every channel once, polling the FIFO, and queue the group.
 *
 * Input Parameters:
 *   dev - ADC device
 *
 * Returned Value:
 *   Zero (OK) on success; -ETIMEDOUT if the scan does not finish within
 *   BL616CL_ADC_SCAN_TIMEOUT_MS; -EIO if a sample carries the wrong
 *   channel; -EOVERFLOW if the upper-half queue has no room for the group.
 *   Nothing is queued on failure.
 *
 ****************************************************************************/

static int bl616cl_adc_scan(struct adc_dev_s *dev)
{
  struct bl616cl_adc_priv_s *priv = dev->ad_priv;
  uint32_t raw[BL616CL_ADC_NCHANNELS];
  uint8_t channel[BL616CL_ADC_NCHANNELS];
  uint32_t data[BL616CL_ADC_NCHANNELS];
  clock_t start;
  bool expired;
  unsigned int i;

  bflb_adc_clear_fifo(priv->adc);
  bflb_adc_start_conversion(priv->adc);

  start = clock_systime_ticks();
  for (; ; )
    {
      /* Sample the clock first, so that a task switch after it cannot
       * expire a scan that has finished.
       */

      expired = clock_systime_ticks() - start >
                MSEC2TICK(BL616CL_ADC_SCAN_TIMEOUT_MS);

      if (bflb_adc_get_count(priv->adc) >= priv->nchannels)
        {
          break;
        }

      if (expired)
        {
          bl616cl_adc_halt(priv);
          return -ETIMEDOUT;
        }
    }

  for (i = 0; i < priv->nchannels; i++)
    {
      raw[i] = bflb_adc_read_raw(priv->adc);
    }

  bflb_adc_stop_conversion(priv->adc);

  priv->next = 0;
  if (bl616cl_adc_convert(priv, raw, priv->nchannels, channel, data) <
      priv->nchannels)
    {
      aerr("ERROR: ADC sample out of scan order\n");
      return -EIO;
    }

  if (bl616cl_adc_queue(dev, channel, data, priv->nchannels) < 0)
    {
      return -EOVERFLOW;
    }

  return OK;
}
#endif /* CONFIG_BL616CL_ADC_POLL */

#ifdef CONFIG_BL616CL_ADC_IRQ
/****************************************************************************
 * Name: bl616cl_adc_stop
 *
 * Description:
 *   Stop conversions with the GPADC interrupts masked, and discard the
 *   samples in the FIFO and the interrupt flags. The caller has disabled
 *   the IRQ and set the state to BL616CL_ADC_STOPPING.
 *
 * Input Parameters:
 *   priv - Driver state
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_adc_stop(struct bl616cl_adc_priv_s *priv)
{
  bflb_adc_int_mask(priv->adc, ADC_INT_REGULAR_FIFO, true);
  bl616cl_adc_halt(priv);
  bflb_adc_int_clear(priv->adc, ADC_INT_REGULAR_FIFO);
}

/****************************************************************************
 * Name: bl616cl_adc_start
 *
 * Description:
 *   Start the continuous scan from the first channel.
 *
 * Input Parameters:
 *   dev - ADC device
 *
 * Returned Value:
 *   Zero (OK) on success; -EBUSY if a scan is running or being stopped.
 *
 ****************************************************************************/

static int bl616cl_adc_start(struct adc_dev_s *dev)
{
  struct bl616cl_adc_priv_s *priv = dev->ad_priv;
  irqstate_t flags;

  flags = spin_lock_irqsave(&dev->ad_spinlock);
  if (priv->state != BL616CL_ADC_IDLE)
    {
      spin_unlock_irqrestore(&dev->ad_spinlock, flags);
      return -EBUSY;
    }

  priv->state = BL616CL_ADC_RUNNING;
  spin_unlock_irqrestore(&dev->ad_spinlock, flags);

  /* Clearing CONV_START does not end a scan round: the GPADC still
   * converts the rest of the round, after the stop has emptied the FIFO.
   * A start before the round ends would deliver those samples first.
   * Setting the GPADC up again soft-resets it, which drops the rest of
   * the round; it takes about 0.2 ms, keeps the calibration, and leaves
   * the FIFO empty with its interrupts masked. The setup passed at
   * initialization, so it cannot fail here.
   */

  bflb_adc_deinit(priv->adc);
  DEBUGVERIFY(bl616cl_adc_configure(priv));
  priv->next = 0;

  /* Keep FIFO_RDY and the overrun flag unmasked in the GPADC for the whole
   * scan: a masked flag reads as 0, and the worker needs the overrun flag.
   * The interrupt handler disables the IRQ at the interrupt controller
   * instead.
   */

  bflb_adc_int_mask(priv->adc, ADC_INT_REGULAR_FIFO_RDY |
                    ADC_INT_REGULAR_FIFO_OVERRUN, false);
  up_enable_irq(BL616CL_IRQ_NUM_GPADC_DMA);
  bflb_adc_start_conversion(priv->adc);

  return OK;
}

/****************************************************************************
 * Name: bl616cl_adc_worker
 *
 * Description:
 *   Drain the FIFO after FIFO_RDY or an overrun, queue the samples and
 *   enable the IRQ again. If samples were lost, in the FIFO or because the
 *   upper-half queue is full, stop the scan and report an overrun: read()
 *   returns the samples queued before the loss, then fails once with EIO.
 *
 * Input Parameters:
 *   arg - ADC device
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_adc_worker(void *arg)
{
  struct adc_dev_s *dev = arg;
  struct bl616cl_adc_priv_s *priv = dev->ad_priv;
  uint32_t raw[BL616CL_ADC_FIFO_DEPTH];
  uint8_t channel[BL616CL_ADC_FIFO_DEPTH];
  uint32_t data[BL616CL_ADC_FIFO_DEPTH];
  const char *loss = NULL;
  irqstate_t flags;
  uint32_t status;
  unsigned int count;
  unsigned int valid;
  unsigned int i;

  count = bflb_adc_get_count(priv->adc);
  if (count > BL616CL_ADC_FIFO_DEPTH)
    {
      count = BL616CL_ADC_FIFO_DEPTH;
    }

  for (i = 0; i < count; i++)
    {
      raw[i] = bflb_adc_read_raw(priv->adc);
    }

  /* Acknowledge FIFO_RDY only. The overrun flag stays set until a stop
   * reports it, and is read after the drain: it then covers an overrun
   * while this worker waited to run, and one after this read raises the
   * IRQ again. The LHAL returns every flag of the FIFO, FIFO_RDY too,
   * so test the overrun bit. The FIFO keeps the samples taken before an
   * overrun, in order; the lost ones would have followed them.
   */

  bflb_adc_int_clear(priv->adc, ADC_INT_REGULAR_FIFO_RDY);

  status = bflb_adc_get_int_status(priv->adc, ADC_INT_REGULAR_FIFO);
  valid = bl616cl_adc_convert(priv, raw, count, channel, data);
  if ((status & ADC_INT_REGULAR_FIFO_OVERRUN) != 0)
    {
      loss = "FIFO overrun";
    }
  else if (valid < count)
    {
      loss = "sample out of scan order";
    }
  else if (bl616cl_adc_queue(dev, channel, data, valid) < 0)
    {
      loss = "queue full";
      valid = 0;
    }
  else
    {
      flags = spin_lock_irqsave(&dev->ad_spinlock);
      if (priv->state == BL616CL_ADC_RUNNING)
        {
          up_enable_irq(BL616CL_IRQ_NUM_GPADC_DMA);
        }

      spin_unlock_irqrestore(&dev->ad_spinlock, flags);
      return;
    }

  /* Stop the scan, unless the last close is stopping it already. Until
   * the overrun is reported, ANIOC_TRIGGER cannot start a scan that this
   * stop would end.
   */

  flags = spin_lock_irqsave(&dev->ad_spinlock);
  if (priv->state != BL616CL_ADC_RUNNING)
    {
      spin_unlock_irqrestore(&dev->ad_spinlock, flags);
      return;
    }

  priv->state = BL616CL_ADC_STOPPING;
  spin_unlock_irqrestore(&dev->ad_spinlock, flags);

  awarn("WARNING: ADC scan stopped, %s\n", loss);
  bl616cl_adc_stop(priv);

  /* Mark the overrun, allow the next start and queue the samples taken
   * before the loss in one section, so that a reader finds all of it or
   * none: EIO comes after these samples, and a reader woken by them can
   * start again at once. When they do not fit, an empty batch still wakes
   * the readers.
   */

  flags = spin_lock_irqsave(&dev->ad_spinlock);
  priv->cb->au_reset(dev);
  priv->state = BL616CL_ADC_IDLE;
  if (priv->cb->au_receive_batch(dev, channel, data, valid) < 0)
    {
      priv->cb->au_receive_batch(dev, channel, data, 0);
    }

  spin_unlock_irqrestore(&dev->ad_spinlock, flags);
}

/****************************************************************************
 * Name: bl616cl_adc_interrupt
 *
 * Description:
 *   Handle FIFO_RDY and FIFO overrun. Disable the IRQ at the interrupt
 *   controller and leave the FIFO to the worker.
 *
 * Input Parameters:
 *   irq     - IRQ number
 *   context - Interrupt context (unused)
 *   arg     - ADC device
 *
 * Returned Value:
 *   Zero (OK).
 *
 ****************************************************************************/

static int bl616cl_adc_interrupt(int irq, void *context, void *arg)
{
  struct adc_dev_s *dev = arg;
  struct bl616cl_adc_priv_s *priv = dev->ad_priv;

  up_disable_irq(irq);
  work_queue(HPWORK, &priv->work, bl616cl_adc_worker, dev, 0);
  return OK;
}
#endif /* CONFIG_BL616CL_ADC_IRQ */

/****************************************************************************
 * Name: bl616cl_adc_bind
 *
 * Description:
 *   Bind the upper-half callbacks (adc_ops_s ao_bind).
 *
 * Input Parameters:
 *   dev      - ADC device
 *   callback - Upper-half callbacks
 *
 * Returned Value:
 *   Zero (OK).
 *
 ****************************************************************************/

static int bl616cl_adc_bind(struct adc_dev_s *dev,
                            const struct adc_callback_s *callback)
{
  struct bl616cl_adc_priv_s *priv = dev->ad_priv;

  DEBUGASSERT(callback != NULL);
  priv->cb = callback;
  return OK;
}

/****************************************************************************
 * Name: bl616cl_adc_reset
 *
 * Description:
 *   Stop conversions and empty the FIFO (adc_ops_s ao_reset). The GPADC
 *   has been set up by bl616cl_adc_initialize().
 *
 * Input Parameters:
 *   dev - ADC device
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_adc_reset(struct adc_dev_s *dev)
{
  bl616cl_adc_halt(dev->ad_priv);
}

/****************************************************************************
 * Name: bl616cl_adc_setup
 *
 * Description:
 *   Prepare for the first open (adc_ops_s ao_setup). Clock, calibration
 *   and the scan table are set up once by bl616cl_adc_initialize() and
 *   kept, so there is nothing to do.
 *
 * Input Parameters:
 *   dev - ADC device
 *
 * Returned Value:
 *   Zero (OK).
 *
 ****************************************************************************/

static int bl616cl_adc_setup(struct adc_dev_s *dev)
{
  return OK;
}

/****************************************************************************
 * Name: bl616cl_adc_shutdown
 *
 * Description:
 *   Stop conversions after the last close (adc_ops_s ao_shutdown). Clock
 *   and calibration are kept for the next open.
 *
 * Input Parameters:
 *   dev - ADC device
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_adc_shutdown(struct adc_dev_s *dev)
{
  struct bl616cl_adc_priv_s *priv = dev->ad_priv;
#ifdef CONFIG_BL616CL_ADC_IRQ
  irqstate_t flags;

  /* Keep the worker from enabling the IRQ or stopping the scan itself,
   * let a running worker finish with the FIFO, then stop the GPADC.
   */

  flags = spin_lock_irqsave(&dev->ad_spinlock);
  priv->state = BL616CL_ADC_STOPPING;
  up_disable_irq(BL616CL_IRQ_NUM_GPADC_DMA);
  spin_unlock_irqrestore(&dev->ad_spinlock, flags);

  work_cancel_sync(HPWORK, &priv->work);
  bl616cl_adc_stop(priv);

  flags = spin_lock_irqsave(&dev->ad_spinlock);
  priv->state = BL616CL_ADC_IDLE;
  spin_unlock_irqrestore(&dev->ad_spinlock, flags);
#else
  bl616cl_adc_halt(priv);
#endif
}

/****************************************************************************
 * Name: bl616cl_adc_rxint
 *
 * Description:
 *   Enable or disable receive interrupts (adc_ops_s ao_rxint). The upper
 *   half calls this only to enable them after the first open; conversions
 *   and their interrupt start with ANIOC_TRIGGER instead.
 *
 * Input Parameters:
 *   dev    - ADC device
 *   enable - Requested state (unused)
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_adc_rxint(struct adc_dev_s *dev, bool enable)
{
}

/****************************************************************************
 * Name: bl616cl_adc_ioctl
 *
 * Description:
 *   Handle the ADC ioctl commands the upper half passes down (adc_ops_s
 *   ao_ioctl).
 *
 * Input Parameters:
 *   dev - ADC device
 *   cmd - ANIOC_TRIGGER or ANIOC_GET_NCHANNELS
 *   arg - Unused
 *
 * Returned Value:
 *   ANIOC_TRIGGER: zero (OK) on success; the errors of bl616cl_adc_scan()
 *   when polling; -EBUSY if a continuous scan is running or being stopped.
 *   ANIOC_GET_NCHANNELS: the number of scanned channels.
 *   -ENOTTY for any other command.
 *
 ****************************************************************************/

static int bl616cl_adc_ioctl(struct adc_dev_s *dev, int cmd,
                             unsigned long arg)
{
  struct bl616cl_adc_priv_s *priv = dev->ad_priv;

  switch (cmd)
    {
      case ANIOC_TRIGGER:
#ifdef CONFIG_BL616CL_ADC_POLL
        return bl616cl_adc_scan(dev);
#else
        return bl616cl_adc_start(dev);
#endif

      case ANIOC_GET_NCHANNELS:
        return priv->nchannels;

      default:
        return -ENOTTY;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_adc_initialize
 *
 * Description:
 *   Set up the GPADC to scan the given external channels. See the header
 *   file for the detailed contract.
 *
 ****************************************************************************/

struct adc_dev_s *bl616cl_adc_initialize(const uint8_t *channels,
                                         unsigned int nchannels)
{
  struct bl616cl_adc_priv_s *priv = &g_bl616cl_adc_priv;
  struct bflb_device_s *gpio;
  uint16_t used = 0;
  unsigned int i;
  int ret;

  if (channels == NULL || nchannels == 0 ||
      nchannels > BL616CL_ADC_NCHANNELS)
    {
      return NULL;
    }

  for (i = 0; i < nchannels; i++)
    {
      if (channels[i] >= BL616CL_ADC_NCHANNELS ||
          (used & (1 << channels[i])) != 0)
        {
          return NULL;
        }

      used |= 1 << channels[i];
      priv->channels[i] = channels[i];
    }

  priv->nchannels = nchannels;
  priv->adc = bflb_device_get_by_name(BFLB_NAME_ADC_V2_0);
  gpio = bflb_device_get_by_name(BFLB_NAME_GPIO);
  if (priv->adc == NULL || gpio == NULL)
    {
      return NULL;
    }

  for (i = 0; i < nchannels; i++)
    {
      bflb_gpio_init(gpio, g_bl616cl_adc_pins[channels[i]],
                     BL616CL_ADC_GPIO_CFG);
    }

  GLB_Set_ADC_CLK(ENABLE, GLB_ADC_CLK_XCLK, CONFIG_BL616CL_ADC_CLK_DIV - 1);
  bflb_peripheral_clock_control(BFLB_PERIPHERAL_GPADC0_P0, true);

  ret = bl616cl_adc_configure(priv);
  if (ret < 0)
    {
      aerr("ERROR: GPADC setup failed: %d\n", ret);
      return NULL;
    }

#ifdef CONFIG_BL616CL_ADC_IRQ
  /* The IRQ stays disabled until ANIOC_TRIGGER starts a scan */

  ret = irq_attach(BL616CL_IRQ_NUM_GPADC_DMA, bl616cl_adc_interrupt,
                   &g_bl616cl_adc_dev);
  if (ret < 0)
    {
      aerr("ERROR: GPADC IRQ attach failed: %d\n", ret);
      return NULL;
    }
#endif

  return &g_bl616cl_adc_dev;
}
