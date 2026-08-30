/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_spi.c
 *
 * SPDX-License-Identifier: Apache-2.0
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

#include "bflb_clock.h"
#include "bflb_name.h"
#include "bflb_peri.h"
#include "bflb_spi.h"
#include "bl616cl_spi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_SPI_DEFAULT_FREQUENCY 400000

#define BL616CL_SDK_DISABLE           0
#define BL616CL_SDK_ENABLE            1
#define BL616CL_SDK_SPI_CLK_XCLK      1
#define BL616CL_SDK_ETIMEDOUT         116
#define BL616CL_OPENVELA_ETIMEDOUT    110

#define BL616CL_SPI_CONFIG_FREQUENCY  (1 << 0)
#define BL616CL_SPI_CONFIG_MODE       (1 << 1)
#define BL616CL_SPI_CONFIG_BITS       (1 << 2)
#define BL616CL_SPI_CONFIG_BITORDER   (1 << 3)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bl616cl_spi_priv_s
{
  struct spi_dev_s spi;
  mutex_t lock;
  struct bflb_device_s *dev;
  const struct bl616cl_spi_board_ops_s *board_ops;
  void *board_arg;
  uint32_t frequency;
  uint32_t actual;
  uint32_t error_count;
  int last_error;
  uint8_t config_error;
  uint16_t refs;
  uint8_t port;
  uint8_t mode;
  uint8_t nbits;
  bool lsbfirst;
  bool selected;
  bool selection_error;
  uint32_t selected_devid;
#ifdef CONFIG_BL616CL_SPI_TEST
  const struct bl616cl_spi_test_ops_s *test_ops;
  void *test_arg;
#endif
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

extern int bl616cl_sdk_glb_set_spi0_clk(uint8_t enable, uint8_t clk_sel,
                                        uint8_t div)
  __asm__("GLB_Set_SPI0_CLK");
extern int bl616cl_sdk_glb_set_spi1_clk(uint8_t enable, uint8_t clk_sel,
                                        uint8_t div)
  __asm__("GLB_Set_SPI1_CLK");
extern int bl616cl_sdk_glb_spi0_sig_swap_set(uint8_t group, uint8_t swap)
  __asm__("GLB_SPI0_Sig_Swap_Set");
extern int bl616cl_sdk_glb_spi1_sig_swap_set(uint8_t group, uint8_t swap)
  __asm__("GLB_SPI1_Sig_Swap_Set");
extern int bl616cl_sdk_glb_spi0_global_swap(uint8_t enable)
  __asm__("GLB_Swap_MCU_SPI_0_MOSI_With_MISO");
extern int bl616cl_sdk_glb_spi1_global_swap(uint8_t enable)
  __asm__("GLB_Swap_MCU_SPI_1_MOSI_With_MISO");

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

static void bl616cl_spi_failed(struct bl616cl_spi_priv_s *priv, int error)
{
  priv->last_error = error;
  priv->error_count++;
}

static int bl616cl_spi_normalize_error(int error)
{
  return error == -BL616CL_SDK_ETIMEDOUT ?
           -BL616CL_OPENVELA_ETIMEDOUT :
           error;
}

static int bl616cl_spi_clock_configure(struct bl616cl_spi_priv_s *priv,
                                       bool enable)
{
  int peripheral = priv->port == 0 ? BFLB_PERIPHERAL_SPI0 :
                                     BFLB_PERIPHERAL_SPI1;
  int ret;

  ret = priv->port == 0 ?
          bl616cl_sdk_glb_set_spi0_clk(enable ? BL616CL_SDK_ENABLE :
                                                BL616CL_SDK_DISABLE,
                                       BL616CL_SDK_SPI_CLK_XCLK, 0) :
          bl616cl_sdk_glb_set_spi1_clk(enable ? BL616CL_SDK_ENABLE :
                                                BL616CL_SDK_DISABLE,
                                       BL616CL_SDK_SPI_CLK_XCLK, 0);
  if (ret != 0)
    {
      return -EIO;
    }

  ret = bflb_peripheral_clock_control(peripheral, enable);
  if (ret < 0 && enable)
    {
      (void)(priv->port == 0 ?
               bl616cl_sdk_glb_set_spi0_clk(BL616CL_SDK_DISABLE,
                                            BL616CL_SDK_SPI_CLK_XCLK, 0) :
               bl616cl_sdk_glb_set_spi1_clk(BL616CL_SDK_DISABLE,
                                            BL616CL_SDK_SPI_CLK_XCLK, 0));
    }

  return ret;
}

static void bl616cl_spi_config_failed(struct bl616cl_spi_priv_s *priv,
                                      uint8_t config, int error)
{
  priv->config_error |= config;
  bl616cl_spi_failed(priv, error);
}

static void bl616cl_spi_config_succeeded(struct bl616cl_spi_priv_s *priv,
                                         uint8_t config)
{
  priv->config_error &= ~config;
  if (priv->config_error == 0)
    {
      priv->last_error = OK;
    }
}

static int bl616cl_spi_transport_feature(
  struct bl616cl_spi_priv_s *priv,
  enum bl616cl_spi_test_feature_e feature, uint32_t value)
{
#ifdef CONFIG_BL616CL_SPI_TEST
  if (priv->test_ops != NULL)
    {
      return priv->test_ops->feature(priv->test_arg, feature, value);
    }
#endif

  switch (feature)
    {
      case BL616CL_SPI_TEST_FREQUENCY:
        return bflb_spi_feature_control(priv->dev, SPI_CMD_SET_FREQ, value);
      case BL616CL_SPI_TEST_MODE:
        return bflb_spi_feature_control(priv->dev, SPI_CMD_SET_MODE, value);
      case BL616CL_SPI_TEST_BITS:
        return bflb_spi_feature_control(priv->dev, SPI_CMD_SET_DATA_WIDTH,
                                        value == 8 ? SPI_DATA_WIDTH_8BIT :
                                                     SPI_DATA_WIDTH_16BIT);
      case BL616CL_SPI_TEST_BITORDER:
        return bflb_spi_feature_control(priv->dev, SPI_CMD_SET_BIT_ORDER,
                                        value ? SPI_BIT_LSB : SPI_BIT_MSB);
      default:
        return -EINVAL;
    }
}

static int bl616cl_spi_transport_exchange(
  struct bl616cl_spi_priv_s *priv, const void *txbuffer, void *rxbuffer,
  size_t nbytes)
{
  int ret;

#ifdef CONFIG_BL616CL_SPI_TEST
  if (priv->test_ops != NULL)
    {
      ret = priv->test_ops->exchange(priv->test_arg, txbuffer, rxbuffer,
                                     nbytes);
      return bl616cl_spi_normalize_error(ret);
    }
#endif

  ret = bflb_spi_poll_exchange(priv->dev, txbuffer, rxbuffer, nbytes);
  return bl616cl_spi_normalize_error(ret);
}

static bool bl616cl_spi_transport_select(
  struct bl616cl_spi_priv_s *priv, uint32_t devid, bool selected)
{
#ifdef CONFIG_BL616CL_SPI_TEST
  if (priv->test_ops != NULL)
    {
      return priv->test_ops->select(priv->test_arg, devid, selected);
    }
#endif

  return priv->board_ops != NULL && priv->board_ops->select != NULL &&
         priv->board_ops->select(priv->board_arg, devid, selected);
}

static void bl616cl_spi_recover(struct bl616cl_spi_priv_s *priv)
{
  struct bflb_spi_config_s config;

#ifdef CONFIG_BL616CL_SPI_TEST
  if (priv->test_ops != NULL)
    {
      priv->test_ops->recover(priv->test_arg);
      return;
    }
#endif

  (void)bflb_spi_feature_control(priv->dev, SPI_CMD_CLEAR_TX_FIFO, 0);
  (void)bflb_spi_feature_control(priv->dev, SPI_CMD_CLEAR_RX_FIFO, 0);
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

static int bl616cl_spi_lock(struct spi_dev_s *dev, bool lock)
{
  struct bl616cl_spi_priv_s *priv =
    (struct bl616cl_spi_priv_s *)dev;

  return lock ? nxmutex_lock(&priv->lock) : nxmutex_unlock(&priv->lock);
}

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
          bl616cl_spi_failed(priv, -ENODEV);
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
      bl616cl_spi_failed(priv, -ENODEV);
    }
}

