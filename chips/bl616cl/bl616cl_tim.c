/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_tim.c
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

#include "bl616cl_lhal.h"
#include "bflb_clock.h"
#include "bflb_irq.h"
#include "bflb_name.h"
#include "bflb_timer.h"
#include "bl616cl_tim.h"
#include <arch/chip/bl616cl_timer.h>

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

/****************************************************************************
 * Name: bl616cl_timer_raw_compare
 *
 * Description:
 *   Convert a timeout to the raw value written to the comparator: the timeout
 *   minus 2.
 *
 * Input Parameters:
 *   timeout - Timeout in timer counts (microseconds).
 *
 * Returned Value:
 *   The raw comparator value.
 *
 ****************************************************************************/

static uint32_t bl616cl_timer_raw_compare(uint32_t timeout)
{
  return timeout - 2;
}

/****************************************************************************
 * Name: bl616cl_timer_configure
 *
 * Description:
 *   Initialize the hardware timer with bflb_timer_init(): preload counter
 *   mode, XTAL clock with the configured divider, comparator 0 set to the
 *   current timeout and the other comparators at their maximum.
 *
 * Input Parameters:
 *   priv - Timer lower half to configure.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: bl616cl_timer_disable_irq
 *
 * Description:
 *   Mask the comparator 0 interrupt and disable the timer interrupt line.
 *
 * Input Parameters:
 *   priv - Timer lower half.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_timer_disable_irq(
  struct bl616cl_timer_lowerhalf_s *priv)
{
  bflb_timer_compint_mask(priv->dev, TIMER_COMP_ID_0, true);
  bflb_irq_disable(priv->dev->irq_num);
}

/****************************************************************************
 * Name: bl616cl_timer_stop_locked
 *
 * Description:
 *   Stop the timer: disable and detach the interrupt, stop the hardware and
 *   increment the generation count so that a running callback notices the
 *   change. The caller must be in a critical section.
 *
 * Input Parameters:
 *   priv - Timer lower half.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_timer_stop_locked(
  struct bl616cl_timer_lowerhalf_s *priv)
{
  bl616cl_timer_disable_irq(priv);
  bflb_irq_detach(priv->dev->irq_num);
  bflb_timer_stop(priv->dev);
  priv->started = false;
  priv->generation++;
}

/****************************************************************************
 * Name: bl616cl_timer_handler
 *
 * Description:
 *   Timer comparator interrupt handler. Acknowledge the interrupt and call
 *   the registered callback outside the critical section. If the callback
 *   returns false the timer is stopped; otherwise the timeout is updated to
 *   the interval it returned when that is at least BL616CL_TIMER_MIN_TIMEOUT.
 *   Nothing is changed if the timer was stopped or reconfigured during the
 *   callback.
 *
 * Input Parameters:
 *   irq - IRQ number (unused).
 *   arg - The bl616cl_timer_lowerhalf_s instance.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: bl616cl_timer_start
 *
 * Description:
 *   Implement the timer_ops_s start method. Configure the hardware, attach
 *   and enable the interrupt if a callback is registered, and start the
 *   timer.
 *
 * Input Parameters:
 *   lower - A pointer to the publicly visible representation of the timer
 *           lower half.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure:
 *
 *     -EBUSY  - The timer is already started.
 *     -EINVAL - The timeout is below BL616CL_TIMER_MIN_TIMEOUT.
 *     Other errors are returned from bflb_irq_attach().
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: bl616cl_timer_stop
 *
 * Description:
 *   Implement the timer_ops_s stop method.
 *
 * Input Parameters:
 *   lower - A pointer to the publicly visible representation of the timer
 *           lower half.
 *
 * Returned Value:
 *   Zero (OK) on success; -ENODEV if the timer is not started.
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: bl616cl_timer_getstatus
 *
 * Description:
 *   Implement the timer_ops_s getstatus method. Report the TCFLAGS_ACTIVE and
 *   TCFLAGS_HANDLER flags, the timeout and the time left, in microseconds.
 *
 * Input Parameters:
 *   lower - A pointer to the publicly visible representation of the timer
 *           lower half.
 *   status - Location to return the timer status.
 *
 * Returned Value:
 *   Always OK.
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: bl616cl_timer_settimeout
 *
 * Description:
 *   Implement the timer_ops_s settimeout method. If the timer is running it
 *   is stopped, reconfigured with the new timeout and started again.
 *
 * Input Parameters:
 *   lower - A pointer to the publicly visible representation of the timer
 *           lower half.
 *   timeout - The new timeout in microseconds.
 *
 * Returned Value:
 *   Zero (OK) on success; -EINVAL if the timeout is below
 *   BL616CL_TIMER_MIN_TIMEOUT.
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: bl616cl_timer_setcallback
 *
 * Description:
 *   Implement the timer_ops_s setcallback method. If the timer is running,
 *   the interrupt is attached and enabled for a non-NULL callback, or
 *   disabled and detached for a NULL one. An interrupt attach failure is
 *   logged and the callback is not changed.
 *
 * Input Parameters:
 *   lower - A pointer to the publicly visible representation of the timer
 *           lower half.
 *   callback - The new timer expiration function, or NULL to remove it.
 *   arg - Argument passed to the callback.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

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
          bflb_irq_detach(priv->dev->irq_num);
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

/****************************************************************************
 * Name: bl616cl_timer_ioctl
 *
 * Description:
 *   Implement the timer_ops_s ioctl method. The only supported command is
 *   BL616CL_TCIOC_SETCLOCKDIV, which sets the timer clock divider and can be
 *   used only while the timer is stopped.
 *
 * Input Parameters:
 *   lower - A pointer to the publicly visible representation of the timer
 *           lower half.
 *   cmd - The ioctl command.
 *   arg - The command argument; for BL616CL_TCIOC_SETCLOCKDIV the divider
 *         value (0 to 255).
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure:
 *
 *     -EBUSY  - The timer is running.
 *     -EINVAL - The divider is larger than 255.
 *     -ENOTTY - The command is not supported.
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: bl616cl_timer_maxtimeout
 *
 * Description:
 *   Implement the timer_ops_s maxtimeout method.
 *
 * Input Parameters:
 *   lower - A pointer to the publicly visible representation of the timer
 *           lower half.
 *   maxtimeout - Location to return the maximum timeout in microseconds.
 *
 * Returned Value:
 *   Always OK.
 *
 ****************************************************************************/

