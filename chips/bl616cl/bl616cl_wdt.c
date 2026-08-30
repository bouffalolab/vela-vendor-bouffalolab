/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_wdt.c
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

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/timers/watchdog.h>

#include "bflb_clock.h"
#include "bflb_irq.h"
#include "bflb_wdg.h"
#include "bl616cl_wdt.h"
#include "hardware/timer_reg.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* WDT clock: the nominal 32768 Hz source with divider 31 gives 1024 Hz.
 * The compare value register is 16 bit.
 */

#define BL616CL_WDT_CLKSRC   WDG_CLKSRC_32K
#define BL616CL_WDT_CLKDIV   31
#define BL616CL_WDT_HZ        1024
#define BL616CL_WDT_MAXTICKS  0xffff
#define BL616CL_WDT_MAXTIMEOUT ((BL616CL_WDT_MAXTICKS * 1000) / \
                                BL616CL_WDT_HZ)
#define BL616CL_WDT_RAW_IRQ  (BL616CL_IRQ_WDG - BL616CL_RISCV_IRQ_ASYNC)

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* This structure provides the private representation of the "lower-half"
 * driver state structure.  This structure must be cast-compatible with the
 * well-known watchdog_lowerhalf_s structure.
 */

struct bl616cl_wdt_lowerhalf_s
{
  const struct watchdog_ops_s *ops; /* Lower half operations */
  struct bflb_device_s *wdg;        /* LHAL WDT device */
  uint16_t timeout;                 /* Current timeout in milliseconds */
  uint32_t lastreset;               /* System ticks at last counter reset */
  bool started;                     /* True: timer has been started */
#ifdef CONFIG_BL616CL_WDT_CAPTURE
  xcpt_t handler;                   /* Capture callback */
  void *upper;                      /* Watchdog upper-half handle */
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bl616cl_wdt_start(struct watchdog_lowerhalf_s *lower);
static int bl616cl_wdt_stop(struct watchdog_lowerhalf_s *lower);
static int bl616cl_wdt_keepalive(struct watchdog_lowerhalf_s *lower);
static int bl616cl_wdt_getstatus(struct watchdog_lowerhalf_s *lower,
                                 struct watchdog_status_s *status);
static int bl616cl_wdt_settimeout(struct watchdog_lowerhalf_s *lower,
                                  uint32_t timeout);
#ifdef CONFIG_BL616CL_WDT_CAPTURE
static xcpt_t bl616cl_wdt_capture(struct watchdog_lowerhalf_s *lower,
                                  xcpt_t handler);
static int bl616cl_wdt_handler(int irq, void *context, void *arg);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct watchdog_ops_s g_bl616cl_wdtops =
{
  .start      = bl616cl_wdt_start,
  .stop       = bl616cl_wdt_stop,
  .keepalive  = bl616cl_wdt_keepalive,
  .getstatus  = bl616cl_wdt_getstatus,
  .settimeout = bl616cl_wdt_settimeout,
#ifdef CONFIG_BL616CL_WDT_CAPTURE
  .capture    = bl616cl_wdt_capture,
#else
  .capture    = NULL,
#endif
  .ioctl      = NULL,
};

static struct bl616cl_wdt_lowerhalf_s g_bl616cl_wdtdev =
{
  .ops       = &g_bl616cl_wdtops,
  .wdg       = NULL,
  .timeout   = BL616CL_WDT_MAXTIMEOUT,
  .lastreset = 0,
  .started   = false,
#ifdef CONFIG_BL616CL_WDT_CAPTURE
  .handler   = NULL,
  .upper     = NULL,
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_wdt_start
 *
 * Description:
 *   Start the watchdog timer, resetting the counter to the current timeout.
 *
 * Input Parameters:
 *   lower - A pointer the publicly visible representation of the
 *           "lower-half" driver state structure.
 *
 * Returned Values:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static uint16_t bl616cl_wdt_ms_to_ticks(uint32_t timeout)
{
  uint32_t ticks = (timeout * BL616CL_WDT_HZ + 999) / 1000;

  return (uint16_t)ticks;
}

static uint32_t bl616cl_wdt_ticks_to_ms(uint32_t ticks)
{
  return (ticks * 1000 + BL616CL_WDT_HZ - 1) / BL616CL_WDT_HZ;
}

#ifdef CONFIG_BL616CL_WDT_CAPTURE
static void bl616cl_wdt_set_action(
  struct bl616cl_wdt_lowerhalf_s *priv, bool capture)
{
  uintptr_t base = priv->wdg->reg_base;
  uint32_t regval;