#ifdef CONFIG_SPI_DELAY_CONTROL
static int bl616cl_spi_setdelay(struct spi_dev_s *dev, uint32_t a,
                                uint32_t b, uint32_t c, uint32_t i)
{
  return -ENOSYS;
}
#endif

static uint32_t bl616cl_spi_setfrequency(struct spi_dev_s *dev,
                                         uint32_t frequency)
{
  struct bl616cl_spi_priv_s *priv =
    (struct bl616cl_spi_priv_s *)dev;
  uint32_t actual;
  int ret;

  if (frequency == 0)
    {
      bl616cl_spi_config_failed(priv, BL616CL_SPI_CONFIG_FREQUENCY, -EINVAL);
      return 0;
    }

  actual = bl616cl_spi_actual_frequency(priv, frequency);
  if (actual == 0)
    {
      bl616cl_spi_config_failed(priv, BL616CL_SPI_CONFIG_FREQUENCY, -EIO);
      return 0;
    }

  if (frequency == priv->frequency && actual == priv->actual &&
      (priv->config_error & BL616CL_SPI_CONFIG_FREQUENCY) == 0)
    {
      return actual;
    }

  ret = bl616cl_spi_transport_feature(priv, BL616CL_SPI_TEST_FREQUENCY,
                                      actual);
  if (ret < 0)
    {
      bl616cl_spi_config_failed(priv, BL616CL_SPI_CONFIG_FREQUENCY, ret);
      return 0;
    }

  priv->frequency = frequency;
  priv->actual = actual;
  bl616cl_spi_config_succeeded(priv, BL616CL_SPI_CONFIG_FREQUENCY);
  return actual;
}

