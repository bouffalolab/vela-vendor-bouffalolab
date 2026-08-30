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
#include <nuttx/clock.h>
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
  uint32_t generation;
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
static int bl616cl_timer_tick_getstatus(struct timer_lowerhalf_s *lower,
                                        struct timer_status_s *status);
static int bl616cl_timer_tick_settimeout(struct timer_lowerhalf_s *lower,
                                         uint32_t timeout);
static int bl616cl_timer_tick_maxtimeout(struct timer_lowerhalf_s *lower,
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
  .tick_getstatus  = bl616cl_timer_tick_getstatus,
  .tick_settimeout = bl616cl_timer_tick_settimeout,
  .tick_maxtimeout = bl616cl_timer_tick_maxtimeout,
};

#ifdef CONFIG_BL616CL_TIMER0
static struct bl616cl_timer_lowerhalf_s g_bl616cl_timer0 =
{
  .ops       = &g_bl616cl_timer_ops,
  .timeout   = 1000000,
  .clock_div = BL616CL_TIMER_DEFAULT_DIV,
};
#endif

#ifdef CONFIG_BL616CL_TIMER1
static struct bl616cl_timer_lowerhalf_s g_bl616cl_timer1 =
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

static void bl616cl_timer_stop_locked(
  struct bl616cl_timer_lowerhalf_s *priv)
{
  bl616cl_timer_disable_irq(priv);
  (void)bflb_irq_detach(priv->dev->irq_num);
  bflb_timer_stop(priv->dev);
  priv->started = false;
  priv->generation++;
}

static void bl616cl_timer_handler(int irq, void *arg)
{
  struct bl616cl_timer_lowerhalf_s *priv = arg;
  tccb_t callback;
  void *callback_arg;
  uint32_t generation;
  uint32_t next_interval = 0;
  irqstate_t flags;
  bool keep_running;

  UNUSED(irq);

  flags = enter_critical_section();
  if (!bflb_timer_get_compint_status(priv->dev, TIMER_COMP_ID_0))
    {
      leave_critical_section(flags);
      return;
    }

  bflb_timer_compint_clear(priv->dev, TIMER_COMP_ID_0);
  callback = priv->callback;
  callback_arg = priv->arg;
  generation = priv->generation;
  leave_critical_section(flags);

  keep_running = callback != NULL &&
                 callback(&next_interval, callback_arg);

  flags = enter_critical_section();
  if (!priv->started || priv->generation != generation)
    {
      leave_critical_section(flags);
      return;
    }

  if (!keep_running)
    {
      bl616cl_timer_stop_locked(priv);
    }
  else if (next_interval >= BL616CL_TIMER_MIN_TIMEOUT)
    {
      priv->timeout = next_interval;
      bflb_timer_set_compvalue(priv->dev, TIMER_COMP_ID_0,
                               bl616cl_timer_raw_compare(next_interval));
    }

  leave_critical_section(flags);
}

