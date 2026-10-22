/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_i2c.c
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

#include <debug.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/param.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>

#include <arch/irq.h>

#include "bl616cl_sdk.h"
#include "bl616cl_glb.h"
#include "bl616cl_pm.h"
#include "bflb_clock.h"
#include "bflb_gpio.h"
#include "bflb_i2c.h"
#include "bflb_name.h"
#include "bflb_peri.h"
#include "bl616cl_i2c.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_I2C_DEFAULT_FREQUENCY 100000
#define BL616CL_I2C_FAST_FREQUENCY    400000
#define BL616CL_I2C_MAX_LENGTH        1024
#define BL616CL_I2C_MAX_PREFIX        16

#ifndef CONFIG_I2C_POLLED
/* Fixed margin of a group timeout, on top of twice the bus time */

#define BL616CL_I2C_TIMEOUT_MS        100

/* Interrupts that end a group. INTEN and INTSTS use the same bits for
 * these and for the FIFO interrupts.
 */

#define BL616CL_I2C_INT_ERRORS \
  (I2C_INTEN_NACK | I2C_INTEN_ARB | I2C_INTEN_FER | I2C_INTEN_TIMEOUT)
#define BL616CL_I2C_INT_EVENTS (I2C_INTEN_END | BL616CL_I2C_INT_ERRORS)
#endif