static void bl616cl_spi_setmode(struct spi_dev_s *dev,
                                enum spi_mode_e mode)
{
  struct bl616cl_spi_priv_s *priv =
    (struct bl616cl_spi_priv_s *)dev;
  int ret;

  if (mode < SPIDEV_MODE0 || mode > SPIDEV_MODE3)
    {
      bl616cl_spi_config_failed(priv, BL616CL_SPI_CONFIG_MODE, -EINVAL);
      return;
    }

  if (mode == priv->mode &&
      (priv->config_error & BL616CL_SPI_CONFIG_MODE) == 0)
    {
      return;
    }

  ret = bl616cl_spi_transport_feature(priv, BL616CL_SPI_TEST_MODE, mode);
  if (ret < 0)
    {
      bl616cl_spi_config_failed(priv, BL616CL_SPI_CONFIG_MODE, ret);
      return;
    }

  priv->mode = mode;
  bl616cl_spi_config_succeeded(priv, BL616CL_SPI_CONFIG_MODE);
}

static void bl616cl_spi_setbits(struct spi_dev_s *dev, int nbits)
{
  struct bl616cl_spi_priv_s *priv =
    (struct bl616cl_spi_priv_s *)dev;
  int ret;

  if (nbits != 8 && nbits != 16)
    {
      bl616cl_spi_config_failed(priv, BL616CL_SPI_CONFIG_BITS, -EINVAL);
      return;
    }

  if (nbits == priv->nbits &&
      (priv->config_error & BL616CL_SPI_CONFIG_BITS) == 0)
    {
      return;
    }

  ret = bl616cl_spi_transport_feature(priv, BL616CL_SPI_TEST_BITS, nbits);
  if (ret < 0)
    {
      bl616cl_spi_config_failed(priv, BL616CL_SPI_CONFIG_BITS, ret);
      return;
    }

  priv->nbits = nbits;
  bl616cl_spi_config_succeeded(priv, BL616CL_SPI_CONFIG_BITS);
}