  putreg16(0xbaba, base + TIMER_WFAR_OFFSET);
  putreg16(0xeb10, base + TIMER_WSAR_OFFSET);
  regval = getreg32(base + TIMER_WMER_OFFSET);
  if (capture)
    {
      regval &= ~TIMER_WRIE;
    }
  else
    {
      regval |= TIMER_WRIE;
    }

  putreg32(regval, base + TIMER_WMER_OFFSET);
}
#endif

static void bl616cl_wdt_clear_irq(
  struct bl616cl_wdt_lowerhalf_s *priv)
{
  bflb_wdg_compint_clear(priv->wdg);
#ifdef CONFIG_BL616CL_WDT_CAPTURE
  bflb_irq_clear_pending(BL616CL_WDT_RAW_IRQ);
#endif
}

static void bl616cl_wdt_configure(struct bl616cl_wdt_lowerhalf_s *priv)
{
  struct bflb_wdg_config_s cfg;

  cfg.clock_source = BL616CL_WDT_CLKSRC;
  cfg.clock_div    = BL616CL_WDT_CLKDIV;
  cfg.comp_val     = bl616cl_wdt_ms_to_ticks(priv->timeout);
#ifdef CONFIG_BL616CL_WDT_CAPTURE
  cfg.mode         = priv->handler != NULL ? WDG_MODE_INTERRUPT :
                                               WDG_MODE_RESET;
#else
  cfg.mode         = WDG_MODE_RESET;
#endif
  bflb_wdg_init(priv->wdg, &cfg);
}

static int bl616cl_wdt_start(struct watchdog_lowerhalf_s *lower)
{
  struct bl616cl_wdt_lowerhalf_s *priv =
    (struct bl616cl_wdt_lowerhalf_s *)lower;
  irqstate_t flags;

  DEBUGASSERT(priv != NULL && priv->wdg != NULL);

  wdinfo("Entry: started\n");

  flags = enter_critical_section();

  if (priv->started)
    {
      leave_critical_section(flags);
      return -EBUSY;
    }

  bl616cl_wdt_configure(priv);
  bl616cl_wdt_clear_irq(priv);
  bflb_wdg_reset_countervalue(priv->wdg);
  priv->lastreset = clock_systime_ticks();
  bflb_wdg_start(priv->wdg);
#ifdef CONFIG_BL616CL_WDT_CAPTURE
  if (priv->handler != NULL)
    {
      up_enable_irq(BL616CL_IRQ_WDG);
    }
#endif

  priv->started = true;

  leave_critical_section(flags);

  return OK;
}

/****************************************************************************
 * Name: bl616cl_wdt_stop
 *
 * Description:
 *   Stop the watchdog timer.
 *
 * Input Parameters:
 *   lower - A pointer the publicly visible representation of the
 *           "lower-half" driver state structure.
 *
 * Returned Values:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int bl616cl_wdt_stop(struct watchdog_lowerhalf_s *lower)
{
  struct bl616cl_wdt_lowerhalf_s *priv =
    (struct bl616cl_wdt_lowerhalf_s *)lower;
  irqstate_t flags;

  DEBUGASSERT(priv != NULL && priv->wdg != NULL);

  flags = enter_critical_section();

#ifdef CONFIG_BL616CL_WDT_CAPTURE
  up_disable_irq(BL616CL_IRQ_WDG);
#endif
  bflb_wdg_stop(priv->wdg);
  bl616cl_wdt_clear_irq(priv);
  priv->started = false;

  leave_critical_section(flags);

  return OK;
}

/****************************************************************************
 * Name: bl616cl_wdt_keepalive
 *
 * Description:
 *   Reset the watchdog timer to the current timeout value to prevent any
 *   imminent watchdog timeouts.  This is sometimes referred as "pinging"
 *   the watchdog timer or "petting the dog".
 *
 * Input Parameters:
 *   lower - A pointer the publicly visible representation of the
 *           "lower-half" driver state structure.
 *
 * Returned Values:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int bl616cl_wdt_keepalive(struct watchdog_lowerhalf_s *lower)
{
  struct bl616cl_wdt_lowerhalf_s *priv =
    (struct bl616cl_wdt_lowerhalf_s *)lower;
  irqstate_t flags;

  DEBUGASSERT(priv != NULL && priv->wdg != NULL);

  flags = enter_critical_section();

  bflb_wdg_reset_countervalue(priv->wdg);
  priv->lastreset = clock_systime_ticks();

  leave_critical_section(flags);

  return OK;
}

/****************************************************************************
 * Name: bl616cl_wdt_getstatus
 *
 * Description:
 *   Get the current watchdog timer status.
 *
 * Input Parameters:
 *   lower  - A pointer the publicly visible representation of the
 *            "lower-half" driver state structure.
 *   status - The location to return the watchdog status information.
 *
 * Returned Values:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int bl616cl_wdt_getstatus(struct watchdog_lowerhalf_s *lower,
                                 struct watchdog_status_s *status)
{
  struct bl616cl_wdt_lowerhalf_s *priv =
    (struct bl616cl_wdt_lowerhalf_s *)lower;
  irqstate_t flags;
  uint32_t compare;
  uint32_t counter;
  uint32_t elapsed;

  DEBUGASSERT(priv != NULL && status != NULL);

  flags = enter_critical_section();

  status->flags = WDFLAGS_RESET;
  if (priv->started)
    {
      status->flags |= WDFLAGS_ACTIVE;
    }

  /* Return the actual timeout in milliseconds */