#define BL616CL_I2C_GPIO_CFG(function) \
  ((function) | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bl616cl_i2c_priv_s
{
  const struct i2c_ops_s *ops;
  mutex_t lock;
  struct bflb_device_s *dev;
  uint16_t refs;
  uint8_t port;
  uint8_t scl_pin;
  uint8_t sda_pin;
#ifndef CONFIG_I2C_POLLED
  sem_t done;               /* Posted by the ISR when a group ends */
  struct i2c_msg_s *msgs;   /* Running group; the last message is the data */
  uint32_t intmask;         /* Interrupts unmasked for the running group */
  uint32_t status;          /* Raw interrupt status when the group ended */
  int result;               /* Result of the last group */
  uint16_t txlen;           /* Write stream length: prefix, then data */
  uint16_t txpos;           /* Write stream bytes queued in the TX FIFO */
  uint16_t rxpos;           /* Data bytes read from the RX FIFO */
  uint8_t count;            /* Messages in the running group (1 or 2) */
  uint8_t prefix;           /* Prefix (sub-address) bytes before the data */
  uint8_t irq;
  bool busy;                /* A group is running and owns the ISR */
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bl616cl_i2c_transfer(struct i2c_master_s *dev,
                                struct i2c_msg_s *msgs, int count);

#ifdef CONFIG_I2C_RESET
/****************************************************************************
 * Name: bl616cl_i2c_reset
 *
 * Description:
 *   Implement the I2C reset callback (i2c_ops_s reset). Bus reset is not
 *   supported by this driver.
 *
 * Input Parameters:
 *   dev - Device structure as returned by bl616cl_i2cbus_initialize()
 *
 * Returned Value:
 *   -EOPNOTSUPP always.
 *
 ****************************************************************************/

static int bl616cl_i2c_reset(struct i2c_master_s *dev)
{
  (void)dev;
  return -EOPNOTSUPP;
}
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct i2c_ops_s g_bl616cl_i2c_ops =
{
  .transfer = bl616cl_i2c_transfer,
#ifdef CONFIG_I2C_RESET
  .reset = bl616cl_i2c_reset,
#endif
  .setup = NULL,
  .shutdown = NULL,
};

#ifdef CONFIG_BL616CL_I2C0
static struct bl616cl_i2c_priv_s g_bl616cl_i2c0 =
{
  .ops = &g_bl616cl_i2c_ops,
  .lock = NXMUTEX_INITIALIZER,
  .port = 0,
#ifndef CONFIG_I2C_POLLED
  .done = SEM_INITIALIZER(0),
  .irq = BL616CL_IRQ_NUM_I2C0,
#endif
};
#endif

#ifdef CONFIG_BL616CL_I2C1
static struct bl616cl_i2c_priv_s g_bl616cl_i2c1 =
{
  .ops = &g_bl616cl_i2c_ops,
  .lock = NXMUTEX_INITIALIZER,
  .port = 1,
#ifndef CONFIG_I2C_POLLED
  .done = SEM_INITIALIZER(0),
  .irq = BL616CL_IRQ_NUM_I2C1,
#endif
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_i2c_priv
 *
 * Description:
 *   Map an I2C port number to its statically allocated private state. Only
 *   ports enabled in Kconfig are available.
 *
 * Input Parameters:
 *   port - I2C port number (0 or 1)
 *
 * Returned Value:
 *   A pointer to the port private state; NULL if the port is unknown or not
 *   enabled.
 *
 ****************************************************************************/

static struct bl616cl_i2c_priv_s *bl616cl_i2c_priv(int port)
{
  switch (port)
    {
#ifdef CONFIG_BL616CL_I2C0
      case 0:
        return &g_bl616cl_i2c0;
#endif
#ifdef CONFIG_BL616CL_I2C1
      case 1:
        return &g_bl616cl_i2c1;
#endif
      default:
        return NULL;
    }
}

/****************************************************************************
 * Name: bl616cl_i2c_frequency_valid
 *
 * Description:
 *   Check that a requested bus frequency is one of the supported rates (100
 *   kHz or 400 kHz).
 *
 * Input Parameters:
 *   frequency - Requested bus frequency in Hz
 *
 * Returned Value:
 *   true if the frequency is supported; false otherwise.
 *
 ****************************************************************************/

static bool bl616cl_i2c_frequency_valid(uint32_t frequency)
{
  return frequency == BL616CL_I2C_DEFAULT_FREQUENCY ||
         frequency == BL616CL_I2C_FAST_FREQUENCY;
}

/****************************************************************************
 * Name: bl616cl_i2c_group_count
 *
 * Description:
 *   Return how many messages starting at index i go out as one LHAL
 *   transfer: two when msgs[i] has NOSTOP or the next message has NOSTART,
 *   otherwise one.
 *
 * Input Parameters:
 *   msgs  - Array of I2C messages
 *   count - Number of messages in the array
 *   i     - Index of the first message of the group
 *
 * Returned Value:
 *   1 or 2.
 *
 ****************************************************************************/

static int bl616cl_i2c_group_count(const struct i2c_msg_s *msgs, int count,
                                   int i)
{
  if ((msgs[i].flags & I2C_M_NOSTOP) != 0 ||
      (i + 1 < count && (msgs[i + 1].flags & I2C_M_NOSTART) != 0))
    {
      return 2;
    }

  return 1;
}

/****************************************************************************
 * Name: bl616cl_i2c_validate
 *
 * Description:
 *   Validate a message chain before any hardware access. Checks buffers,
 *   length limits, 7-bit addresses, frequency and flags. Besides single
 *   messages it accepts two kinds of pairs on the same address and
 *   frequency, both starting with a write (prefix): a NOSTOP prefix followed
 *   by a read (repeated start), and a prefix followed by a NOSTART write
 *   (continuation of the same write).
 *
 * Input Parameters:
 *   msgs  - Array of I2C messages to validate
 *   count - Number of messages in the array
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. Errors: -EINVAL
 *   for malformed messages or a pair with different address or frequency;
 *   -EOPNOTSUPP for 10-bit addressing, an unsupported NOSTOP/NOSTART usage
 *   or, in interrupt mode, a continued write longer than 1024 bytes with
 *   its prefix.
 *
 ****************************************************************************/

static int bl616cl_i2c_validate(struct i2c_msg_s *msgs, int count)
{
  const uint16_t allowed = I2C_M_READ | I2C_M_TEN | I2C_M_NOSTOP |
                           I2C_M_NOSTART;
  int i;

  if (msgs == NULL || count <= 0)
    {
      return -EINVAL;
    }

  /* Validate every descriptor before classifying message-chain semantics. */

  for (i = 0; i < count; i++)
    {
      if (msgs[i].buffer == NULL || msgs[i].length <= 0 ||
          msgs[i].length > BL616CL_I2C_MAX_LENGTH ||
          msgs[i].addr > 0x7f ||
          !bl616cl_i2c_frequency_valid(msgs[i].frequency) ||
          (msgs[i].flags & ~allowed) != 0)
        {
          return -EINVAL;
        }
    }

  for (i = 0; i < count; i++)
    {
      if ((msgs[i].flags & (I2C_M_TEN | I2C_M_NOSTART)) != 0)
        {
          return -EOPNOTSUPP;
        }

      if (bl616cl_i2c_group_count(msgs, count, i) == 2)
        {
          /* The prefix becomes the LHAL sub-address, so the pair is either
           * prefix + repeated-start read or prefix + continued write.
           */

          if ((msgs[i].flags & I2C_M_READ) != 0 ||
              msgs[i].length > BL616CL_I2C_MAX_PREFIX ||
              i + 1 >= count ||
              (msgs[i + 1].flags != I2C_M_READ &&
               msgs[i + 1].flags != I2C_M_NOSTART))
            {
              return -EOPNOTSUPP;
            }

          if (msgs[i + 1].addr != msgs[i].addr ||
              msgs[i + 1].frequency != msgs[i].frequency)
            {
              return -EINVAL;
            }

#ifndef CONFIG_I2C_POLLED
          /* A continued write goes out as one packet of prefix and data */

          if (msgs[i + 1].flags == I2C_M_NOSTART &&
              msgs[i].length + msgs[i + 1].length > BL616CL_I2C_MAX_LENGTH)
            {
              return -EOPNOTSUPP;
            }
#endif

          i++;
        }
    }

  return OK;
}

#ifdef CONFIG_I2C_POLLED
/****************************************************************************
 * Name: bl616cl_i2c_transport_transfer
 *
 * Description:
 *   Run one LHAL transfer group with bflb_i2c_transfer(). An LHAL timeout is
 *   mapped to -ETIMEDOUT.
 *
 * Input Parameters:
 *   priv  - I2C port private state
 *   msgs  - LHAL message array (at most two entries)
 *   count - Number of messages in the array
 *
 * Returned Value:
 *   The value returned by the transfer on success; a negated errno value on
 *   failure, including -ETIMEDOUT on timeout.
 *
 ****************************************************************************/

static int bl616cl_i2c_transport_transfer(
  struct bl616cl_i2c_priv_s *priv, struct bflb_i2c_msg_s *msgs, int count)
{
  int ret;

  ret = bflb_i2c_transfer(priv->dev, msgs, count);
  if (ret == -BL616CL_LHAL_ETIMEDOUT)
    {
      return -ETIMEDOUT;
    }

  return ret;
}

/****************************************************************************
 * Name: bl616cl_i2c_transport_failed
 *
 * Description:
 *   Handle a transport failure. Log the interrupt status, de-initialize the
 *   controller and pass the error through.
 *
 * Input Parameters:
 *   priv      - I2C port private state
 *   operation - Name of the failed step, used in the log
 *   ret       - Negated errno value of the failure
 *
 * Returned Value:
 *   The unchanged ret value.
 *
 ****************************************************************************/

static int bl616cl_i2c_transport_failed(struct bl616cl_i2c_priv_s *priv,
                                        const char *operation, int ret)
{
  uint32_t status = bflb_i2c_get_intstatus(priv->dev);

  i2cerr("ERROR: I2C%d %s failed: ret=%d status=%08" PRIx32 "\n",
         priv->port, operation, ret, status);
  bflb_i2c_deinit(priv->dev);
  return ret;
}

/****************************************************************************
 * Name: bl616cl_i2c_call
 *
 * Description:
 *   Convert one group of one or two NuttX messages to LHAL messages, set the
 *   bus frequency of the first message and run the transfer. In a pair the
 *   first message is passed as an LHAL NOSTOP sub-address; LHAL sends a
 *   repeated start before a read and continues a write without one. Any
 *   failure cleans up the controller.
 *
 * Input Parameters:
 *   priv  - I2C port private state
 *   msgs  - Messages of the group
 *   count - Number of messages in the group (1 or 2)
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int bl616cl_i2c_call(struct bl616cl_i2c_priv_s *priv,
                            struct i2c_msg_s *msgs, int count)
{
  struct bflb_i2c_msg_s lhal[2];
  int ret;
  int i;

  for (i = 0; i < count; i++)
    {
      lhal[i].addr = msgs[i].addr;
      lhal[i].flags = 0;
      if ((msgs[i].flags & I2C_M_READ) != 0)
        {
          lhal[i].flags |= I2C_M_READ;
        }

      if (i == 0 && count == 2)
        {
          lhal[i].flags |= I2C_M_NOSTOP;
        }

      lhal[i].buffer = msgs[i].buffer;
      lhal[i].length = msgs[i].length;
    }

  bflb_i2c_init(priv->dev, msgs[0].frequency);
  ret = bl616cl_i2c_transport_transfer(priv, lhal, count);
  if (ret < 0)
    {
      return bl616cl_i2c_transport_failed(priv, "transfer", ret);
    }

  return ret;
}
#else
/****************************************************************************
 * Name: bl616cl_i2c_timeout
 *
 * Description:
 *   Return how long a group may take: a fixed margin plus twice its
 *   nominal bus time (9 bits per byte, one address byte per message).
 *
 * Input Parameters:
 *   msgs  - Messages of the group
 *   count - Number of messages in the group (1 or 2)
 *
 * Returned Value:
 *   The timeout in system ticks.
 *
 ****************************************************************************/

static clock_t bl616cl_i2c_timeout(const struct i2c_msg_s *msgs, int count)
{
  uint64_t bytes = count;
  int i;

  for (i = 0; i < count; i++)
    {
      bytes += msgs[i].length;
    }

  return MSEC2TICK(BL616CL_I2C_TIMEOUT_MS) +
         USEC2TICK(bytes * 9 * 2 * USEC_PER_SEC / msgs[0].frequency) + 1;
}

/****************************************************************************
 * Name: bl616cl_i2c_fill_tx
 *
 * Description:
 *   Queue as much of the write stream (the prefix, then the data message;
 *   only the sub-address for a prefix + read) as fits in the TX FIFO, and
 *   mask the TX FIFO interrupt once all of it is queued; a prefix + read
 *   then continues on the RX FIFO interrupt. The FIFO takes 32-bit words:
 *   the words that hold prefix bytes are assembled here, the rest is
 *   written from the data buffer.
 *
 * Input Parameters:
 *   priv - I2C port private state
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_i2c_fill_tx(struct bl616cl_i2c_priv_s *priv)
{
  uint8_t *prefix = priv->msgs[0].buffer;
  uint8_t *data = priv->msgs[priv->count - 1].buffer;

  while (priv->txpos < priv->prefix)
    {
      uint8_t word[4];
      uint32_t n = MIN(4, priv->txlen - priv->txpos);
      uint32_t i;

      for (i = 0; i < n; i++)
        {
          uint32_t pos = priv->txpos + i;

          word[i] = pos < priv->prefix ? prefix[pos] :
                                         data[pos - priv->prefix];
        }

      if (bflb_i2c_fifo_write(priv->dev, word, n) == 0)
        {
          return;
        }

      priv->txpos += n;
    }

  if (priv->txpos < priv->txlen)
    {
      priv->txpos += bflb_i2c_fifo_write(priv->dev,
                                         data + priv->txpos - priv->prefix,
                                         priv->txlen - priv->txpos);
    }

  if (priv->txpos >= priv->txlen)
    {
      priv->intmask &= ~I2C_INTEN_TX_FIFO;
      bflb_i2c_int_mask(priv->dev, I2C_INTEN_TX_FIFO, true);

      if ((priv->msgs[priv->count - 1].flags & I2C_M_READ) != 0)
        {
          priv->intmask |= I2C_INTEN_RX_FIFO;
          bflb_i2c_int_mask(priv->dev, I2C_INTEN_RX_FIFO, false);
        }
    }
}

/****************************************************************************
 * Name: bl616cl_i2c_finish
 *
 * Description:
 *   End the running group: record the raw status and the result, stop the
 *   controller (this also clears the FIFOs and the status and masks every
 *   interrupt) and wake the waiting thread. Called from the ISR.
 *
 * Input Parameters:
 *   priv   - I2C port private state
 *   result - Result of the group
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_i2c_finish(struct bl616cl_i2c_priv_s *priv, int result)
{
  priv->status = bflb_i2c_get_intstatus(priv->dev);
  bflb_i2c_deinit(priv->dev);
  priv->result = result;
  priv->busy = false;
  nxsem_post(&priv->done);
}

/****************************************************************************
 * Name: bl616cl_i2c_interrupt
 *
 * Description:
 *   I2C interrupt handler. Moves the write stream or the read data between
 *   the buffers and the FIFOs, then ends the group on END or on an error.
 *   The raw status always shows TX FIFO ready, so only the interrupts
 *   unmasked for the group count.
 *   Any NACK, arbitration loss, FIFO error or master timeout fails the group
 *   with -ETIMEDOUT.
 *
 * Input Parameters:
 *   irq     - IRQ number (unused)
 *   context - Interrupt context (unused)
 *   arg     - I2C port private state
 *
 * Returned Value:
 *   Zero (OK) always.
 *
 ****************************************************************************/

static int bl616cl_i2c_interrupt(int irq, void *context, void *arg)
{
  struct bl616cl_i2c_priv_s *priv = arg;
  struct i2c_msg_s *msg;
  uint32_t status;

  if (!priv->busy)
    {
      bflb_i2c_int_mask(priv->dev, I2C_INTEN_ALL, true);
      return OK;
    }

  msg = &priv->msgs[priv->count - 1];
  status = bflb_i2c_get_intstatus(priv->dev) & priv->intmask;

  if ((status & I2C_INTSTS_TX_FIFO) != 0)
    {
      bl616cl_i2c_fill_tx(priv);
    }

  /* The last bytes may still be in the RX FIFO when END comes */

  if ((status & (I2C_INTSTS_RX_FIFO | I2C_INTSTS_END)) != 0 &&
      (msg->flags & I2C_M_READ) != 0)
    {
      priv->rxpos += bflb_i2c_fifo_read(priv->dev,
                                        msg->buffer + priv->rxpos,
                                        msg->length - priv->rxpos);
      if (priv->rxpos >= msg->length)
        {
          priv->intmask &= ~I2C_INTEN_RX_FIFO;
          bflb_i2c_int_mask(priv->dev, I2C_INTEN_RX_FIFO, true);
        }
    }

  if ((status & BL616CL_I2C_INT_ERRORS) != 0)
    {
      bl616cl_i2c_finish(priv, -ETIMEDOUT);
    }
  else if ((status & I2C_INTSTS_END) != 0)
    {
      bool done = (msg->flags & I2C_M_READ) != 0 ?
                  priv->rxpos >= msg->length : priv->txpos >= priv->txlen;

      bl616cl_i2c_finish(priv, done ? OK : -ETIMEDOUT);
    }

  return OK;
}

/****************************************************************************
 * Name: bl616cl_i2c_call
 *
 * Description:
 *   Run one group of one or two NuttX messages in LHAL interrupt mode. A
 *   prefix + read pair passes the prefix as an LHAL NOSTOP sub-address
 *   (repeated start before the read). A write, with or without a prefix,
 *   goes out as one packet whose data is the prefix followed by the write.
 *   LHAL configures the packets, unmasks the FIFO interrupt for the
 *   direction of the first message (TX unless the group is a plain read)
 *   and starts; the ISR queues the sub-address of a prefix + read and
 *   switches it to the RX FIFO, moves the data and wakes the thread. On
 *   timeout the controller is stopped here.
 *
 * Input Parameters:
 *   priv  - I2C port private state
 *   msgs  - Messages of the group
 *   count - Number of messages in the group (1 or 2)
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure: -ETIMEDOUT on
 *   NACK, arbitration loss, FIFO error or timeout.
 *
 ****************************************************************************/

static int bl616cl_i2c_call(struct bl616cl_i2c_priv_s *priv,
                            struct i2c_msg_s *msgs, int count)
{
  struct bflb_i2c_msg_s lhal[2];
  irqstate_t flags;
  bool read = (msgs[count - 1].flags & I2C_M_READ) != 0;
  int lcount = 1;
  int ret;

  /* With a sub-address the controller fetches the write data right after
   * it, and an empty TX FIFO then is an underflow (FER) that can put stale
   * bytes on the bus. A single packet does not start its data before the
   * first word is queued, so a write never uses the sub-address.
   */

  lhal[0].addr = msgs[0].addr;
  lhal[0].buffer = msgs[0].buffer;
  lhal[0].length = msgs[0].length;
  if (read && count == 2)
    {
      lhal[0].flags = I2C_M_NOSTOP;
      lhal[1].addr = msgs[1].addr;
      lhal[1].flags = I2C_M_READ | I2C_M_INT;
      lhal[1].buffer = msgs[1].buffer;
      lhal[1].length = msgs[1].length;
      lcount = 2;
    }
  else
    {
      lhal[0].flags = (read ? I2C_M_READ : 0) | I2C_M_INT;
      if (count == 2)
        {
          lhal[0].length += msgs[1].length;
        }
    }

  bflb_i2c_init(priv->dev, msgs[0].frequency);
  nxsem_reset(&priv->done, 0);
  priv->msgs = msgs;
  priv->count = count;
  priv->prefix = count == 2 ? msgs[0].length : 0;
  priv->txlen = read ? priv->prefix : lhal[0].length;
  priv->txpos = 0;
  priv->rxpos = 0;
  priv->intmask = BL616CL_I2C_INT_EVENTS |
                  (priv->txlen == 0 ? I2C_INTEN_RX_FIFO : I2C_INTEN_TX_FIFO);
  priv->busy = true;

  /* LHAL unmasks the FIFO interrupt and starts the controller; the ISR may
   * already queue the write before this returns.
   */

  ret = bflb_i2c_transfer(priv->dev, lhal, lcount);

  /* bflb_i2c_int_mask() is a read-modify-write, and so is the ISR's */

  flags = enter_critical_section();
  if (ret < 0)
    {
      priv->status = bflb_i2c_get_intstatus(priv->dev);
      bflb_i2c_deinit(priv->dev);
      priv->busy = false;
    }
  else
    {
      bflb_i2c_int_mask(priv->dev, BL616CL_I2C_INT_EVENTS, false);
    }

  leave_critical_section(flags);

  if (ret >= 0)
    {
      nxsem_tickwait_uninterruptible(&priv->done,
                                     bl616cl_i2c_timeout(msgs, count));

      flags = enter_critical_section();
      if (priv->busy)
        {
          priv->status = bflb_i2c_get_intstatus(priv->dev);
          bflb_i2c_deinit(priv->dev);
          priv->busy = false;
          priv->result = -ETIMEDOUT;
        }

      ret = priv->result;
      leave_critical_section(flags);
    }

  if (ret < 0)
    {
      i2cerr("ERROR: I2C%d transfer failed: ret=%d status=%08" PRIx32 "\n",
             priv->port, ret, priv->status);
    }

  return ret;
}
#endif

/****************************************************************************
 * Name: bl616cl_i2c_transfer
 *
 * Description:
 *   Implement the i2c_ops_s transfer method. Validates the chain, takes the
 *   port lock and runs it in groups (a prefix write plus its read or NOSTART
 *   write count as one group).
 *
 * Input Parameters:
 *   dev   - Device structure as returned by bl616cl_i2cbus_initialize()
 *   msgs  - Array of I2C messages
 *   count - Number of messages in the array
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. Errors: -EINVAL
 *   or -EOPNOTSUPP from validation; -ENODEV if the port is not initialized;
 *   -ETIMEDOUT on NACK, arbitration loss, FIFO error or timeout.
 *
 ****************************************************************************/

static int bl616cl_i2c_transfer(struct i2c_master_s *dev,
                                struct i2c_msg_s *msgs, int count)
{
  struct bl616cl_i2c_priv_s *priv = (struct bl616cl_i2c_priv_s *)dev;
  int group_count;
  int ret;
  int i;

  ret = bl616cl_i2c_validate(msgs, count);
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->refs == 0)
    {
      ret = -ENODEV;
      goto out;
    }

  for (i = 0; i < count; i += group_count)
    {
      group_count = bl616cl_i2c_group_count(msgs, count, i);
      ret = bl616cl_i2c_call(priv, &msgs[i], group_count);
      if (ret < 0)
        {
          goto out;
        }
    }

out:
  nxmutex_unlock(&priv->lock);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_i2cbus_initialize
 *
 * Description:
 *   Initialize an I2C port. The first call sets up clocks and the SCL/SDA
 *   pins, starts the controller at 100 kHz and, in interrupt mode, attaches
 *   its interrupt. Later calls with the same pins only take a reference.
 *
 * Input Parameters:
 *   port    - I2C port number (0 or 1)
 *   scl_pin - SCL GPIO number (must be even)
 *   sda_pin - SDA GPIO number (must be odd, different from scl_pin)
 *
 * Returned Value:
 *   An I2C master handle on success; NULL if the pins or port are invalid,
 *   the port is not enabled, the pins differ from an existing user, a device
 *   lookup fails or the interrupt cannot be attached.
 *
 ****************************************************************************/

struct i2c_master_s *bl616cl_i2cbus_initialize(int port, uint8_t scl_pin,
                                                uint8_t sda_pin)
{
  struct bl616cl_i2c_priv_s *priv;
  struct bflb_device_s *gpio;
  uint32_t function;
  int peripheral;

  if (scl_pin >= GPIO_PIN_MAX || sda_pin >= GPIO_PIN_MAX ||
      scl_pin == sda_pin || (scl_pin & 1) != 0 || (sda_pin & 1) == 0)
    {
      return NULL;
    }

  priv = bl616cl_i2c_priv(port);
  if (priv == NULL)
    {
      return NULL;
    }

  nxmutex_lock(&priv->lock);
  if (priv->refs > 0)
    {
      if (priv->scl_pin != scl_pin || priv->sda_pin != sda_pin)
        {
          nxmutex_unlock(&priv->lock);
          return NULL;
        }

      priv->refs++;
      nxmutex_unlock(&priv->lock);
      return (struct i2c_master_s *)priv;
    }

  gpio = bflb_device_get_by_name(BFLB_NAME_GPIO);
  priv->dev = bflb_device_get_by_name(port == 0 ? BFLB_NAME_I2C0 :
                                                  BFLB_NAME_I2C1);
  if (gpio == NULL || priv->dev == NULL)
    {
      priv->dev = NULL;
      nxmutex_unlock(&priv->lock);
      return NULL;
    }

  peripheral = port == 0 ? BFLB_PERIPHERAL_I2C0 : BFLB_PERIPHERAL_I2C1;
  function = port == 0 ? GPIO_FUNC_I2C0 : GPIO_FUNC_I2C1;

  GLB_Set_I2C_CLK(ENABLE, GLB_I2C_CLK_XCLK, 0);
  bflb_peripheral_clock_control(peripheral, true);
  pm_disable_gpio_keep(scl_pin);
  pm_disable_gpio_keep(sda_pin);
  bflb_gpio_init(gpio, scl_pin, BL616CL_I2C_GPIO_CFG(function));
  bflb_gpio_init(gpio, sda_pin, BL616CL_I2C_GPIO_CFG(function));
  bflb_i2c_init(priv->dev, BL616CL_I2C_DEFAULT_FREQUENCY);

#ifndef CONFIG_I2C_POLLED
  /* bflb_i2c_init() masks every I2C interrupt; each group unmasks its own */

  if (irq_attach(priv->irq, bl616cl_i2c_interrupt, priv) < 0)
    {
      bflb_i2c_deinit(priv->dev);
      bflb_gpio_deinit(gpio, scl_pin);
      bflb_gpio_deinit(gpio, sda_pin);
      bflb_peripheral_clock_control(peripheral, false);
      priv->dev = NULL;
      nxmutex_unlock(&priv->lock);
      return NULL;
    }

  up_enable_irq(priv->irq);
#endif

  priv->scl_pin = scl_pin;
  priv->sda_pin = sda_pin;
  priv->refs = 1;
  nxmutex_unlock(&priv->lock);
  return (struct i2c_master_s *)priv;
}

/****************************************************************************
 * Name: bl616cl_i2cbus_uninitialize
 *
 * Description:
 *   Drop one reference to an I2C port. The last reference de-initializes the
 *   controller, detaches its interrupt in interrupt mode, releases the pins
 *   and gates the peripheral clock.
 *
 * Input Parameters:
 *   dev - Device structure as returned by bl616cl_i2cbus_initialize()
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. Errors: -EINVAL
 *   for a NULL or unknown handle or a port that is not initialized; -ENODEV
 *   if no I2C port is enabled.
 *
 ****************************************************************************/

int bl616cl_i2cbus_uninitialize(struct i2c_master_s *dev)
{
  struct bl616cl_i2c_priv_s *priv = (struct bl616cl_i2c_priv_s *)dev;
  struct bflb_device_s *gpio;
  int peripheral;

  if (priv == NULL)
    {
      return -EINVAL;
    }

#if defined(CONFIG_BL616CL_I2C0) && defined(CONFIG_BL616CL_I2C1)
  if (priv != &g_bl616cl_i2c0 && priv != &g_bl616cl_i2c1)
#elif defined(CONFIG_BL616CL_I2C0)
  if (priv != &g_bl616cl_i2c0)
#elif defined(CONFIG_BL616CL_I2C1)
  if (priv != &g_bl616cl_i2c1)
#else
  return -ENODEV;
#endif
#if defined(CONFIG_BL616CL_I2C0) || defined(CONFIG_BL616CL_I2C1)
    {
      return -EINVAL;
    }
#endif

  nxmutex_lock(&priv->lock);
  if (priv->refs == 0)
    {
      nxmutex_unlock(&priv->lock);
      return -EINVAL;
    }

  if (--priv->refs > 0)
    {
      nxmutex_unlock(&priv->lock);
      return OK;
    }

  gpio = bflb_device_get_by_name(BFLB_NAME_GPIO);
  bflb_i2c_deinit(priv->dev);
#ifndef CONFIG_I2C_POLLED
  up_disable_irq(priv->irq);
  irq_detach(priv->irq);
#endif
  if (gpio != NULL)
    {
      bflb_gpio_deinit(gpio, priv->scl_pin);
      bflb_gpio_deinit(gpio, priv->sda_pin);
    }

  peripheral = priv->port == 0 ? BFLB_PERIPHERAL_I2C0 :
                                 BFLB_PERIPHERAL_I2C1;
  bflb_peripheral_clock_control(peripheral, false);
  priv->dev = NULL;
  nxmutex_unlock(&priv->lock);
  return OK;
}
