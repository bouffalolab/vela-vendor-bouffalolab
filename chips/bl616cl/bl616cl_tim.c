/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_tim.c
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
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/spinlock.h>
#include <nuttx/timers/timer.h>

#include "bflb_clock.h"
#include "bflb_irq.h"
#include "bflb_name.h"
#include "bflb_timer.h"
#include "bl616cl_tim.h"
#include "bl616cl_tim_ioctl.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_TIMER_DEFAULT_DIV 39
#define BL616CL_TIMER_MIN_TIMEOUT 2
#define BL616CL_TIMER_MAX_TIMEOUT UINT32_MAX

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bl616cl_timer_lowerhalf_s
{
  const struct timer_ops_s *ops;
  struct bflb_device_s *dev;
  tccb_t callback;
  void *arg;
  uint32_t timeout;
  uint8_t clock_div;
  bool started;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void bl616cl_timer_handler(int irq, void *arg);
static int bl616cl_timer_start(struct timer_lowerhalf_s *lower);
static int bl616cl_timer_stop(struct timer_lowerhalf_s *lower);
static int bl616cl_timer_getstatus(struct timer_lowerhalf_s *lower,
                                   struct timer_status_s *status);
static int bl616cl_timer_settimeout(struct timer_lowerhalf_s *lower,
                                    uint32_t timeout);
static void bl616cl_timer_setcallback(struct timer_lowerhalf_s *lower,
                                      tccb_t callback, void *arg);
static int bl616cl_timer_ioctl(struct timer_lowerhalf_s *lower, int cmd,
                               unsigned long arg);
static int bl616cl_timer_maxtimeout(struct timer_lowerhalf_s *lower,
                                    uint32_t *maxtimeout);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct timer_ops_s g_bl616cl_timer_ops =
{
  .start       = bl616cl_timer_start,
  .stop        = bl616cl_timer_stop,
  .getstatus   = bl616cl_timer_getstatus,
  .settimeout  = bl616cl_timer_settimeout,
  .setcallback = bl616cl_timer_setcallback,
  .ioctl       = bl616cl_timer_ioctl,
  .maxtimeout  = bl616cl_timer_maxtimeout,
};

#ifdef CONFIG_BL616CL_TIMER0
static struct bl616cl_timer_lowerhalf_s g_bl616cl_timer0 =
{
  .ops       = &g_bl616cl_timer_ops,
  .timeout   = 1000000,
  .clock_div = BL616CL_TIMER_DEFAULT_DIV,
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t bl616cl_timer_raw_compare(uint32_t timeout)
{
  return timeout - 2;
}

static void bl616cl_timer_configure(
  struct bl616cl_timer_lowerhalf_s *priv)
{
  struct bflb_timer_config_s config;

  config.counter_mode = TIMER_COUNTER_MODE_PROLOAD;
  config.clock_source = TIMER_CLKSRC_XTAL;
  config.clock_div = priv->clock_div;
  config.trigger_comp_id = TIMER_COMP_ID_0;
  config.comp0_val = priv->timeout;
  config.comp1_val = UINT32_MAX;
  config.comp2_val = UINT32_MAX;
  config.preload_val = 0;
  bflb_timer_init(priv->dev, &config);
}

static void bl616cl_timer_disable_irq(
  struct bl616cl_timer_lowerhalf_s *priv)
{
  bflb_timer_compint_mask(priv->dev, TIMER_COMP_ID_0, true);
  bflb_irq_disable(priv->dev->irq_num);
}

static void bl616cl_timer_handler(int irq, void *arg)
{
  struct bl616cl_timer_lowerhalf_s *priv = arg;
  uint32_t next_interval = 0;

  UNUSED(irq);

  if (!bflb_timer_get_compint_status(priv->dev, TIMER_COMP_ID_0))
    {
      return;
    }

  bflb_timer_compint_clear(priv->dev, TIMER_COMP_ID_0);

  if (priv->callback == NULL || !priv->callback(&next_interval, priv->arg))
    {
      (void)bl616cl_timer_stop((struct timer_lowerhalf_s *)priv);
    }
  else if (next_interval >= BL616CL_TIMER_MIN_TIMEOUT)
    {
      priv->timeout = next_interval;
      bflb_timer_set_compvalue(priv->dev, TIMER_COMP_ID_0,
                               bl616cl_timer_raw_compare(next_interval));
    }
}

static int bl616cl_timer_start(struct timer_lowerhalf_s *lower)
{
  struct bl616cl_timer_lowerhalf_s *priv =
    (struct bl616cl_timer_lowerhalf_s *)lower;
  irqstate_t flags;
  int ret;

  DEBUGASSERT(priv != NULL && priv->dev != NULL);

  if (priv->started)
    {
      return -EBUSY;
    }

  if (priv->timeout < BL616CL_TIMER_MIN_TIMEOUT)
    {
      return -EINVAL;
    }

  bflb_timer_stop(priv->dev);
  bl616cl_timer_disable_irq(priv);

  bl616cl_timer_configure(priv);

  flags = enter_critical_section();
  if (priv->callback != NULL)
    {
      ret = bflb_irq_attach(priv->dev->irq_num,
                            bl616cl_timer_handler, priv);
      if (ret < 0)
        {
          leave_critical_section(flags);
          return ret;
        }

      bflb_timer_compint_mask(priv->dev, TIMER_COMP_ID_0, false);
      bflb_irq_enable(priv->dev->irq_num);
    }
  else
    {
      bflb_timer_compint_mask(priv->dev, TIMER_COMP_ID_0, true);
    }

  bflb_timer_start(priv->dev);
  priv->started = true;
  leave_critical_section(flags);
  return OK;
}

static int bl616cl_timer_stop(struct timer_lowerhalf_s *lower)
{
  struct bl616cl_timer_lowerhalf_s *priv =
    (struct bl616cl_timer_lowerhalf_s *)lower;
  irqstate_t flags;

  DEBUGASSERT(priv != NULL && priv->dev != NULL);

  if (!priv->started)
    {
      return -ENODEV;
    }

  flags = enter_critical_section();
  bl616cl_timer_disable_irq(priv);
  (void)bflb_irq_detach(priv->dev->irq_num);
  bflb_timer_stop(priv->dev);
  priv->started = false;
  leave_critical_section(flags);
  return OK;
}

static int bl616cl_timer_getstatus(struct timer_lowerhalf_s *lower,
                                   struct timer_status_s *status)
{
  struct bl616cl_timer_lowerhalf_s *priv =
    (struct bl616cl_timer_lowerhalf_s *)lower;
  uint32_t count;
  uint32_t elapsed;

  DEBUGASSERT(priv != NULL && priv->dev != NULL && status != NULL);

  status->flags = 0;
  if (priv->started)
    {
      status->flags |= TCFLAGS_ACTIVE;
    }

  if (priv->callback != NULL)
    {
      status->flags |= TCFLAGS_HANDLER;
    }

  status->timeout = priv->timeout;
  if (priv->started)
    {
      count = bflb_timer_get_countervalue(priv->dev);
      elapsed = count < bl616cl_timer_raw_compare(priv->timeout) ? count :
                priv->timeout;
      status->timeleft = priv->timeout - elapsed;
    }
  else
    {
      status->timeleft = priv->timeout;
    }

  return OK;
}

static int bl616cl_timer_settimeout(struct timer_lowerhalf_s *lower,
                                    uint32_t timeout)
{
  struct bl616cl_timer_lowerhalf_s *priv =
    (struct bl616cl_timer_lowerhalf_s *)lower;
  irqstate_t flags;

  DEBUGASSERT(priv != NULL && priv->dev != NULL);

  if (timeout < BL616CL_TIMER_MIN_TIMEOUT)
    {
      return -EINVAL;
    }

  priv->timeout = timeout;
  if (priv->started)
    {
      flags = enter_critical_section();
      bflb_timer_stop(priv->dev);
      bl616cl_timer_configure(priv);
      if (priv->callback == NULL)
        {
          bflb_timer_compint_mask(priv->dev, TIMER_COMP_ID_0, true);
        }

      bflb_timer_start(priv->dev);
      leave_critical_section(flags);
    }

  return OK;
}

static void bl616cl_timer_setcallback(struct timer_lowerhalf_s *lower,
                                      tccb_t callback, void *arg)
{
  struct bl616cl_timer_lowerhalf_s *priv =
    (struct bl616cl_timer_lowerhalf_s *)lower;
  irqstate_t flags;
  int ret = OK;

  flags = enter_critical_section();
  priv->callback = callback;
  priv->arg = arg;
  if (priv->dev != NULL && priv->started)
    {
      if (callback != NULL)
        {
          ret = bflb_irq_attach(priv->dev->irq_num,
                                bl616cl_timer_handler, priv);
          if (ret >= 0)
            {
              bflb_timer_compint_mask(priv->dev, TIMER_COMP_ID_0, false);
              bflb_irq_enable(priv->dev->irq_num);
            }
        }
      else
        {
          bl616cl_timer_disable_irq(priv);
          (void)bflb_irq_detach(priv->dev->irq_num);
        }
    }

  leave_critical_section(flags);

  if (ret < 0)
    {
      tmrerr("ERROR: Failed to attach timer interrupt: %d\n", ret);
    }
}

static int bl616cl_timer_ioctl(struct timer_lowerhalf_s *lower, int cmd,
                               unsigned long arg)
{
  struct bl616cl_timer_lowerhalf_s *priv =
    (struct bl616cl_timer_lowerhalf_s *)lower;

  if (cmd == BL616CL_TCIOC_SETCLOCKDIV)
    {
      if (priv->started)
        {
          return -EBUSY;
        }

      if (arg > UINT8_MAX)
        {
          return -EINVAL;
        }

      priv->clock_div = (uint8_t)arg;
      return OK;
    }

  return -ENOTTY;
}

static int bl616cl_timer_maxtimeout(struct timer_lowerhalf_s *lower,
                                    uint32_t *maxtimeout)
{
  UNUSED(lower);
  DEBUGASSERT(maxtimeout != NULL);
  *maxtimeout = BL616CL_TIMER_MAX_TIMEOUT;
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bl616cl_timer_initialize(const char *devpath, uint8_t timer)
{
  struct bl616cl_timer_lowerhalf_s *priv;
  void *handle;

  DEBUGASSERT(devpath != NULL);
  PERIPHERAL_CLOCK_TIMER0_1_WDG_ENABLE();

  switch (timer)
    {
#ifdef CONFIG_BL616CL_TIMER0
      case 0:
        priv = &g_bl616cl_timer0;
        break;
#endif
      default:
        return -ENODEV;
    }

  priv->dev = bflb_device_get_by_name(timer == 0 ? BFLB_NAME_TIMER0 :
                                      BFLB_NAME_TIMER1);
  if (priv->dev == NULL)
    {
      return -ENODEV;
    }

  bflb_timer_stop(priv->dev);
  bl616cl_timer_disable_irq(priv);
  handle = timer_register(devpath, (struct timer_lowerhalf_s *)priv);
  return handle != NULL ? OK : -EEXIST;
}