#ifdef CONFIG_SPI_HWFEATURES
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
      bl616cl_spi_config_failed(priv, BL616CL_SPI_CONFIG_BITORDER, -ENOSYS);
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

  ret = bl616cl_spi_transport_feature(priv, BL616CL_SPI_TEST_BITORDER,
                                      lsbfirst);
  if (ret < 0)
    {
      bl616cl_spi_config_failed(priv, BL616CL_SPI_CONFIG_BITORDER, ret);
      return ret;
    }

  priv->lsbfirst = lsbfirst;
  bl616cl_spi_config_succeeded(priv, BL616CL_SPI_CONFIG_BITORDER);
  return OK;
}
#endif

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
static int bl616cl_spi_cmddata(struct spi_dev_s *dev, uint32_t devid,
                               bool cmd)
{
  return -ENOSYS;
}
#endif

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
      bl616cl_spi_failed(priv, -EINVAL);
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
      bl616cl_spi_failed(priv, -EINVAL);
      return;
    }

  nbytes = nwords * word_size;
  ret = bl616cl_spi_transport_exchange(priv, txbuffer, rxbuffer, nbytes);
  if (ret < 0)
    {
      spierr("ERROR: SPI%d polling exchange failed: %d\n", priv->port, ret);
      bl616cl_spi_failed(priv, ret);
      bl616cl_spi_recover(priv);
      return;
    }

  priv->last_error = OK;
  priv->config_error = 0;
}

#ifdef CONFIG_SPI_TRIGGER
static int bl616cl_spi_trigger(struct spi_dev_s *dev)
{
  return -ENOSYS;
}
#endif