  status->timeout = priv->timeout;

  /* Return the approximate time until the watchdog timer expiration */

  if (priv->started)
    {
      compare = bl616cl_wdt_ms_to_ticks(priv->timeout);
      counter = bflb_wdg_get_countervalue(priv->wdg);
      if (counter < compare)
        {
          status->timeleft = bl616cl_wdt_ticks_to_ms(compare - counter);
          if (status->timeleft > priv->timeout)
            {
              status->timeleft = priv->timeout;
            }
        }
      else
        {
          /* WVR can briefly expose the previous value after WCR is
           * written. Use the software timestamp until the new count is
           * visible instead of reporting an immediate timeout.
           */

          elapsed = TICK2MSEC(clock_systime_ticks() - priv->lastreset);
          status->timeleft = elapsed < priv->timeout ?
                             priv->timeout - elapsed : 0;
        }
    }
  else
    {
      status->timeleft = priv->timeout;
    }

#ifdef CONFIG_BL616CL_WDT_CAPTURE
  if (priv->handler != NULL)
    {
      status->flags &= ~WDFLAGS_RESET;
      status->flags |= WDFLAGS_CAPTURE;
    }
#endif

  leave_critical_section(flags);
  return OK;
}

/****************************************************************************
 * Name: bl616cl_wdt_settimeout
 *
 * Description:
 *   Set a new timeout value (in milliseconds) for the watchdog timer.
 *
 * Input Parameters:
 *   lower   - A pointer the publicly visible representation of the
 *             "lower-half" driver state structure.
 *   timeout - The new timeout value in milliseconds.
 *
 * Returned Values:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int bl616cl_wdt_settimeout(struct watchdog_lowerhalf_s *lower,
                                  uint32_t timeout)
{
  struct bl616cl_wdt_lowerhalf_s *priv =
    (struct bl616cl_wdt_lowerhalf_s *)lower;
  irqstate_t flags;

  DEBUGASSERT(priv != NULL);

  /* Can this timeout be represented by the 16 bit compare value? */

