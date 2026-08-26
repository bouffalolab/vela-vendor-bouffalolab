/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_oneshot.c
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

#include "bflb_clock.h"
#include "bflb_irq.h"
#include "bflb_name.h"
#include "bflb_timer.h"
#include "bl616cl_oneshot.h"
#include "chip.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_ONESHOT_CLOCK_FREQUENCY 1000000
#define BL616CL_ONESHOT_CLOCK_DIV       39
#define BL616CL_ONESHOT_MIN_DELAY       2
#define BL616CL_ONESHOT_MAX_DELAY       UINT32_MAX
#define BL616CL_TIMER_TCER_OFFSET        0x84
#define BL616CL_TIMER1_COUNTER_CLEAR     (1 << 6)

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

static void bl616cl_oneshot_clear_counter(
  struct bl616cl_oneshot_lowerhalf_s *priv)
{
  uintptr_t regaddr = priv->dev->reg_base + BL616CL_TIMER_TCER_OFFSET;
  uint32_t regval = getreg32(regaddr);

  putreg32(regval | BL616CL_TIMER1_COUNTER_CLEAR, regaddr);
  putreg32(regval & ~BL616CL_TIMER1_COUNTER_CLEAR, regaddr);
}

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

static clkcnt_t bl616cl_oneshot_current(
  struct oneshot_lowerhalf_s *lower)
{
  UNUSED(lower);
  return bl616cl_oneshot_mtime();
}

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

static void bl616cl_oneshot_cancel(struct oneshot_lowerhalf_s *lower)
{
  struct bl616cl_oneshot_lowerhalf_s *priv =
    (struct bl616cl_oneshot_lowerhalf_s *)lower;
  irqstate_t flags;

  flags = enter_critical_section();
  bl616cl_oneshot_stop_locked(priv);
  leave_critical_section(flags);
}

static clkcnt_t bl616cl_oneshot_max_delay(
  struct oneshot_lowerhalf_s *lower)
{
  UNUSED(lower);
  return BL616CL_ONESHOT_MAX_DELAY;
}

/****************************************************************************
 * Public Functions
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
      (void)bflb_irq_detach(priv->dev->irq_num);
    }

  return ret;
}