static int bl616cl_timer_start(struct timer_lowerhalf_s *lower)
{
  struct bl616cl_timer_lowerhalf_s *priv =
    (struct bl616cl_timer_lowerhalf_s *)lower;
  irqstate_t flags;
  int ret;

  DEBUGASSERT(priv != NULL && priv->dev != NULL);

  flags = enter_critical_section();
  if (priv->started)
    {
      leave_critical_section(flags);
      return -EBUSY;
    }

  if (priv->timeout < BL616CL_TIMER_MIN_TIMEOUT)
    {
      leave_critical_section(flags);
      return -EINVAL;
    }

  bflb_timer_stop(priv->dev);
  bl616cl_timer_disable_irq(priv);
  bl616cl_timer_configure(priv);

  if (priv->callback != NULL)
    {
      ret = bflb_irq_attach(priv->dev->irq_num,
                            bl616cl_timer_handler, priv);
      if (ret < 0)
        {
          bflb_timer_compint_mask(priv->dev, TIMER_COMP_ID_0, true);
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
  priv->generation++;
  leave_critical_section(flags);
  return OK;
}

static int bl616cl_timer_stop(struct timer_lowerhalf_s *lower)
{
  struct bl616cl_timer_lowerhalf_s *priv =
    (struct bl616cl_timer_lowerhalf_s *)lower;
  irqstate_t flags;

  DEBUGASSERT(priv != NULL && priv->dev != NULL);

  flags = enter_critical_section();
  if (!priv->started)
    {
      leave_critical_section(flags);
      return -ENODEV;
    }

  bl616cl_timer_stop_locked(priv);
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
  irqstate_t flags;

  DEBUGASSERT(priv != NULL && priv->dev != NULL && status != NULL);

  flags = enter_critical_section();
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

  leave_critical_section(flags);
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

  flags = enter_critical_section();
  priv->timeout = timeout;
  priv->generation++;
  if (priv->started)
    {
      bflb_timer_stop(priv->dev);
      bl616cl_timer_configure(priv);
      if (priv->callback == NULL)
        {
          bflb_timer_compint_mask(priv->dev, TIMER_COMP_ID_0, true);
        }

      bflb_timer_start(priv->dev);
    }

  leave_critical_section(flags);
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
  if (priv->dev != NULL && priv->started)
    {
      if (callback != NULL)
        {
          ret = bflb_irq_attach(priv->dev->irq_num,
                                bl616cl_timer_handler, priv);
          if (ret >= 0)
            {
              priv->callback = callback;
              priv->arg = arg;
              priv->generation++;
              bflb_timer_compint_mask(priv->dev, TIMER_COMP_ID_0, false);
              bflb_irq_enable(priv->dev->irq_num);
            }
        }
      else
        {
          priv->callback = NULL;
          priv->arg = NULL;
          priv->generation++;
          bl616cl_timer_disable_irq(priv);
          (void)bflb_irq_detach(priv->dev->irq_num);
        }
    }
  else
    {
      priv->callback = callback;
      priv->arg = arg;
      priv->generation++;
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
  irqstate_t flags;
  int ret = -ENOTTY;

  flags = enter_critical_section();
  if (cmd == BL616CL_TCIOC_SETCLOCKDIV)
    {
      if (priv->started)
        {
          ret = -EBUSY;
          goto out;
        }

      if (arg > UINT8_MAX)
        {
          ret = -EINVAL;
          goto out;
        }

      priv->clock_div = (uint8_t)arg;
      ret = OK;
    }

out:
  leave_critical_section(flags);
  return ret;
}

static int bl616cl_timer_maxtimeout(struct timer_lowerhalf_s *lower,
                                    uint32_t *maxtimeout)
{
  UNUSED(lower);
  DEBUGASSERT(maxtimeout != NULL);
  *maxtimeout = BL616CL_TIMER_MAX_TIMEOUT;
  return OK;
}

static uint32_t bl616cl_timer_usec_to_ticks(uint32_t usec)
{
  uint32_t tick_usec = (uint32_t)USEC_PER_TICK;

  return usec / tick_usec + (usec % tick_usec != 0);
}

static int bl616cl_timer_tick_getstatus(struct timer_lowerhalf_s *lower,
                                        struct timer_status_s *status)
{
  int ret;

  ret = bl616cl_timer_getstatus(lower, status);
  if (ret >= 0)
    {
      status->timeout = bl616cl_timer_usec_to_ticks(status->timeout);
      status->timeleft = bl616cl_timer_usec_to_ticks(status->timeleft);
    }

  return ret;
}

static int bl616cl_timer_tick_settimeout(struct timer_lowerhalf_s *lower,
                                         uint32_t timeout)
{
  uint32_t tick_usec = (uint32_t)USEC_PER_TICK;

  if (timeout == 0)
    {
      return -EINVAL;
    }

  if (timeout > BL616CL_TIMER_MAX_TIMEOUT / tick_usec)
    {
      return -ERANGE;
    }

  return bl616cl_timer_settimeout(lower, timeout * tick_usec);
}

static int bl616cl_timer_tick_maxtimeout(struct timer_lowerhalf_s *lower,
                                         uint32_t *maxtimeout)
{
  UNUSED(lower);
  DEBUGASSERT(maxtimeout != NULL);
  *maxtimeout = BL616CL_TIMER_MAX_TIMEOUT / (uint32_t)USEC_PER_TICK;
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bl616cl_timer_initialize(const char *devpath, uint8_t timer)
{
  struct bl616cl_timer_lowerhalf_s *priv;
  void *handle;
  irqstate_t flags;

  DEBUGASSERT(devpath != NULL);
  PERIPHERAL_CLOCK_TIMER0_1_WDG_ENABLE();

  switch (timer)
    {
#ifdef CONFIG_BL616CL_TIMER0
      case 0:
        priv = &g_bl616cl_timer0;
        break;
#endif
#ifdef CONFIG_BL616CL_TIMER1
      case 1:
        priv = &g_bl616cl_timer1;
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

  flags = enter_critical_section();
  bflb_timer_stop(priv->dev);
  bl616cl_timer_disable_irq(priv);
  leave_critical_section(flags);
  handle = timer_register(devpath, (struct timer_lowerhalf_s *)priv);
  return handle != NULL ? OK : -EEXIST;
}

#ifdef CONFIG_BL616CL_TIMER_TEST
struct timer_lowerhalf_s *bl616cl_timer_test_lower(uint8_t timer)
{
  switch (timer)
    {
#ifdef CONFIG_BL616CL_TIMER0
      case 0:
        return (struct timer_lowerhalf_s *)&g_bl616cl_timer0;
#endif
#ifdef CONFIG_BL616CL_TIMER1
      case 1:
        return (struct timer_lowerhalf_s *)&g_bl616cl_timer1;
#endif
      default:
        return NULL;
    }
}
#endif
