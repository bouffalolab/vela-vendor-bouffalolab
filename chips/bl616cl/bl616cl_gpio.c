/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_gpio.c
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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <stdint.h>

#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>
#include <nuttx/ioexpander/ioexpander.h>

#include "bflb_gpio.h"
#include "bflb_irq.h"

#include "bl616cl_gpio.h"

#ifdef CONFIG_BL616CL_GPIO

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define GPIO_CFGSET_INPUT_FLOAT \
  (GPIO_INPUT | GPIO_FLOAT | GPIO_SMT_EN)
#define GPIO_CFGSET_INPUT_PULLUP \
  (GPIO_INPUT | GPIO_PULLUP | GPIO_SMT_EN)
#define GPIO_CFGSET_INPUT_PULLDOWN \
  (GPIO_INPUT | GPIO_PULLDOWN | GPIO_SMT_EN)
#define GPIO_CFGSET_OUTPUT \
  (GPIO_INPUT | GPIO_OUTPUT | GPIO_PULLUP | GPIO_DRV_1 | GPIO_SMT_EN)
#define GPIO_CFGSET_OUTPUT_OPENDRAIN \
  (GPIO_INPUT | GPIO_OUTPUT | GPIO_FLOAT | GPIO_DRV_1 | GPIO_SMT_EN)

/* Interrupt input configuration shared by the INTCFG options */

#define GPIO_CFGSET_INT_PULLDOWN \
  (GPIO_INPUT | GPIO_PULLDOWN | GPIO_DRV_1 | GPIO_SMT_EN)
#define GPIO_CFGSET_INT_PULLUP \
  (GPIO_INPUT | GPIO_PULLUP | GPIO_DRV_1 | GPIO_SMT_EN)

/* Short alias: the full trig-mode macro name exceeds 80 columns */

#define GPIO_TRIG_BOTH_EDGE \
  GPIO_INT_TRIG_MODE_SYNC_FALLING_RISING_EDGE

/****************************************************************************
 * Private Types
 ****************************************************************************/

#ifdef CONFIG_IOEXPANDER_INT_ENABLE
/* This type represents one registered pin interrupt callback */

struct bl616cl_ioe_callback_s
{
  ioe_pinset_t pinset;          /* Set of pin interrupts registered */
  ioe_callback_t cbfunc;        /* Saved callback function pointer */
  FAR void *cbarg;              /* Saved callback argument */
};
#endif

/* This structure represents the state of the BL616CL I/O expander driver */

