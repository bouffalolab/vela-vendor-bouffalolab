/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_spi.c
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
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <nuttx/mutex.h>
#include <nuttx/spi/spi.h>

#include "bl616cl_sdk.h"
#include "bl616cl_glb.h"
#include "bflb_clock.h"
#include "bflb_name.h"
#include "bflb_peri.h"
#include "bflb_spi.h"
#include "bl616cl_spi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_SPI_DEFAULT_FREQUENCY 400000

#define BL616CL_SPI_CONFIG_FREQUENCY  (1 << 0)
#define BL616CL_SPI_CONFIG_MODE       (1 << 1)
#define BL616CL_SPI_CONFIG_BITS       (1 << 2)
#define BL616CL_SPI_CONFIG_BITORDER   (1 << 3)

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum bl616cl_spi_feature_e
{
  BL616CL_SPI_FEATURE_FREQUENCY = 0,
  BL616CL_SPI_FEATURE_MODE,
  BL616CL_SPI_FEATURE_BITS,
  BL616CL_SPI_FEATURE_BITORDER,
};

struct bl616cl_spi_priv_s
{
  struct spi_dev_s spi;
  mutex_t lock;
  struct bflb_device_s *dev;
  const struct bl616cl_spi_board_ops_s *board_ops;
  void *board_arg;
  uint32_t frequency;
  uint32_t actual;
  uint8_t config_error;
  uint16_t refs;
  uint8_t port;
  uint8_t mode;
  uint8_t nbits;
  bool lsbfirst;
  bool selected;
  bool selection_error;
  uint32_t selected_devid;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bl616cl_spi_lock(struct spi_dev_s *dev, bool lock);
static void bl616cl_spi_select(struct spi_dev_s *dev, uint32_t devid,
                               bool selected);
static uint32_t bl616cl_spi_setfrequency(struct spi_dev_s *dev,
                                         uint32_t frequency);
#ifdef CONFIG_SPI_DELAY_CONTROL
static int bl616cl_spi_setdelay(struct spi_dev_s *dev, uint32_t a,
                                uint32_t b, uint32_t c, uint32_t i);
#endif
static void bl616cl_spi_setmode(struct spi_dev_s *dev,
                                enum spi_mode_e mode);
static void bl616cl_spi_setbits(struct spi_dev_s *dev, int nbits);
#ifdef CONFIG_SPI_HWFEATURES
static int bl616cl_spi_hwfeatures(struct spi_dev_s *dev,
                                  spi_hwfeatures_t features);
#endif
static uint32_t bl616cl_spi_send(struct spi_dev_s *dev, uint32_t word);
#ifdef CONFIG_SPI_CMDDATA
static int bl616cl_spi_cmddata(struct spi_dev_s *dev, uint32_t devid,
                               bool cmd);
#endif
static void bl616cl_spi_exchange(struct spi_dev_s *dev,
                                 const void *txbuffer, void *rxbuffer,
                                 size_t nwords);
#ifdef CONFIG_SPI_TRIGGER
static int bl616cl_spi_trigger(struct spi_dev_s *dev);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct spi_ops_s g_bl616cl_spi_ops =
{
  .lock = bl616cl_spi_lock,
  .select = bl616cl_spi_select,
  .setfrequency = bl616cl_spi_setfrequency,
#ifdef CONFIG_SPI_DELAY_CONTROL
  .setdelay = bl616cl_spi_setdelay,
#endif
  .setmode = bl616cl_spi_setmode,
  .setbits = bl616cl_spi_setbits,
#ifdef CONFIG_SPI_HWFEATURES
  .hwfeatures = bl616cl_spi_hwfeatures,
#endif
  .status = NULL,
#ifdef CONFIG_SPI_CMDDATA
  .cmddata = bl616cl_spi_cmddata,
#endif
  .send = bl616cl_spi_send,
#ifdef CONFIG_SPI_EXCHANGE
  .exchange = bl616cl_spi_exchange,
#else
  .sndblock = NULL,
  .recvblock = NULL,
#endif
#ifdef CONFIG_SPI_TRIGGER
  .trigger = bl616cl_spi_trigger,
#endif
  .registercallback = NULL,
};

static mutex_t g_bl616cl_spi_init_lock = NXMUTEX_INITIALIZER;

#ifdef CONFIG_BL616CL_SPI0
static struct bl616cl_spi_priv_s g_bl616cl_spi0 =
{
  .spi =
  {
    .ops = &g_bl616cl_spi_ops
  },
  .lock = NXMUTEX_INITIALIZER,
  .frequency = BL616CL_SPI_DEFAULT_FREQUENCY,
  .port = 0,
  .mode = SPIDEV_MODE0,
  .nbits = 8,
};
#endif

#ifdef CONFIG_BL616CL_SPI1
static struct bl616cl_spi_priv_s g_bl616cl_spi1 =
{
  .spi =
  {
    .ops = &g_bl616cl_spi_ops
  },
  .lock = NXMUTEX_INITIALIZER,
  .frequency = BL616CL_SPI_DEFAULT_FREQUENCY,
  .port = 1,
  .mode = SPIDEV_MODE0,
  .nbits = 8,
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_spi_priv
 *
 * Description:
 *   Map an SPI port number to its driver state instance, limited to the
 *   ports enabled in Kconfig.
 *
 * Input Parameters:
 *   port - SPI port number (0 or 1).
 *
 * Returned Value:
 *   Pointer to the port state, or NULL if the port is invalid or not
 *   enabled.
 *
 ****************************************************************************/

static struct bl616cl_spi_priv_s *bl616cl_spi_priv(int port)
{
  switch (port)
    {
#ifdef CONFIG_BL616CL_SPI0
      case 0:
        return &g_bl616cl_spi0;
#endif
#ifdef CONFIG_BL616CL_SPI1
      case 1:
        return &g_bl616cl_spi1;
#endif
      default:
        return NULL;
    }
}

/****************************************************************************
 * Name: bl616cl_spi_from_dev
 *
 * Description:
 *   Map an spi_dev_s pointer back to the matching driver state instance by
 *   address comparison.
 *
 * Input Parameters:
 *   dev - Pointer to the SPI device instance.
 *
 * Returned Value:
 *   Pointer to the port state, or NULL if dev is not a known SPI device.
 *
 ****************************************************************************/

static struct bl616cl_spi_priv_s *bl616cl_spi_from_dev(
  struct spi_dev_s *dev)
{
#ifdef CONFIG_BL616CL_SPI0
  if (dev == &g_bl616cl_spi0.spi)
    {
      return &g_bl616cl_spi0;
    }
#endif

#ifdef CONFIG_BL616CL_SPI1
  if (dev == &g_bl616cl_spi1.spi)
    {
      return &g_bl616cl_spi1;
    }
#endif