  if (timeout < 1 || timeout > BL616CL_WDT_MAXTIMEOUT)
    {
      wderr("ERROR: Cannot represent timeout=%" PRIu32 "\n", timeout);
      return -ERANGE;
    }

  flags = enter_critical_section();

  if (priv->started)
    {
#ifdef CONFIG_BL616CL_WDT_CAPTURE
      up_disable_irq(BL616CL_IRQ_WDG);
#endif
      bflb_wdg_stop(priv->wdg);
      priv->timeout = timeout;
      bl616cl_wdt_configure(priv);
      bl616cl_wdt_clear_irq(priv);
      bflb_wdg_reset_countervalue(priv->wdg);
      priv->lastreset = clock_systime_ticks();
      bflb_wdg_start(priv->wdg);
#ifdef CONFIG_BL616CL_WDT_CAPTURE
      if (priv->handler != NULL)
        {
          up_enable_irq(BL616CL_IRQ_WDG);
        }

#endif
    }
  else
    {
      priv->timeout = timeout;
    }

  leave_critical_section(flags);

  return OK;
}

#ifdef CONFIG_BL616CL_WDT_CAPTURE
static int bl616cl_wdt_handler(int irq, void *context, void *arg)
{
  struct bl616cl_wdt_lowerhalf_s *priv = arg;
  xcpt_t handler;
  void *upper;

  bflb_wdg_compint_clear(priv->wdg);
  bflb_irq_clear_pending(BL616CL_WDT_RAW_IRQ);
  handler = priv->handler;
  upper = priv->upper;
  if (handler != NULL)
    {
      handler(irq, context, upper);
    }

  return OK;
}

static xcpt_t bl616cl_wdt_capture(struct watchdog_lowerhalf_s *lower,
                                  xcpt_t handler)
{
  struct bl616cl_wdt_lowerhalf_s *priv =
    (struct bl616cl_wdt_lowerhalf_s *)lower;
  irqstate_t flags;
  xcpt_t oldhandler;

  flags = enter_critical_section();
  oldhandler = priv->handler;
  priv->handler = handler;
  if (priv->started)
    {
      up_disable_irq(BL616CL_IRQ_WDG);
      if ((oldhandler == NULL) != (handler == NULL))
        {
          bl616cl_wdt_clear_irq(priv);
          bl616cl_wdt_set_action(priv, handler != NULL);
        }

      if (handler != NULL)
        {
          up_enable_irq(BL616CL_IRQ_WDG);
        }
    }

  leave_critical_section(flags);
  return oldhandler;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_wdt_initialize
 *
 * Description:
 *   Initialize the watchdog timer and register it as devpath. See the
 *   header file for the detailed contract.
 *
 * Input Parameters:
 *   devpath - The full path to the watchdog device.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int bl616cl_wdt_initialize(FAR const char *devpath)
{
  FAR struct bl616cl_wdt_lowerhalf_s *priv = &g_bl616cl_wdtdev;
  FAR void *handle;

  DEBUGASSERT(devpath != NULL);

  priv->wdg = bflb_device_get_by_name(BFLB_NAME_WDT);
  if (priv->wdg == NULL)
    {
      wderr("ERROR: Failed to get WDT device\n");
      return -ENODEV;
    }

  PERIPHERAL_CLOCK_TIMER0_1_WDG_ENABLE();
  bflb_wdg_stop(priv->wdg);
  bl616cl_wdt_clear_irq(priv);
#ifdef CONFIG_BL616CL_WDT_CAPTURE
  if (irq_attach(BL616CL_IRQ_WDG, bl616cl_wdt_handler, priv) < 0)
    {
      return -EIO;
    }

  up_disable_irq(BL616CL_IRQ_WDG);
#endif

  handle = watchdog_register(devpath,
                             (FAR struct watchdog_lowerhalf_s *)priv);
  if (handle == NULL)
    {
#ifdef CONFIG_BL616CL_WDT_CAPTURE
      irq_detach(BL616CL_IRQ_WDG);
#endif
      return -ENODEV;
    }

#ifdef CONFIG_BL616CL_WDT_CAPTURE
  priv->upper = handle;
#endif
  return OK;
}