struct bl616cl_gpio_dev_s
{
  struct ioexpander_dev_s dev;    /* Nested structure as public device */
  FAR struct bflb_device_s *gpio; /* LHAL GPIO device */
  ioe_pinset_t inpins;            /* Bitmap of pins set as inputs */
  ioe_pinset_t invert;            /* Pin value inversion bitmap */
  mutex_t lock;                   /* Mutual exclusion */

#ifdef CONFIG_IOEXPANDER_INT_ENABLE
  struct work_s work;           /* Interrupt bottom half work */
  struct bl616cl_ioe_callback_s
       cb[CONFIG_BL616CL_GPIO_INT_NCALLBACKS];
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bl616cl_gpio_direction(FAR struct ioexpander_dev_s *dev,
                                  uint8_t pin, int direction);
static int bl616cl_gpio_option(FAR struct ioexpander_dev_s *dev,
                               uint8_t pin, int opt, FAR void *value);
static int bl616cl_gpio_writepin(FAR struct ioexpander_dev_s *dev,
                                 uint8_t pin, bool value);
static int bl616cl_gpio_readpin(FAR struct ioexpander_dev_s *dev,
                                uint8_t pin, FAR bool *value);
static int bl616cl_gpio_readbuf(FAR struct ioexpander_dev_s *dev,
                                uint8_t pin, FAR bool *value);
#ifdef CONFIG_IOEXPANDER_INT_ENABLE
static FAR void *bl616cl_gpio_attach(FAR struct ioexpander_dev_s *dev,
                                     ioe_pinset_t pinset,
                                     ioe_callback_t callback,
                                     FAR void *arg);
static int bl616cl_gpio_detach(FAR struct ioexpander_dev_s *dev,
                               FAR void *handle);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct ioexpander_ops_s g_bl616cl_gpio_ops =
{
  bl616cl_gpio_direction,
  bl616cl_gpio_option,
  bl616cl_gpio_writepin,
  bl616cl_gpio_readpin,
  bl616cl_gpio_readbuf,
#ifdef CONFIG_IOEXPANDER_MULTIPIN
  NULL,
  NULL,
  NULL,
#endif
#ifdef CONFIG_IOEXPANDER_INT_ENABLE
  bl616cl_gpio_attach,
  bl616cl_gpio_detach,
#endif
};

static struct bl616cl_gpio_dev_s g_bl616cl_gpio =
{
  .lock = NXMUTEX_INITIALIZER,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_gpio_direction
 *
 * Description:
 *   Set the direction of an ioexpander pin.
 *
 * Input Parameters:
 *   dev - Device-specific state data
 *   pin - The index of the pin to alter in this call
 *   dir - One of the IOEXPANDER_DIRECTION_ macros
 *
 * Returned Value:
 *   0 on success, else a negative error code
 *
 ****************************************************************************/

static int bl616cl_gpio_direction(FAR struct ioexpander_dev_s *dev,
                                  uint8_t pin, int direction)
{
  FAR struct bl616cl_gpio_dev_s *priv =
    (FAR struct bl616cl_gpio_dev_s *)dev;
  uint32_t cfgset;
  int ret;

  if (direction < IOEXPANDER_DIRECTION_IN ||
      direction > IOEXPANDER_DIRECTION_OUT_OPENDRAIN)
    {
      return -EINVAL;
    }

  gpioinfo("pin=%u direction=%u\n", pin, direction);

  DEBUGASSERT(pin < CONFIG_IOEXPANDER_NPINS);

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (direction <= IOEXPANDER_DIRECTION_IN_PULLDOWN)
    {
      priv->inpins |= ((ioe_pinset_t)1 << pin);

      cfgset = GPIO_CFGSET_INPUT_FLOAT;
      if (direction == IOEXPANDER_DIRECTION_IN_PULLUP)
        {
          cfgset = GPIO_CFGSET_INPUT_PULLUP;
        }
      else if (direction == IOEXPANDER_DIRECTION_IN_PULLDOWN)
        {
          cfgset = GPIO_CFGSET_INPUT_PULLDOWN;
        }
    }
  else
    {
      priv->inpins &= ~((ioe_pinset_t)1 << pin);

      if (direction == IOEXPANDER_DIRECTION_OUT)
        {
          cfgset = GPIO_CFGSET_OUTPUT;
        }
      else
        {
          cfgset = GPIO_CFGSET_OUTPUT_OPENDRAIN;
        }
    }

  bflb_gpio_init(priv->gpio, pin, cfgset);

  nxmutex_unlock(&priv->lock);
  return OK;
}

/****************************************************************************
 * Name: bl616cl_gpio_option
 *
 * Description:
 *   Set a pin option, e.g. polarity inversion or interrupt configuration.
 *
 * Input Parameters:
 *   dev   - Device-specific state data
 *   pin   - The index of the pin to alter in this call
 *   opt   - One of the IOEXPANDER_OPTION_ macros
 *   val   - The option value
 *
 * Returned Value:
 *   0 on success, else a negative error code
 *
 ****************************************************************************/

static int bl616cl_gpio_option(FAR struct ioexpander_dev_s *dev,
                               uint8_t pin, int opt, FAR void *value)
{
  FAR struct bl616cl_gpio_dev_s *priv =
    (FAR struct bl616cl_gpio_dev_s *)dev;
  uintptr_t val = (uintptr_t)value;
  int ret;

  gpioinfo("pin=%u option=%u value=%lu\n", pin, opt,
           (unsigned long)val);

  DEBUGASSERT(pin < CONFIG_IOEXPANDER_NPINS);

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  switch (opt)
    {
      case IOEXPANDER_OPTION_INVERT:
        {
          if (val == IOEXPANDER_VAL_INVERT)
            {
              priv->invert |= ((ioe_pinset_t)1 << pin);
            }
          else
            {
              priv->invert &= ~((ioe_pinset_t)1 << pin);
            }

          ret = OK;
        }
        break;

#ifdef CONFIG_IOEXPANDER_INT_ENABLE
      case IOEXPANDER_OPTION_INTCFG:
        {
          switch (val)
            {
              case IOEXPANDER_VAL_HIGH:
                bflb_gpio_init(priv->gpio, pin, GPIO_CFGSET_INT_PULLDOWN);
                bflb_gpio_int_init(priv->gpio, pin,
                                   GPIO_INT_TRIG_MODE_SYNC_HIGH_LEVEL);
                break;

              case IOEXPANDER_VAL_LOW:
                bflb_gpio_init(priv->gpio, pin, GPIO_CFGSET_INT_PULLUP);
                bflb_gpio_int_init(priv->gpio, pin,
                                   GPIO_INT_TRIG_MODE_SYNC_LOW_LEVEL);
                break;

              case IOEXPANDER_VAL_RISING:
                bflb_gpio_init(priv->gpio, pin, GPIO_CFGSET_INT_PULLDOWN);
                bflb_gpio_int_init(priv->gpio, pin,
                                   GPIO_INT_TRIG_MODE_SYNC_RISING_EDGE);
                break;

              case IOEXPANDER_VAL_FALLING:
                bflb_gpio_init(priv->gpio, pin, GPIO_CFGSET_INT_PULLUP);
                bflb_gpio_int_init(priv->gpio, pin,
                                   GPIO_INT_TRIG_MODE_SYNC_FALLING_EDGE);
                break;

              case IOEXPANDER_VAL_BOTH:
                bflb_gpio_init(priv->gpio, pin, GPIO_CFGSET_INT_PULLUP);
                bflb_gpio_int_init(priv->gpio, pin,
                                   GPIO_TRIG_BOTH_EDGE);
                break;

              case IOEXPANDER_VAL_DISABLE:
                bflb_gpio_int_mask(priv->gpio, pin, true);
                break;

              default:
                ret = -EINVAL;
                break;
            }

          if (ret >= 0)
            {
              /* Trigger configuration also arms the interrupt source */

              bflb_gpio_int_mask(priv->gpio, pin, false);
            }
        }
        break;
#endif

      default:
        gpiowarn("WARNING: Unrecognized option: %d\n", opt);
        ret = -ENOSYS;
        break;
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

/****************************************************************************
 * Name: bl616cl_gpio_writepin
 *
 * Description:
 *   Write a PIN level, accounting for polarity inversion.
 *
 * Input Parameters:
 *   dev   - Device-specific state data
 *   pin   - The index of the pin
 *   value - The PIN level
 *
 * Returned Value:
 *   0 on success, else a negative error code
 *
 ****************************************************************************/

static int bl616cl_gpio_writepin(FAR struct ioexpander_dev_s *dev,
                                 uint8_t pin, bool value)
{
  FAR struct bl616cl_gpio_dev_s *priv =
    (FAR struct bl616cl_gpio_dev_s *)dev;
  int ret;

  gpioinfo("pin=%u value=%u\n", pin, value);

  DEBUGASSERT(pin < CONFIG_IOEXPANDER_NPINS);

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  value ^= ((priv->invert >> pin) & 1);
  if (value)
    {
      bflb_gpio_set(priv->gpio, pin);
    }
  else
    {
      bflb_gpio_reset(priv->gpio, pin);
    }

  nxmutex_unlock(&priv->lock);
  return OK;
}

/****************************************************************************
 * Name: bl616cl_gpio_readpin
 *
 * Description:
 *   Read the actual PIN level. This can be different from the last value
 *   written to this pin.
 *
 * Input Parameters:
 *   dev    - Device-specific state data
 *   pin    - The index of the pin
 *   valptr - Pointer to a buffer where the pin level is stored
 *
 * Returned Value:
 *   0 on success, else a negative error code
 *
 ****************************************************************************/

static int bl616cl_gpio_readpin(FAR struct ioexpander_dev_s *dev,
                                uint8_t pin, FAR bool *value)
{
  FAR struct bl616cl_gpio_dev_s *priv =
    (FAR struct bl616cl_gpio_dev_s *)dev;
  int ret;

  gpioinfo("pin=%u\n", pin);

  DEBUGASSERT(pin < CONFIG_IOEXPANDER_NPINS && value != NULL);

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  *value = bflb_gpio_read(priv->gpio, pin) ^
           ((priv->invert >> pin) & 1);

  nxmutex_unlock(&priv->lock);
  return OK;
}

/****************************************************************************
 * Name: bl616cl_gpio_readbuf
 *
 * Description:
 *   Read the buffered PIN level. The BL616CL GPIO controller has no output
 *   latch readable back, so this returns the actual PIN level like
 *   readpin().
 *
 * Input Parameters:
 *   dev    - Device-specific state data
 *   pin    - The index of the pin
 *   valptr - Pointer to a buffer where the pin level is stored
 *
 * Returned Value:
 *   0 on success, else a negative error code
 *
 ****************************************************************************/

static int bl616cl_gpio_readbuf(FAR struct ioexpander_dev_s *dev,
                                uint8_t pin, FAR bool *value)
{
  return bl616cl_gpio_readpin(dev, pin, value);
}

#ifdef CONFIG_IOEXPANDER_INT_ENABLE
/****************************************************************************
 * Name: bl616cl_gpio_pin_from_pinset
 *
 * Description:
 *   Extract the single pin number encoded in a pinset bitmap.
 *
 * Returned Value:
 *   The pin number on success; a negated errno value if the pinset is
 *   empty or selects more than one pin.
 *
 ****************************************************************************/

static int bl616cl_gpio_pin_from_pinset(ioe_pinset_t pinset)
{
  if (pinset == 0 || (pinset & (pinset - 1)) != 0)
    {
      return -EINVAL;
    }

#if CONFIG_IOEXPANDER_NPINS <= 32
  return __builtin_ctz((uint32_t)pinset);
#else
  return __builtin_ctzll((unsigned long long)pinset);
#endif
}

/****************************************************************************
 * Name: bl616cl_gpio_attach
 *
 * Description:
 *   Attach a pin interrupt callback function.
 *
 * Input Parameters:
 *   dev      - Device-specific state data
 *   pinset   - The set of pin events that will generate the callback
 *   callback - The pointer to callback function. NULL will detach.
 *   arg      - Callback private argument
 *
 * Returned Value:
 *   An opaque handle on success; NULL on failure.
 *
 ****************************************************************************/

static FAR void *bl616cl_gpio_attach(FAR struct ioexpander_dev_s *dev,
                                     ioe_pinset_t pinset,
                                     ioe_callback_t callback,
                                     FAR void *arg)
{
  FAR struct bl616cl_gpio_dev_s *priv =
    (FAR struct bl616cl_gpio_dev_s *)dev;
  FAR struct bl616cl_ioe_callback_s *cb = NULL;
  irqstate_t flags;
  int pin;
  int i;

  gpioinfo("pinset=%llx callback=%p arg=%p\n",
           (unsigned long long)pinset, callback, arg);

  pin = bl616cl_gpio_pin_from_pinset(pinset);
  if (pin < 0 || callback == NULL)
    {
      return NULL;
    }

  flags = enter_critical_section();

  for (i = 0; i < CONFIG_BL616CL_GPIO_INT_NCALLBACKS; i++)
    {
      if (priv->cb[i].cbfunc == NULL)
        {
          priv->cb[i].pinset = pinset;
          priv->cb[i].cbfunc = callback;
          priv->cb[i].cbarg  = arg;
          cb                 = &priv->cb[i];
          break;
        }
    }

  if (cb != NULL)
    {
      /* Arm the pin interrupt by clearing the mask bit */

      bflb_gpio_int_mask(priv->gpio, pin, false);
    }

  leave_critical_section(flags);

  return cb;
}

/****************************************************************************
 * Name: bl616cl_gpio_detach
 *
 * Description:
 *   Detach and disable a pin interrupt callback function.
 *
 * Input Parameters:
 *   dev    - Device-specific state data
 *   handle - The non-NULL opaque value returned by bl616cl_gpio_attach()
 *
 * Returned Value:
 *   0 on success, else a negative error code
 *
 ****************************************************************************/

static int bl616cl_gpio_detach(FAR struct ioexpander_dev_s *dev,
                               FAR void *handle)
{
  FAR struct bl616cl_gpio_dev_s *priv =
    (FAR struct bl616cl_gpio_dev_s *)dev;
  FAR struct bl616cl_ioe_callback_s *cb =
    (FAR struct bl616cl_ioe_callback_s *)handle;
  irqstate_t flags;
  int pin;
  int i;

  gpioinfo("handle=%p\n", handle);

  DEBUGASSERT(cb != NULL);

  pin = bl616cl_gpio_pin_from_pinset(cb->pinset);
  if (pin < 0)
    {
      return pin;
    }

  bflb_gpio_int_mask(priv->gpio, pin, true);

  flags = enter_critical_section();

  cb->pinset = 0;
  cb->cbfunc = NULL;
  cb->cbarg  = NULL;

  leave_critical_section(flags);

  UNUSED(i);
  return OK;
}

/****************************************************************************
 * Name: bl616cl_gpio_irqworker
 *
 * Description:
 *   Handle GPIO interrupt events. This function executes in the context of
 *   the high priority worker thread.
 *
 ****************************************************************************/

static void bl616cl_gpio_irqworker(FAR void *arg)
{
  FAR struct bl616cl_gpio_dev_s *priv =
    (FAR struct bl616cl_gpio_dev_s *)arg;
  int i;

  for (i = 0; i < CONFIG_BL616CL_GPIO_INT_NCALLBACKS; i++)
    {
      FAR struct bl616cl_ioe_callback_s *cb = &priv->cb[i];
      int pin;

      if (cb->cbfunc == NULL)
        {
          continue;
        }

      pin = bl616cl_gpio_pin_from_pinset(cb->pinset);
      if (pin < 0)
        {
          continue;
        }

      if (bflb_gpio_get_intstatus(priv->gpio, pin))
        {
          bflb_gpio_int_clear(priv->gpio, pin);

          gpioinfo("interrupt pin=%d\n", pin);

          cb->cbfunc(&priv->dev, cb->pinset, cb->cbarg);
        }
    }

  /* Re-enable the GPIO controller interrupt */

  bflb_irq_enable(priv->gpio->irq_num);
}

/****************************************************************************
 * Name: bl616cl_gpio_interrupt
 *
 * Description:
 *   Handle the GPIO controller interrupt. This defers processing to the
 *   high priority worker thread and masks the controller IRQ until the
 *   work completes.
 *
 ****************************************************************************/

static void bl616cl_gpio_interrupt(int irq, FAR void *arg)
{
  FAR struct bl616cl_gpio_dev_s *priv =
    (FAR struct bl616cl_gpio_dev_s *)arg;

  DEBUGASSERT(priv != NULL);

  if (work_available(&priv->work))
    {
      /* Mask further interrupts until the worker finishes */

      bflb_irq_disable(priv->gpio->irq_num);

      work_queue(HPWORK, &priv->work, bl616cl_gpio_irqworker,
                 priv, 0);
    }
}
#endif /* CONFIG_IOEXPANDER_INT_ENABLE */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_gpio_initialize
 *
 * Description:
 *   Initialize the BL616CL GPIO controller as an I/O expander device.
 *   See the header file for the detailed contract.
 *
 * Returned Value:
 *   A pointer to the I/O expander instance on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct ioexpander_dev_s *bl616cl_gpio_initialize(void)
{
  FAR struct bl616cl_gpio_dev_s *priv = &g_bl616cl_gpio;

  if (priv->gpio != NULL)
    {
      return &priv->dev;
    }

  priv->gpio = bflb_device_get_by_name(BFLB_NAME_GPIO);
  if (priv->gpio == NULL)
    {
      gpioerr("ERROR: Failed to get GPIO device\n");
      return NULL;
    }

  priv->dev.ops = &g_bl616cl_gpio_ops;

#ifdef CONFIG_IOEXPANDER_INT_ENABLE
  memset(priv->cb, 0, sizeof(priv->cb));

  /* Attach the GPIO controller interrupt and enable it */

  if (bflb_irq_attach(priv->gpio->irq_num, bl616cl_gpio_interrupt,
                      priv) != OK)
    {
      gpioerr("ERROR: Failed to attach GPIO interrupt\n");
      return NULL;
    }

  bflb_irq_enable(priv->gpio->irq_num);
#endif

  return &priv->dev;
}

#endif /* CONFIG_BL616CL_GPIO */