  return NULL;
}

/****************************************************************************
 * Name: bl616cl_spi_normalize_error
 *
 * Description:
 *   Translate the LHAL timeout error code into -ETIMEDOUT; other codes pass
 *   through.
 *
 * Input Parameters:
 *   error - Error code from the LHAL transport.
 *
 * Returned Value:
 *   -ETIMEDOUT if error is the LHAL timeout code, otherwise error unchanged.
 *
 ****************************************************************************/

static int bl616cl_spi_normalize_error(int error)
{
  return error == -BL616CL_LHAL_ETIMEDOUT ? -ETIMEDOUT : error;
}

/****************************************************************************
 * Name: bl616cl_spi_clock_configure
 *
 * Description:
 *   Enable or disable the SPI module clock (GLB, XCLK source) and the
 *   peripheral clock gate. If enabling the peripheral clock fails, the GLB
 *   clock is turned off again.
 *
 * Input Parameters:
 *   priv - Pointer to the SPI driver state.
 *   enable - True: enable the clocks; false: disable them.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. -EIO if the GLB
 *   clock setup fails; otherwise the error from the peripheral clock
 *   control.
 *
 ****************************************************************************/

static int bl616cl_spi_clock_configure(struct bl616cl_spi_priv_s *priv,
                                       bool enable)
{
  int peripheral = priv->port == 0 ? BFLB_PERIPHERAL_SPI0 :
                                     BFLB_PERIPHERAL_SPI1;
  int ret;

  ret = priv->port == 0 ?
          GLB_Set_SPI0_CLK(enable ? ENABLE : DISABLE, GLB_SPI_CLK_XCLK, 0) :
          GLB_Set_SPI1_CLK(enable ? ENABLE : DISABLE, GLB_SPI_CLK_XCLK, 0);
  if (ret != SUCCESS)
    {
      return -EIO;
    }

  ret = bflb_peripheral_clock_control(peripheral, enable);
  if (ret < 0 && enable)
    {
      priv->port == 0 ?
        GLB_Set_SPI0_CLK(DISABLE, GLB_SPI_CLK_XCLK, 0) :
        GLB_Set_SPI1_CLK(DISABLE, GLB_SPI_CLK_XCLK, 0);
    }

  return ret;
}

/****************************************************************************
 * Name: bl616cl_spi_transport_feature
 *
 * Description:
 *   Apply a frequency, mode, data width or bit order setting to the hardware
 *   via bflb_spi_feature_control().
 *
 * Input Parameters:
 *   priv - Pointer to the SPI driver state.
 *   feature - Which setting to change.
 *   value - Value of the setting (for bits: 8 or 16; for bit order: nonzero
 *       is LSB first).
 *
 * Returned Value:
 *   The result of the feature control call, or -EINVAL for an unknown
 *   feature.
 *
 ****************************************************************************/

static int bl616cl_spi_transport_feature(
  struct bl616cl_spi_priv_s *priv,
  enum bl616cl_spi_feature_e feature, uint32_t value)
{
  switch (feature)
    {
      case BL616CL_SPI_FEATURE_FREQUENCY:
        return bflb_spi_feature_control(priv->dev, SPI_CMD_SET_FREQ, value);
      case BL616CL_SPI_FEATURE_MODE:
        return bflb_spi_feature_control(priv->dev, SPI_CMD_SET_MODE, value);
      case BL616CL_SPI_FEATURE_BITS:
        return bflb_spi_feature_control(priv->dev, SPI_CMD_SET_DATA_WIDTH,
                                        value == 8 ? SPI_DATA_WIDTH_8BIT :
                                                     SPI_DATA_WIDTH_16BIT);
      case BL616CL_SPI_FEATURE_BITORDER:
        return bflb_spi_feature_control(priv->dev, SPI_CMD_SET_BIT_ORDER,
                                        value ? SPI_BIT_LSB : SPI_BIT_MSB);
      default:
        return -EINVAL;
    }
}

/****************************************************************************
 * Name: bl616cl_spi_transport_exchange
 *
 * Description:
 *   Perform a polled full-duplex exchange of nbytes bytes using
 *   bflb_spi_poll_exchange(). LHAL timeouts are mapped to -ETIMEDOUT.
 *
 * Input Parameters:
 *   priv - Pointer to the SPI driver state.
 *   txbuffer - Data to send.
 *   rxbuffer - Buffer for received data.
 *   nbytes - Number of bytes to exchange.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int bl616cl_spi_transport_exchange(
  struct bl616cl_spi_priv_s *priv, const void *txbuffer, void *rxbuffer,
  size_t nbytes)
{
  int ret;

  ret = bflb_spi_poll_exchange(priv->dev, txbuffer, rxbuffer, nbytes);
  return bl616cl_spi_normalize_error(ret);
}

/****************************************************************************
 * Name: bl616cl_spi_transport_select
 *
 * Description:
 *   Assert or deassert chip select through the board select callback.
 *
 * Input Parameters:
 *   priv - Pointer to the SPI driver state.
 *   devid - Device ID passed to the select callback.
 *   selected - True: select the device; false: deselect it.
 *
 * Returned Value:
 *   True if the callback accepted the request; false if it rejected it or no
 *   callback is set.
 *
 ****************************************************************************/

static bool bl616cl_spi_transport_select(
  struct bl616cl_spi_priv_s *priv, uint32_t devid, bool selected)
{
  return priv->board_ops != NULL && priv->board_ops->select != NULL &&
         priv->board_ops->select(priv->board_arg, devid, selected);
}

/****************************************************************************
 * Name: bl616cl_spi_recover
 *
 * Description:
 *   Recover after a failed transfer: clear both FIFOs, deinitialize the
 *   controller and reinitialize it as master with the current frequency,
 *   mode, data width and bit order.
 *
 * Input Parameters:
 *   priv - Pointer to the SPI driver state.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616cl_spi_recover(struct bl616cl_spi_priv_s *priv)
{
  struct bflb_spi_config_s config;

  bflb_spi_feature_control(priv->dev, SPI_CMD_CLEAR_TX_FIFO, 0);
  bflb_spi_feature_control(priv->dev, SPI_CMD_CLEAR_RX_FIFO, 0);
  bflb_spi_deinit(priv->dev);

  config.freq = priv->actual;
  config.role = SPI_ROLE_MASTER;
  config.mode = priv->mode;
  config.data_width = priv->nbits == 16 ? SPI_DATA_WIDTH_16BIT :
                                          SPI_DATA_WIDTH_8BIT;
  config.bit_order = priv->lsbfirst ? SPI_BIT_LSB : SPI_BIT_MSB;
  config.byte_order = SPI_BYTE_LSB;
  config.tx_fifo_threshold = 0;
  config.rx_fifo_threshold = 0;
  bflb_spi_init(priv->dev, &config);
}

/****************************************************************************
 * Name: bl616cl_spi_actual_frequency
 *
 * Description:
 *   Compute the frequency the controller will actually produce for a
 *   requested frequency, using the peripheral clock divided by an even
 *   divider (2 * 1..256), rounded so the result does not exceed the request.
 *
 *   The LHAL SPI_CMD_SET_FREQ rounds to the nearest divider instead, which
 *   can exceed the request (1.1 MHz gives 1.111 MHz from a 40 MHz clock)
 *   and overclock a device. Passing it the frequency computed here makes it
 *   program exactly this divider, so the value returned to the SPI upper
 *   half matches the hardware. SPI_CMD_GET_FREQ is not used to read the
 *   divider back: it drops the +1 of both phase fields and divides by zero
 *   at the fastest setting.
 *
 * Input Parameters:
 *   priv - Pointer to the SPI driver state.
 *   frequency - Requested SPI clock frequency in Hz.
 *
 * Returned Value:
 *   The achievable frequency in Hz, or 0 if the peripheral clock is unknown
 *   or the request is below the minimum supported frequency.
 *
 ****************************************************************************/

static uint32_t bl616cl_spi_actual_frequency(
  struct bl616cl_spi_priv_s *priv, uint32_t frequency)
{
  uint32_t clock;
  uint32_t divider;
  uint64_t denominator;

