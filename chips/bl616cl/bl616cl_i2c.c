/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_i2c.c
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
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/i2c/i2c_master.h>
#include <nuttx/mutex.h>

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

/* LHAL and OpenVela use different values for ETIMEDOUT. */

#define BL616CL_LHAL_ETIMEDOUT     116
#define BL616CL_OPENVELA_ETIMEDOUT 110

#define BL616CL_I2C_GPIO_CFG(function) \
  ((function) | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)

#define BL616CL_SDK_ENABLE       1
#define BL616CL_SDK_I2C_CLK_XCLK 1

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
#ifdef CONFIG_BL616CL_I2C_TEST
  const struct bl616cl_i2c_test_ops_s *test_ops;
  void *test_arg;
  uint32_t last_status;
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bl616cl_i2c_transfer(struct i2c_master_s *dev,
                                struct i2c_msg_s *msgs, int count);

#ifdef CONFIG_I2C_RESET
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
};
#endif

#ifdef CONFIG_BL616CL_I2C1
static struct bl616cl_i2c_priv_s g_bl616cl_i2c1 =
{
  .ops = &g_bl616cl_i2c_ops,
  .lock = NXMUTEX_INITIALIZER,
  .port = 1,
};
#endif

extern int bl616cl_sdk_glb_set_i2c_clk(uint8_t enable, uint8_t clk_sel,
                                       uint8_t div)
  __asm__("GLB_Set_I2C_CLK");
extern int bl616cl_sdk_pm_disable_gpio_keep(uint32_t pin)
  __asm__("pm_disable_gpio_keep");

