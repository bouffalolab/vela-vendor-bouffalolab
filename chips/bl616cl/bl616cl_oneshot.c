/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_oneshot.c
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

#include <nuttx/spinlock.h>
#include <nuttx/timers/oneshot.h>

#include "bl616cl_lhal.h"
#include "bflb_clock.h"
#include "bflb_irq.h"
#include "bflb_name.h"
#include "bflb_timer.h"
#include "bl616cl_oneshot.h"
#include "hardware/bl616cl_core.h"
#include "hardware/bl616cl_memorymap.h"
#include "hardware/timer_reg.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_ONESHOT_CLOCK_FREQUENCY 1000000
#define BL616CL_ONESHOT_CLOCK_DIV       39
#define BL616CL_ONESHOT_MIN_DELAY       2
#define BL616CL_ONESHOT_MAX_DELAY       UINT32_MAX

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bl616cl_oneshot_lowerhalf_s
{
  struct oneshot_lowerhalf_s lower;
  struct bflb_device_s *dev;
  bool running;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static clkcnt_t bl616cl_oneshot_current(
  struct oneshot_lowerhalf_s *lower);
static void bl616cl_oneshot_start(struct oneshot_lowerhalf_s *lower,
                                  clkcnt_t delay);
static void bl616cl_oneshot_start_absolute(
  struct oneshot_lowerhalf_s *lower, clkcnt_t expected);
static void bl616cl_oneshot_cancel(struct oneshot_lowerhalf_s *lower);
static clkcnt_t bl616cl_oneshot_max_delay(
  struct oneshot_lowerhalf_s *lower);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct oneshot_operations_s g_bl616cl_oneshot_ops =
{
  .current        = bl616cl_oneshot_current,
  .start          = bl616cl_oneshot_start,
  .start_absolute = bl616cl_oneshot_start_absolute,
  .cancel         = bl616cl_oneshot_cancel,
  .max_delay      = bl616cl_oneshot_max_delay,
};

static struct bl616cl_oneshot_lowerhalf_s g_bl616cl_oneshot =
{
  .lower.ops = &g_bl616cl_oneshot_ops,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_oneshot_clamp_delay
 *
 * Description:
 *   Limit a delay to the range BL616CL_ONESHOT_MIN_DELAY to
 *   BL616CL_ONESHOT_MAX_DELAY.
 *
 * Input Parameters:
 *   delay - Requested delay in oneshot clock counts.
 *
 * Returned Value:
 *   The delay limited to the supported range.
 *
 ****************************************************************************/

static uint32_t bl616cl_oneshot_clamp_delay(clkcnt_t delay)
{
  if (delay < BL616CL_ONESHOT_MIN_DELAY)
    {
      return BL616CL_ONESHOT_MIN_DELAY;
    }

  if (delay > BL616CL_ONESHOT_MAX_DELAY)
    {
      return BL616CL_ONESHOT_MAX_DELAY;
    }

  return (uint32_t)delay;
}

/****************************************************************************
 * Name: bl616cl_oneshot_mtime
 *
 * Description:
 *   Read the 64-bit CORET mtime counter. The high word is read again until it
 *   is stable to avoid a torn read.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The 64-bit mtime value.
 *
 ****************************************************************************/

static uint64_t bl616cl_oneshot_mtime(void)
{
  volatile uint32_t *mtime =
    (volatile uint32_t *)(uintptr_t)BL616CL_CORET_MTIME;
  uint32_t high;
  uint32_t low;

  do
    {
      high = mtime[1];
      low = mtime[0];
    }
  while (mtime[1] != high);

  return ((uint64_t)high << 32) | low;
}

/****************************************************************************
 * Name: bl616cl_oneshot_clear_counter
 *
 * Description:
 *   Clear the timer counter by setting and then clearing the
 *   TIMER_TCR1_CNT_CLR bit in the timer TCER register.
 *
 * Input Parameters:
 *   priv - Oneshot lower half.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_oneshot_clear_counter(
  struct bl616cl_oneshot_lowerhalf_s *priv)
{
  uintptr_t regaddr = priv->dev->reg_base + TIMER_TCER_OFFSET;
  uint32_t regval = getreg32(regaddr);

  putreg32(regval | TIMER_TCR1_CNT_CLR, regaddr);
  putreg32(regval & ~TIMER_TCR1_CNT_CLR, regaddr);
}

/****************************************************************************
 * Name: bl616cl_oneshot_configure
 *
 * Description:
 *   Initialize the hardware timer with bflb_timer_init(): preload counter
 *   mode, XTAL clock with BL616CL_ONESHOT_CLOCK_DIV, comparator 0 set to the
 *   delay and the other comparators at their maximum.
 *
 * Input Parameters:
 *   priv - Oneshot lower half.
 *   delay - Comparator 0 value in timer counts.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_oneshot_configure(
  struct bl616cl_oneshot_lowerhalf_s *priv, uint32_t delay)
{
  struct bflb_timer_config_s config;

  config.counter_mode = TIMER_COUNTER_MODE_PROLOAD;
  config.clock_source = TIMER_CLKSRC_XTAL;
  config.clock_div = BL616CL_ONESHOT_CLOCK_DIV;
  config.trigger_comp_id = TIMER_COMP_ID_0;
  config.comp0_val = delay;
  config.comp1_val = UINT32_MAX;
  config.comp2_val = UINT32_MAX;
  config.preload_val = 0;
  bflb_timer_init(priv->dev, &config);
}

/****************************************************************************
 * Name: bl616cl_oneshot_stop_locked
 *
 * Description:
 *   Stop the timer: mask and disable the interrupt, stop the hardware and
 *   clear any pending timer and interrupt state. The caller must be in a
 *   critical section, or in the timer interrupt handler.
 *
 * Input Parameters:
 *   priv - Oneshot lower half.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_oneshot_stop_locked(
  struct bl616cl_oneshot_lowerhalf_s *priv)
{
  bflb_timer_compint_mask(priv->dev, TIMER_COMP_ID_0, true);
  bflb_irq_disable(priv->dev->irq_num);
  bflb_timer_stop(priv->dev);
  bflb_timer_compint_clear(priv->dev, TIMER_COMP_ID_0);
  bflb_irq_clear_pending(priv->dev->irq_num);
  priv->running = false;
}

/****************************************************************************
 * Name: bl616cl_oneshot_start_locked
 *
 * Description:
 *   Start a one-shot countdown. The delay is clamped, a running countdown is
 *   stopped first, and the counter is cleared before the timer is configured
 *   and started with its interrupt enabled. The caller must be in a critical
 *   section.
 *
 * Input Parameters:
 *   priv - Oneshot lower half.
 *   delay - Delay in oneshot clock counts.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_oneshot_start_locked(
  struct bl616cl_oneshot_lowerhalf_s *priv, clkcnt_t delay)
{
  uint32_t compare = bl616cl_oneshot_clamp_delay(delay);

  if (priv->running)
    {
      bl616cl_oneshot_stop_locked(priv);
    }

  bl616cl_oneshot_clear_counter(priv);
  bl616cl_oneshot_configure(priv, compare);
  bflb_timer_compint_clear(priv->dev, TIMER_COMP_ID_0);
  bflb_irq_clear_pending(priv->dev->irq_num);
  bflb_timer_compint_mask(priv->dev, TIMER_COMP_ID_0, false);
  bflb_irq_enable(priv->dev->irq_num);
  bflb_timer_start(priv->dev);
  priv->running = true;
}

/****************************************************************************
 * Name: bl616cl_oneshot_handler
 *
 * Description:
 *   Timer interrupt handler. If the comparator 0 interrupt is pending, stop
 *   the timer and call oneshot_process_callback() when a callback is
 *   registered.
 *
 * Input Parameters:
 *   irq - IRQ number (unused).
 *   arg - The bl616cl_oneshot_lowerhalf_s instance.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_oneshot_handler(int irq, void *arg)
{
  struct bl616cl_oneshot_lowerhalf_s *priv = arg;

  UNUSED(irq);

  if (!bflb_timer_get_compint_status(priv->dev, TIMER_COMP_ID_0))
    {
      return;
    }

  bl616cl_oneshot_stop_locked(priv);

  if (priv->lower.callback != NULL)
    {
      oneshot_process_callback(&priv->lower);
    }
}

/****************************************************************************
 * Name: bl616cl_oneshot_current
 *
 * Description:
 *   Implement the oneshot_operations_s current method. The value comes from
 *   the free-running CORET mtime counter.
 *
 * Input Parameters:
 *   lower - Oneshot lower half (unused).
 *
 * Returned Value:
 *   The current counter value.
 *
 ****************************************************************************/

static clkcnt_t bl616cl_oneshot_current(
  struct oneshot_lowerhalf_s *lower)
{
  UNUSED(lower);
  return bl616cl_oneshot_mtime();
}

/****************************************************************************
 * Name: bl616cl_oneshot_start
 *
 * Description:
 *   Implement the oneshot_operations_s start method. Start the timer to
 *   expire after the given delay.
 *
 * Input Parameters:
 *   lower - Oneshot lower half.
 *   delay - Delay in oneshot clock counts; clamped to the supported range.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_oneshot_start(struct oneshot_lowerhalf_s *lower,
                                  clkcnt_t delay)
{
  struct bl616cl_oneshot_lowerhalf_s *priv =
    (struct bl616cl_oneshot_lowerhalf_s *)lower;
  irqstate_t flags;

  flags = enter_critical_section();
  bl616cl_oneshot_start_locked(priv, delay);
  leave_critical_section(flags);
}

/****************************************************************************
 * Name: bl616cl_oneshot_start_absolute
 *
 * Description:
 *   Implement the oneshot_operations_s start_absolute method. The delay is
 *   the expected time minus the current mtime value; if that time has already
 *   passed the minimum delay is used.
 *
 * Input Parameters:
 *   lower - Oneshot lower half.
 *   expected - Expiration time in counter units of bl616cl_oneshot_current().
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_oneshot_start_absolute(
  struct oneshot_lowerhalf_s *lower, clkcnt_t expected)
{
  struct bl616cl_oneshot_lowerhalf_s *priv =
    (struct bl616cl_oneshot_lowerhalf_s *)lower;
  clkcnt_t current;
  clkcnt_t delay;
  irqstate_t flags;

  flags = enter_critical_section();
  current = bl616cl_oneshot_mtime();
  delay = expected > current ? expected - current :
                               BL616CL_ONESHOT_MIN_DELAY;
  bl616cl_oneshot_start_locked(priv, delay);
  leave_critical_section(flags);
}

/****************************************************************************
 * Name: bl616cl_oneshot_cancel
 *
 * Description:
 *   Implement the oneshot_operations_s cancel method. Stop the timer without
 *   calling the callback.
 *
 * Input Parameters:
 *   lower - Oneshot lower half.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_oneshot_cancel(struct oneshot_lowerhalf_s *lower)
{
  struct bl616cl_oneshot_lowerhalf_s *priv =
    (struct bl616cl_oneshot_lowerhalf_s *)lower;
  irqstate_t flags;

  flags = enter_critical_section();
  bl616cl_oneshot_stop_locked(priv);
  leave_critical_section(flags);
}

/****************************************************************************
 * Name: bl616cl_oneshot_max_delay
 *
 * Description:
 *   Implement the oneshot_operations_s max_delay method.
 *
 * Input Parameters:
 *   lower - Oneshot lower half (unused).
 *
 * Returned Value:
 *   BL616CL_ONESHOT_MAX_DELAY.
 *
 ****************************************************************************/

static clkcnt_t bl616cl_oneshot_max_delay(
  struct oneshot_lowerhalf_s *lower)
{
  UNUSED(lower);
  return BL616CL_ONESHOT_MAX_DELAY;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_oneshot_initialize
 *
 * Description:
 *   Initialize the oneshot lower half on TIMER1 and register it as a oneshot
 *   device. The timer is stopped with its interrupt attached, and the driver
 *   is registered with oneshot_register() at BL616CL_ONESHOT_CLOCK_FREQUENCY.
 *
 * Input Parameters:
 *   devpath - The device path to register.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure:
 *
 *     -ENODEV - The TIMER1 lhal device was not found.
 *     Other errors are returned from bflb_irq_attach() and
 *     oneshot_register().
 *
 ****************************************************************************/

int bl616cl_oneshot_initialize(const char *devpath)
{
  struct bl616cl_oneshot_lowerhalf_s *priv = &g_bl616cl_oneshot;
  int ret;

  DEBUGASSERT(devpath != NULL);

  PERIPHERAL_CLOCK_TIMER0_1_WDG_ENABLE();
  priv->dev = bflb_device_get_by_name(BFLB_NAME_TIMER1);
  if (priv->dev == NULL)
    {
      return -ENODEV;
    }

  bflb_timer_stop(priv->dev);
  bl616cl_oneshot_configure(priv, BL616CL_ONESHOT_MIN_DELAY);
  bl616cl_oneshot_stop_locked(priv);

  ret = bflb_irq_attach(priv->dev->irq_num,
                        bl616cl_oneshot_handler, priv);
  if (ret < 0)
    {
      return ret;
    }

  oneshot_count_init(&priv->lower, BL616CL_ONESHOT_CLOCK_FREQUENCY);
  ret = oneshot_register(devpath, &priv->lower);
  if (ret < 0)
    {
      bflb_irq_detach(priv->dev->irq_num);
    }

  return ret;
}