  clock = bflb_clk_get_peripheral_clock(BFLB_DEVICE_TYPE_SPI, priv->port);
  if (clock == 0)
    {
      return 0;
    }

  if (frequency < ((uint64_t)clock + 2 * (UINT8_MAX + 1) - 1) /
                  (2 * (UINT8_MAX + 1)))
    {
      return 0;
    }

  denominator = (uint64_t)frequency * 2;
  divider = (uint32_t)(((uint64_t)clock + denominator - 1) / denominator);
  if (divider == 0)
    {
      divider = 1;
    }
  else if (divider > UINT8_MAX + 1)
    {
      divider = UINT8_MAX + 1;
    }

  return clock / (2 * divider);
}

/****************************************************************************
 * Name: bl616cl_spi_lock
 *
 * Description:
 *   Implement the spi_ops_s lock operation. Take or release the per-port
 *   mutex so that a task has exclusive use of the bus.
 *
 * Input Parameters:
 *   dev - Pointer to the SPI device instance.
 *   lock - True: take the mutex; false: release it.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int bl616cl_spi_lock(struct spi_dev_s *dev, bool lock)
{
  struct bl616cl_spi_priv_s *priv =
    (struct bl616cl_spi_priv_s *)dev;

  return lock ? nxmutex_lock(&priv->lock) : nxmutex_unlock(&priv->lock);
}

/****************************************************************************
 * Name: bl616cl_spi_select
 *
 * Description:
 *   Implement the spi_ops_s select operation. Assert or deassert chip select
 *   through the board callback. Selecting a different device first deselects
 *   the previously selected one. A rejected select blocks further exchanges
 *   until a select succeeds.
 *
 * Input Parameters:
 *   dev - Pointer to the SPI device instance.
 *   devid - Device ID of the chip select.
 *   selected - True: select the device; false: deselect it.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616cl_spi_select(struct spi_dev_s *dev, uint32_t devid,
                               bool selected)
{
  struct bl616cl_spi_priv_s *priv =
    (struct bl616cl_spi_priv_s *)dev;
  bool accepted;

  if (selected && priv->selected && priv->selected_devid != devid)
    {
      accepted = bl616cl_spi_transport_select(priv, priv->selected_devid,
                                              false);
      priv->selected = false;
      if (!accepted)
        {
          priv->selection_error = true;
          return;
        }
    }

  accepted = bl616cl_spi_transport_select(priv, devid, selected);
  if (accepted)
    {
      if (selected)
        {
          priv->selected = true;
          priv->selected_devid = devid;
          priv->selection_error = false;
        }
      else if (priv->selected && priv->selected_devid == devid)
        {
          priv->selected = false;
        }
    }
  else if (selected)
    {
      priv->selection_error = true;
    }
}

#ifdef CONFIG_SPI_DELAY_CONTROL
/****************************************************************************
 * Name: bl616cl_spi_setdelay
 *
 * Description:
 *   Implement the spi_ops_s setdelay operation. Delay control is not
 *   supported.
 *
 * Input Parameters:
 *   dev - Pointer to the SPI device instance.
 *   a - Unused.
 *   b - Unused.
 *   c - Unused.
 *   i - Unused.
 *
 * Returned Value:
 *   -ENOSYS is always returned.
 *
 ****************************************************************************/

static int bl616cl_spi_setdelay(struct spi_dev_s *dev, uint32_t a,
                                uint32_t b, uint32_t c, uint32_t i)
{
  return -ENOSYS;
}
#endif

/****************************************************************************
 * Name: bl616cl_spi_setfrequency
 *
 * Description:
 *   Implement the spi_ops_s setfrequency operation. Compute the achievable
 *   frequency and program it into the controller. A repeat of the current
 *   setting is skipped unless a previous attempt failed.
 *
 * Input Parameters:
 *   dev - Pointer to the SPI device instance.
 *   frequency - Requested SPI clock frequency in Hz.
 *
 * Returned Value:
 *   The actual frequency in Hz, or 0 on failure (the failure is recorded in
 *   the driver state).
 *
 ****************************************************************************/

static uint32_t bl616cl_spi_setfrequency(struct spi_dev_s *dev,
                                         uint32_t frequency)
{
  struct bl616cl_spi_priv_s *priv =
    (struct bl616cl_spi_priv_s *)dev;
  uint32_t actual;
  int ret;

  if (frequency == 0)
    {
      priv->config_error |= BL616CL_SPI_CONFIG_FREQUENCY;
      return 0;
    }

  actual = bl616cl_spi_actual_frequency(priv, frequency);
  if (actual == 0)
    {
      priv->config_error |= BL616CL_SPI_CONFIG_FREQUENCY;
      return 0;
    }

  if (frequency == priv->frequency && actual == priv->actual &&
      (priv->config_error & BL616CL_SPI_CONFIG_FREQUENCY) == 0)
    {
      return actual;
    }

  ret = bl616cl_spi_transport_feature(priv, BL616CL_SPI_FEATURE_FREQUENCY,
                                      actual);
  if (ret < 0)
    {
      priv->config_error |= BL616CL_SPI_CONFIG_FREQUENCY;
      return 0;
    }

  priv->frequency = frequency;
  priv->actual = actual;
  priv->config_error &= ~BL616CL_SPI_CONFIG_FREQUENCY;
  return actual;
}

/****************************************************************************
 * Name: bl616cl_spi_setmode
 *
 * Description:
 *   Implement the spi_ops_s setmode operation. Program SPI mode 0-3 into the
 *   controller. Invalid modes and hardware failures are recorded and later
 *   exchanges are refused until the mode is set successfully.
 *
 * Input Parameters:
 *   dev - Pointer to the SPI device instance.
 *   mode - The requested SPI mode.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616cl_spi_setmode(struct spi_dev_s *dev,
                                enum spi_mode_e mode)
{
  struct bl616cl_spi_priv_s *priv =
    (struct bl616cl_spi_priv_s *)dev;
  int ret;

  if (mode < SPIDEV_MODE0 || mode > SPIDEV_MODE3)
    {
      priv->config_error |= BL616CL_SPI_CONFIG_MODE;
      return;
    }

  if (mode == priv->mode &&
      (priv->config_error & BL616CL_SPI_CONFIG_MODE) == 0)
    {
      return;
    }

  ret = bl616cl_spi_transport_feature(priv, BL616CL_SPI_FEATURE_MODE, mode);
  if (ret < 0)
    {
      priv->config_error |= BL616CL_SPI_CONFIG_MODE;
      return;
    }

  priv->mode = mode;
  priv->config_error &= ~BL616CL_SPI_CONFIG_MODE;
}

/****************************************************************************
 * Name: bl616cl_spi_setbits
 *
 * Description:
 *   Implement the spi_ops_s setbits operation. Program the word width into
 *   the controller; only 8 and 16 bits are supported. Failures are recorded
 *   and later exchanges are refused until the width is set successfully.
 *
 * Input Parameters:
 *   dev - Pointer to the SPI device instance.
 *   nbits - Word width in bits (8 or 16).
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616cl_spi_setbits(struct spi_dev_s *dev, int nbits)
{
  struct bl616cl_spi_priv_s *priv =
    (struct bl616cl_spi_priv_s *)dev;
  int ret;

  if (nbits != 8 && nbits != 16)
    {
      priv->config_error |= BL616CL_SPI_CONFIG_BITS;
      return;
    }

  if (nbits == priv->nbits &&
      (priv->config_error & BL616CL_SPI_CONFIG_BITS) == 0)
    {
      return;
    }

  ret = bl616cl_spi_transport_feature(priv, BL616CL_SPI_FEATURE_BITS, nbits);
  if (ret < 0)
    {
      priv->config_error |= BL616CL_SPI_CONFIG_BITS;
      return;
    }

  priv->nbits = nbits;
  priv->config_error &= ~BL616CL_SPI_CONFIG_BITS;
}

#ifdef CONFIG_SPI_HWFEATURES
/****************************************************************************
 * Name: bl616cl_spi_hwfeatures
 *
 * Description:
 *   Implement the spi_ops_s hwfeatures operation. The only supported feature
 *   is HWFEAT_LSBFIRST (when CONFIG_SPI_BITORDER is set), which selects the
 *   bit order.
 *
 * Input Parameters:
 *   dev - Pointer to the SPI device instance.
 *   features - Bit set of HWFEAT_* features to enable.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. -ENOSYS if an
 *   unsupported feature is requested; otherwise the error from the hardware.
 *
 ****************************************************************************/

static int bl616cl_spi_hwfeatures(struct spi_dev_s *dev,
                                  spi_hwfeatures_t features)
{
  struct bl616cl_spi_priv_s *priv =
    (struct bl616cl_spi_priv_s *)dev;
  bool lsbfirst;
  int ret;

#ifdef CONFIG_SPI_BITORDER
  if ((features & ~HWFEAT_LSBFIRST) != 0)
#else
  if (features != 0)
#endif
    {
      priv->config_error |= BL616CL_SPI_CONFIG_BITORDER;
      return -ENOSYS;
    }

#ifdef CONFIG_SPI_BITORDER
  lsbfirst = (features & HWFEAT_LSBFIRST) != 0;
#else
  lsbfirst = false;
#endif
  if (lsbfirst == priv->lsbfirst &&
      (priv->config_error & BL616CL_SPI_CONFIG_BITORDER) == 0)
    {
      return OK;
    }

  ret = bl616cl_spi_transport_feature(priv, BL616CL_SPI_FEATURE_BITORDER,
                                      lsbfirst);
  if (ret < 0)
    {
      priv->config_error |= BL616CL_SPI_CONFIG_BITORDER;
      return ret;
    }

  priv->lsbfirst = lsbfirst;
  priv->config_error &= ~BL616CL_SPI_CONFIG_BITORDER;
  return OK;
}
#endif

/****************************************************************************
 * Name: bl616cl_spi_send
 *
 * Description:
 *   Implement the spi_ops_s send operation. Exchange a single 8 or 16-bit
 *   word (per the current width) and return the received word.
 *
 * Input Parameters:
 *   dev - Pointer to the SPI device instance.
 *   word - The word to send.
 *
 * Returned Value:
 *   The word received.
 *
 ****************************************************************************/

static uint32_t bl616cl_spi_send(struct spi_dev_s *dev, uint32_t word)
{
  struct bl616cl_spi_priv_s *priv =
    (struct bl616cl_spi_priv_s *)dev;
  uint16_t tx16;
  uint16_t rx16 = 0;
  uint8_t tx8;
  uint8_t rx8 = 0;

  if (priv->nbits == 16)
    {
      tx16 = (uint16_t)word;
      bl616cl_spi_exchange(dev, &tx16, &rx16, 1);
      return rx16;
    }

  tx8 = (uint8_t)word;
  bl616cl_spi_exchange(dev, &tx8, &rx8, 1);
  return rx8;
}

#ifdef CONFIG_SPI_CMDDATA
/****************************************************************************
 * Name: bl616cl_spi_cmddata
 *
 * Description:
 *   Implement the spi_ops_s cmddata operation. Command/data selection is not
 *   supported.
 *
 * Input Parameters:
 *   dev - Pointer to the SPI device instance.
 *   devid - Unused.
 *   cmd - Unused.
 *
 * Returned Value:
 *   -ENOSYS is always returned.
 *
 ****************************************************************************/

static int bl616cl_spi_cmddata(struct spi_dev_s *dev, uint32_t devid,
                               bool cmd)
{
  return -ENOSYS;
}
#endif

/****************************************************************************
 * Name: bl616cl_spi_exchange
 *
 * Description:
 *   Implement the spi_ops_s exchange operation. Perform a polled exchange of
 *   nwords words. The call is skipped if a configuration or select error is
 *   pending, the width is invalid or the buffers are misaligned. On a
 *   transfer failure the error is logged and the controller is recovered.
 *
 * Input Parameters:
 *   dev - Pointer to the SPI device instance.
 *   txbuffer - Data to send.
 *   rxbuffer - Buffer for received data.
 *   nwords - Number of words to exchange.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616cl_spi_exchange(struct spi_dev_s *dev,
                                 const void *txbuffer, void *rxbuffer,
                                 size_t nwords)
{
  struct bl616cl_spi_priv_s *priv =
    (struct bl616cl_spi_priv_s *)dev;
  size_t word_size;
  size_t nbytes;
  int ret;

  if (nwords == 0)
    {
      return;
    }

  if (priv->nbits != 8 && priv->nbits != 16)
    {
      return;
    }

  if (priv->config_error != 0)
    {
      return;
    }

  if (priv->selection_error)
    {
      return;
    }

  word_size = priv->nbits == 16 ? sizeof(uint16_t) : sizeof(uint8_t);
  if (nwords > SIZE_MAX / word_size ||
      (word_size > 1 &&
       (((uintptr_t)txbuffer & (word_size - 1)) != 0 ||
        ((uintptr_t)rxbuffer & (word_size - 1)) != 0)))
    {
      return;
    }

  nbytes = nwords * word_size;
  ret = bl616cl_spi_transport_exchange(priv, txbuffer, rxbuffer, nbytes);
  if (ret < 0)
    {
      spierr("ERROR: SPI%d polling exchange failed: %d\n", priv->port, ret);
      bl616cl_spi_recover(priv);
      return;
    }

  priv->config_error = 0;
}

#ifdef CONFIG_SPI_TRIGGER
/****************************************************************************
 * Name: bl616cl_spi_trigger
 *
 * Description:
 *   Implement the spi_ops_s trigger operation. Triggered transfers are not
 *   supported.
 *
 * Input Parameters:
 *   dev - Pointer to the SPI device instance.
 *
 * Returned Value:
 *   -ENOSYS is always returned.
 *
 ****************************************************************************/

static int bl616cl_spi_trigger(struct spi_dev_s *dev)
{
  return -ENOSYS;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_spibus_initialize
 *
 * Description:
 *   Initialize an SPI port as a master and return its device instance. The
 *   first call enables the clocks and sets 400 kHz, mode 0, 8-bit, MSB
 *   first; later calls with the same board ops and argument only take a
 *   reference.
 *
 * Input Parameters:
 *   port - SPI port number (0 or 1).
 *   board_ops - Board callbacks, including chip select.
 *   board_arg - Argument passed to the board callbacks.
 *
 * Returned Value:
 *   A pointer to the SPI device instance, or NULL if the port is invalid,
 *   board_ops or its select callback is NULL, the arguments differ from the
 *   first initialization, the reference count is exhausted, or the device or
 *   clock setup fails.
 *
 ****************************************************************************/

struct spi_dev_s *bl616cl_spibus_initialize(
  int port, const struct bl616cl_spi_board_ops_s *board_ops, void *board_arg)
{
  struct bl616cl_spi_priv_s *priv = bl616cl_spi_priv(port);
  struct bflb_spi_config_s config;
  int ret;

  if (priv == NULL || board_ops == NULL || board_ops->select == NULL)
    {
      return NULL;
    }

  nxmutex_lock(&g_bl616cl_spi_init_lock);
  nxmutex_lock(&priv->lock);
  if (priv->refs > 0)
    {
      if (priv->board_ops != board_ops || priv->board_arg != board_arg)
        {
          nxmutex_unlock(&priv->lock);
          nxmutex_unlock(&g_bl616cl_spi_init_lock);
          return NULL;
        }

      if (priv->refs == UINT16_MAX)
        {
          nxmutex_unlock(&priv->lock);
          nxmutex_unlock(&g_bl616cl_spi_init_lock);
          return NULL;
        }

      priv->refs++;
      nxmutex_unlock(&priv->lock);
      nxmutex_unlock(&g_bl616cl_spi_init_lock);
      return &priv->spi;
    }

  priv->dev = bflb_device_get_by_name(port == 0 ? BFLB_NAME_SPI0 :
                                                  BFLB_NAME_SPI1);
  if (priv->dev == NULL)
    {
      nxmutex_unlock(&priv->lock);
      nxmutex_unlock(&g_bl616cl_spi_init_lock);
      return NULL;
    }

  ret = bl616cl_spi_clock_configure(priv, true);
  if (ret < 0)
    {
      priv->dev = NULL;
      nxmutex_unlock(&priv->lock);
      nxmutex_unlock(&g_bl616cl_spi_init_lock);
      return NULL;
    }

  config.freq = BL616CL_SPI_DEFAULT_FREQUENCY;
  config.role = SPI_ROLE_MASTER;
  config.mode = SPI_MODE0;
  config.data_width = SPI_DATA_WIDTH_8BIT;
  config.bit_order = SPI_BIT_MSB;
  config.byte_order = SPI_BYTE_LSB;
  config.tx_fifo_threshold = 0;
  config.rx_fifo_threshold = 0;
  bflb_spi_init(priv->dev, &config);

  priv->board_ops = board_ops;
  priv->board_arg = board_arg;
  priv->frequency = BL616CL_SPI_DEFAULT_FREQUENCY;
  priv->actual = bl616cl_spi_actual_frequency(priv, priv->frequency);
  priv->config_error = 0;
  priv->mode = SPIDEV_MODE0;
  priv->nbits = 8;
  priv->lsbfirst = false;
  priv->selected = false;
  priv->selection_error = false;
  priv->refs = 1;
  nxmutex_unlock(&priv->lock);
  nxmutex_unlock(&g_bl616cl_spi_init_lock);
  return &priv->spi;
}

/****************************************************************************
 * Name: bl616cl_spibus_uninitialize
 *
 * Description:
 *   Release a reference to an SPI port. On the last reference, deselect any
 *   selected device, deinitialize the controller, disable the clocks and
 *   clear the board callbacks.
 *
 * Input Parameters:
 *   dev - Pointer to the SPI device instance.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. -EINVAL if dev
 *   is not a known device or has no reference.
 *
 ****************************************************************************/

int bl616cl_spibus_uninitialize(struct spi_dev_s *dev)
{
  struct bl616cl_spi_priv_s *priv = bl616cl_spi_from_dev(dev);

  if (priv == NULL)
    {
      return -EINVAL;
    }

  nxmutex_lock(&g_bl616cl_spi_init_lock);
  nxmutex_lock(&priv->lock);
  if (priv->refs == 0)
    {
      nxmutex_unlock(&priv->lock);
      nxmutex_unlock(&g_bl616cl_spi_init_lock);
      return -EINVAL;
    }

  if (--priv->refs > 0)
    {
      nxmutex_unlock(&priv->lock);
      nxmutex_unlock(&g_bl616cl_spi_init_lock);
      return OK;
    }

  if (priv->selected)
    {
      bl616cl_spi_transport_select(priv, priv->selected_devid, false);
      priv->selected = false;
    }

  bflb_spi_deinit(priv->dev);
  bl616cl_spi_clock_configure(priv, false);
  priv->dev = NULL;
  priv->board_ops = NULL;
  priv->board_arg = NULL;
  nxmutex_unlock(&priv->lock);
  nxmutex_unlock(&g_bl616cl_spi_init_lock);
  return OK;
}

/****************************************************************************
 * Name: bl616cl_spi_configure_pins
 *
 * Description:
 *   Select which pins carry MISO and MOSI by programming the port's MOSI/MISO
 *   swap. Each pin must be a valid SPI data pin (pin number modulo 4 is 2 or
 *   3), and MISO and MOSI must use different signals.
 *
 * Input Parameters:
 *   port - SPI port number (0 or 1).
 *   miso_pin - GPIO number for MISO (0-36).
 *   mosi_pin - GPIO number for MOSI (0-36).
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. -EINVAL for an
 *   invalid port or pin combination; -EIO if the GLB configuration fails.
 *
 ****************************************************************************/

int bl616cl_spi_configure_pins(int port, uint8_t miso_pin,
                               uint8_t mosi_pin)
{
  struct bl616cl_spi_priv_s *priv = bl616cl_spi_priv(port);
  BL_Fun_Type swap;
  uint8_t group;
  int ret;

  if (priv == NULL || miso_pin > 36 || mosi_pin > 36 ||
      (miso_pin % 4 != 2 && miso_pin % 4 != 3) ||
      (mosi_pin % 4 != 2 && mosi_pin % 4 != 3) ||
      miso_pin % 4 == mosi_pin % 4)
    {
      return -EINVAL;
    }

  /* The per-group signal swap also moves SS and SCLK onto the data pins
   * (BL616CL PIN_MUX table), so keep it clear for every group of the port.
   * The port-wide MOSI/MISO swap alone selects the data order; MISO on
   * signal 2 is the unswapped layout used by the Bouffalo SDK boards.
   */

  nxmutex_lock(&g_bl616cl_spi_init_lock);
  ret = SUCCESS;
  for (group = GLB_SPI_SIG_SWAP_GRP_GPIO0_GPIO11;
       ret == SUCCESS && group <= GLB_SPI_SIG_SWAP_GRP_GPIO36_GPIO45;
       group++)
    {
      ret = port == 0 ? GLB_SPI0_Sig_Swap_Set(group, 0) :
                        GLB_SPI1_Sig_Swap_Set(group, 0);
    }

  swap = miso_pin % 4 == 3 ? ENABLE : DISABLE;
  if (ret == SUCCESS)
    {
      ret = port == 0 ? GLB_Swap_MCU_SPI_0_MOSI_With_MISO(swap) :
                        GLB_Swap_MCU_SPI_1_MOSI_With_MISO(swap);
    }

  nxmutex_unlock(&g_bl616cl_spi_init_lock);
  return ret == SUCCESS ? OK : -EIO;
}