/****************************************************************************
 * Private Functions
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

static bool bl616cl_i2c_frequency_valid(uint32_t frequency)
{
  return frequency == BL616CL_I2C_DEFAULT_FREQUENCY ||
         frequency == BL616CL_I2C_FAST_FREQUENCY;
}

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

      if ((msgs[i].flags & I2C_M_NOSTOP) != 0)
        {
          if ((msgs[i].flags & I2C_M_READ) != 0 ||
              msgs[i].length > BL616CL_I2C_MAX_PREFIX ||
              i + 1 >= count || msgs[i + 1].flags != I2C_M_READ)
            {
              return -EOPNOTSUPP;
            }

          if (msgs[i + 1].addr != msgs[i].addr ||
              msgs[i + 1].frequency != msgs[i].frequency)
            {
              return -EINVAL;
            }

          i++;
        }
    }

  return OK;
}

static int bl616cl_i2c_real_configure(struct bl616cl_i2c_priv_s *priv,
                                      uint32_t frequency)
{
  bflb_i2c_init(priv->dev, frequency);
  return OK;
}

static int bl616cl_i2c_transport_configure(
  struct bl616cl_i2c_priv_s *priv, uint32_t frequency)
{
#ifdef CONFIG_BL616CL_I2C_TEST
  if (priv->test_ops != NULL)
    {
      return priv->test_ops->configure(priv->test_arg, frequency);
    }
#endif

  return bl616cl_i2c_real_configure(priv, frequency);
}

static int bl616cl_i2c_transport_transfer(
  struct bl616cl_i2c_priv_s *priv, struct bflb_i2c_msg_s *msgs, int count)
{
  int ret;

#ifdef CONFIG_BL616CL_I2C_TEST
  if (priv->test_ops != NULL)
    {
      struct bl616cl_i2c_test_msg_s test_msgs[2];
      int i;

      DEBUGASSERT(count > 0 && count <= 2);
      for (i = 0; i < count; i++)
        {
          test_msgs[i].addr = msgs[i].addr;
          test_msgs[i].flags = msgs[i].flags;
          test_msgs[i].buffer = msgs[i].buffer;
          test_msgs[i].length = msgs[i].length;
        }

      return priv->test_ops->transfer(priv->test_arg, test_msgs, count);
    }
#endif

  ret = bflb_i2c_transfer(priv->dev, msgs, count);
  if (ret == -BL616CL_LHAL_ETIMEDOUT)
    {
      return -BL616CL_OPENVELA_ETIMEDOUT;
    }

  return ret;
}

static uint32_t bl616cl_i2c_transport_status(
  struct bl616cl_i2c_priv_s *priv)
{
#ifdef CONFIG_BL616CL_I2C_TEST
  if (priv->test_ops != NULL)
    {
      return priv->test_ops->status(priv->test_arg);
    }
#endif

  return bflb_i2c_get_intstatus(priv->dev);
}

static void bl616cl_i2c_transport_cleanup(
  struct bl616cl_i2c_priv_s *priv)
{
#ifdef CONFIG_BL616CL_I2C_TEST
  if (priv->test_ops != NULL)
    {
      priv->test_ops->cleanup(priv->test_arg);
      return;
    }
#endif

  bflb_i2c_deinit(priv->dev);
}

static int bl616cl_i2c_transport_failed(struct bl616cl_i2c_priv_s *priv,
                                        const char *operation, int ret)
{
  uint32_t status = bl616cl_i2c_transport_status(priv);

#ifdef CONFIG_BL616CL_I2C_TEST
  priv->last_status = status;
#endif
  i2cerr("ERROR: I2C%d %s failed: ret=%d status=%08" PRIx32 "\n",
         priv->port, operation, ret, status);
  bl616cl_i2c_transport_cleanup(priv);
  return ret;
}

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

      if ((msgs[i].flags & I2C_M_NOSTOP) != 0)
        {
          lhal[i].flags |= I2C_M_NOSTOP;
        }

      lhal[i].buffer = msgs[i].buffer;
      lhal[i].length = msgs[i].length;
    }

  ret = bl616cl_i2c_transport_configure(priv, msgs[0].frequency);
  if (ret < 0)
    {
      return bl616cl_i2c_transport_failed(priv, "configure", ret);
    }

  ret = bl616cl_i2c_transport_transfer(priv, lhal, count);
  if (ret < 0)
    {
      return bl616cl_i2c_transport_failed(priv, "transfer", ret);
    }

  return ret;
}

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

#ifdef CONFIG_BL616CL_I2C_TEST
  priv->last_status = 0;
#endif
  if (priv->refs == 0)
    {
      ret = -ENODEV;
      goto out;
    }

  for (i = 0; i < count; i += group_count)
    {
      group_count = (msgs[i].flags & I2C_M_NOSTOP) != 0 ? 2 : 1;
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

  (void)bl616cl_sdk_glb_set_i2c_clk(BL616CL_SDK_ENABLE,
                                    BL616CL_SDK_I2C_CLK_XCLK, 0);
  (void)bflb_peripheral_clock_control(peripheral, true);
  (void)bl616cl_sdk_pm_disable_gpio_keep(scl_pin);
  (void)bl616cl_sdk_pm_disable_gpio_keep(sda_pin);
  bflb_gpio_init(gpio, scl_pin, BL616CL_I2C_GPIO_CFG(function));
  bflb_gpio_init(gpio, sda_pin, BL616CL_I2C_GPIO_CFG(function));
  bflb_i2c_init(priv->dev, BL616CL_I2C_DEFAULT_FREQUENCY);

  priv->scl_pin = scl_pin;
  priv->sda_pin = sda_pin;
  priv->refs = 1;
  nxmutex_unlock(&priv->lock);
  return (struct i2c_master_s *)priv;
}

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
  if (gpio != NULL)
    {
      bflb_gpio_deinit(gpio, priv->scl_pin);
      bflb_gpio_deinit(gpio, priv->sda_pin);
    }

  peripheral = priv->port == 0 ? BFLB_PERIPHERAL_I2C0 :
                                 BFLB_PERIPHERAL_I2C1;
  (void)bflb_peripheral_clock_control(peripheral, false);
  priv->dev = NULL;
  nxmutex_unlock(&priv->lock);
  return OK;
}

#ifdef CONFIG_BL616CL_I2C_TEST
int bl616cl_i2c_test_install(int port,
                             const struct bl616cl_i2c_test_ops_s *ops,
                             void *arg)
{
  struct bl616cl_i2c_priv_s *priv = bl616cl_i2c_priv(port);

  if (priv == NULL ||
      (ops != NULL && (ops->configure == NULL || ops->transfer == NULL ||
                       ops->status == NULL || ops->cleanup == NULL)))
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

struct i2c_master_s *bl616cl_i2c_test_device(int port)
{
  struct bl616cl_i2c_priv_s *priv = bl616cl_i2c_priv(port);

  return priv != NULL && priv->refs > 0 ? (struct i2c_master_s *)priv : NULL;
}

uint32_t bl616cl_i2c_test_last_status(int port)
{
  struct bl616cl_i2c_priv_s *priv = bl616cl_i2c_priv(port);
  uint32_t status;

  if (priv == NULL)
    {
      return 0;
    }

  nxmutex_lock(&priv->lock);
  status = priv->last_status;
  nxmutex_unlock(&priv->lock);
  return status;
}
#endif