/****************************************************************************
 * Public Functions
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
  priv->last_error = OK;
  priv->error_count = 0;
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
      if (!bl616cl_spi_transport_select(priv, priv->selected_devid, false))
        {
          bl616cl_spi_failed(priv, -ENODEV);
        }

      priv->selected = false;
    }

  bflb_spi_deinit(priv->dev);
  (void)bl616cl_spi_clock_configure(priv, false);
  priv->dev = NULL;
  priv->board_ops = NULL;
  priv->board_arg = NULL;
#ifdef CONFIG_BL616CL_SPI_TEST
  priv->test_ops = NULL;
  priv->test_arg = NULL;
#endif
  nxmutex_unlock(&priv->lock);
  nxmutex_unlock(&g_bl616cl_spi_init_lock);
  return OK;
}

int bl616cl_spi_configure_pins(int port, uint8_t miso_pin,
                               uint8_t mosi_pin)
{
  struct bl616cl_spi_priv_s *priv = bl616cl_spi_priv(port);
  uint8_t miso_group;
  uint8_t mosi_group;
  uint8_t swap;
  int ret;

  if (priv == NULL || miso_pin > 36 || mosi_pin > 36 ||
      (miso_pin % 4 != 2 && miso_pin % 4 != 3) ||
      (mosi_pin % 4 != 2 && mosi_pin % 4 != 3))
    {
      return -EINVAL;
    }

  miso_group = miso_pin / 12;
  mosi_group = mosi_pin / 12;
  if (miso_group == mosi_group && miso_pin % 4 == mosi_pin % 4)
    {
      return -EINVAL;
    }

  nxmutex_lock(&g_bl616cl_spi_init_lock);
  ret = port == 0 ?
          bl616cl_sdk_glb_spi0_global_swap(BL616CL_SDK_DISABLE) :
          bl616cl_sdk_glb_spi1_global_swap(BL616CL_SDK_DISABLE);
  if (ret != 0)
    {
      nxmutex_unlock(&g_bl616cl_spi_init_lock);
      return -EIO;
    }

  swap = miso_pin % 4 == 3;
  ret = port == 0 ?
          bl616cl_sdk_glb_spi0_sig_swap_set(miso_group, swap) :
          bl616cl_sdk_glb_spi1_sig_swap_set(miso_group, swap);
  if (ret == 0 && mosi_group != miso_group)
    {
      swap = mosi_pin % 4 == 2;
      ret = port == 0 ?
              bl616cl_sdk_glb_spi0_sig_swap_set(mosi_group, swap) :
              bl616cl_sdk_glb_spi1_sig_swap_set(mosi_group, swap);
    }

  nxmutex_unlock(&g_bl616cl_spi_init_lock);
  return ret == 0 ? OK : -EIO;
}

#ifdef CONFIG_BL616CL_SPI_TEST
static bool bl616cl_spi_test_select(void *arg, uint32_t devid,
                                    bool selected)
{
  (void)arg;
  (void)devid;
  (void)selected;
  return false;
}

static const struct bl616cl_spi_board_ops_s
g_bl616cl_spi_test_board_ops =
{
  .select = bl616cl_spi_test_select,
};

int bl616cl_spi_test_initialize(int port)
{
  return bl616cl_spibus_initialize(port, &g_bl616cl_spi_test_board_ops,
                                   NULL) == NULL ?
           -ENODEV :
           OK;
}

int bl616cl_spi_test_addref(int port)
{
  struct bl616cl_spi_priv_s *priv = bl616cl_spi_priv(port);

  if (priv == NULL)
    {
      return -ENODEV;
    }

  nxmutex_lock(&g_bl616cl_spi_init_lock);
  nxmutex_lock(&priv->lock);
  if (priv->refs == 0 || priv->refs == UINT16_MAX)
    {
      nxmutex_unlock(&priv->lock);
      nxmutex_unlock(&g_bl616cl_spi_init_lock);
      return -ENODEV;
    }

  priv->refs++;
  nxmutex_unlock(&priv->lock);
  nxmutex_unlock(&g_bl616cl_spi_init_lock);
  return OK;
}

int bl616cl_spi_test_install(int port,
                             const struct bl616cl_spi_test_ops_s *ops,
                             void *arg)
{
  struct bl616cl_spi_priv_s *priv = bl616cl_spi_priv(port);

  if (priv == NULL ||
      (ops != NULL && (ops->select == NULL || ops->feature == NULL ||
                       ops->exchange == NULL || ops->recover == NULL)))
    {
      return -EINVAL;
    }

  nxmutex_lock(&priv->lock);
  if (priv->refs == 0)
    {
      nxmutex_unlock(&priv->lock);
      return -ENODEV;
    }

  priv->test_ops = ops;
  priv->test_arg = ops == NULL ? NULL : arg;
  nxmutex_unlock(&priv->lock);
  return OK;
}

struct spi_dev_s *bl616cl_spi_test_device(int port)
{
  struct bl616cl_spi_priv_s *priv = bl616cl_spi_priv(port);
  struct spi_dev_s *spi = NULL;

  if (priv != NULL)
    {
      nxmutex_lock(&priv->lock);
      if (priv->refs > 0)
        {
          spi = &priv->spi;
        }

      nxmutex_unlock(&priv->lock);
    }

  return spi;
}

int bl616cl_spi_test_get_diag(int port,
                              struct bl616cl_spi_test_diag_s *diag)
{
  struct bl616cl_spi_priv_s *priv = bl616cl_spi_priv(port);

  if (priv == NULL || diag == NULL)
    {
      return -EINVAL;
    }

  nxmutex_lock(&priv->lock);
  if (priv->refs == 0)
    {
      nxmutex_unlock(&priv->lock);
      return -ENODEV;
    }

  diag->last_error = priv->last_error;
  diag->error_count = priv->error_count;
  diag->actual_frequency = priv->actual;
  diag->mode = priv->mode;
  diag->nbits = priv->nbits;
  diag->lsbfirst = priv->lsbfirst;
  nxmutex_unlock(&priv->lock);
  return OK;
}
#endif