static int bl616cl_timer_maxtimeout(struct timer_lowerhalf_s *lower,
                                    uint32_t *maxtimeout)
{
  UNUSED(lower);
  DEBUGASSERT(maxtimeout != NULL);
  *maxtimeout = BL616CL_TIMER_MAX_TIMEOUT;
  return OK;
}

/****************************************************************************
 * Name: bl616cl_timer_usec_to_ticks
 *
 * Description:
 *   Convert microseconds to system clock ticks, rounding up.
 *
 * Input Parameters:
 *   usec - Time in microseconds.
 *
 * Returned Value:
 *   The time in system ticks (USEC_PER_TICK microseconds each).
 *
 ****************************************************************************/

static uint32_t bl616cl_timer_usec_to_ticks(uint32_t usec)
{
  uint32_t tick_usec = (uint32_t)USEC_PER_TICK;

  return usec / tick_usec + (usec % tick_usec != 0);
}

/****************************************************************************
 * Name: bl616cl_timer_tick_getstatus
 *
 * Description:
 *   Implement the timer_ops_s tick_getstatus method. Get the status in
 *   microseconds and convert the timeout and time left to system ticks.
 *
 * Input Parameters:
 *   lower - A pointer to the publicly visible representation of the timer
 *           lower half.
 *   status - Location to return the timer status.
 *
 * Returned Value:
 *   The result of bl616cl_timer_getstatus().
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: bl616cl_timer_tick_settimeout
 *
 * Description:
 *   Implement the timer_ops_s tick_settimeout method. Convert the timeout
 *   from system ticks to microseconds and set it.
 *
 * Input Parameters:
 *   lower - A pointer to the publicly visible representation of the timer
 *           lower half.
 *   timeout - The new timeout in system ticks.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure:
 *
 *     -EINVAL - The timeout is zero.
 *     -ERANGE - The timeout does not fit in 32 bits of microseconds.
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: bl616cl_timer_tick_maxtimeout
 *
 * Description:
 *   Implement the timer_ops_s tick_maxtimeout method.
 *
 * Input Parameters:
 *   lower - A pointer to the publicly visible representation of the timer
 *           lower half.
 *   maxtimeout - Location to return the maximum timeout in system ticks.
 *
 * Returned Value:
 *   Always OK.
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: bl616cl_timer_initialize
 *
 * Description:
 *   Register a hardware timer as a timer driver device. Enable the timer
 *   0/1/watchdog peripheral clock, stop the timer with its interrupt masked
 *   and register it with timer_register().
 *
 * Input Parameters:
 *   devpath - The device path to register, for example /dev/timer0.
 *   timer - Timer index: 0 for TIMER0 or 1 for TIMER1; it must be enabled by
 *           CONFIG_BL616CL_TIMER0 or CONFIG_BL616CL_TIMER1.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure:
 *
 *     -ENODEV - The timer is not enabled in the configuration or its lhal
 *               device was not found.
 *     -EEXIST - timer_register() failed.
 *
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
/****************************************************************************
 * Name: bl616cl_timer_test_lower
 *
 * Description:
 *   Return the lower half of a timer so that test code can call its
 *   operations directly. Available only with CONFIG_BL616CL_TIMER_TEST.
 *
 * Input Parameters:
 *   timer - Timer index, 0 or 1.
 *
 * Returned Value:
 *   The timer lower half; NULL if that timer is not enabled.
 *
 ****************************************************************************/

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
