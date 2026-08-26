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
#include <nuttx/spinlock.h>
#include <nuttx/timers/watchdog.h>

#include "bflb_wdg.h"
#include "bl616cl_wdt.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* WDT clock: 32 kHz source with divider 31 gives ~1024 Hz, i.e. one
 * counter tick per millisecond. The compare value register is 16 bit.
 */

#define BL616CL_WDT_CLKSRC   WDG_CLKSRC_32K
#define BL616CL_WDT_CLKDIV   31
#define BL616CL_WDT_MAXTIMEOUT 0xffff

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
  uint32_t lastreset;               /* Ticks since last keepalive */
  uint16_t timeout;                 /* Current timeout in milliseconds */
  bool started;                     /* True: timer has been started */
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
  .capture    = NULL,
  .ioctl      = NULL,
};

static struct bl616cl_wdt_lowerhalf_s g_bl616cl_wdtdev =
{
  .ops       = &g_bl616cl_wdtops,
  .wdg       = NULL,
  .lastreset = 0,
  .timeout   = BL616CL_WDT_MAXTIMEOUT,
  .started   = false,
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

static int bl616cl_wdt_start(struct watchdog_lowerhalf_s *lower)
{
  struct bl616cl_wdt_lowerhalf_s *priv =
    (struct bl616cl_wdt_lowerhalf_s *)lower;
  struct bflb_wdg_config_s cfg;
  irqstate_t flags;

  DEBUGASSERT(priv != NULL && priv->wdg != NULL);

  wdinfo("Entry: started\n");

  if (priv->started)
    {
      /* Return EBUSY to indicate that the timer was already running */

      return -EBUSY;
    }

  flags = enter_critical_section();

  cfg.clock_source = BL616CL_WDT_CLKSRC;
  cfg.clock_div    = BL616CL_WDT_CLKDIV;
  cfg.comp_val     = priv->timeout;
  cfg.mode         = WDG_MODE_RESET;

  bflb_wdg_init(priv->wdg, &cfg);
  bflb_wdg_reset_countervalue(priv->wdg);
  bflb_wdg_start(priv->wdg);

  priv->lastreset = clock_systime_ticks();
  priv->started   = true;

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

  DEBUGASSERT(priv != NULL && priv->wdg != NULL);

  bflb_wdg_stop(priv->wdg);
  priv->started = false;

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

  priv->lastreset = clock_systime_ticks();
  bflb_wdg_reset_countervalue(priv->wdg);

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
  uint32_t elapsed;

  DEBUGASSERT(priv != NULL && status != NULL);

  status->flags = WDFLAGS_RESET;
  if (priv->started)
    {
      status->flags |= WDFLAGS_ACTIVE;
    }

  /* Return the actual timeout in milliseconds */

  status->timeout = priv->timeout;

  /* Return the approximate time until the watchdog timer expiration */

  elapsed = TICK2MSEC(clock_systime_ticks() - priv->lastreset);
  if (elapsed > (uint32_t)priv->timeout)
    {
      elapsed = priv->timeout;
    }

  status->timeleft = priv->timeout - elapsed;

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

  DEBUGASSERT(priv != NULL);

  /* Can this timeout be represented by the 16 bit compare value? */

  if (timeout < 1 || timeout > BL616CL_WDT_MAXTIMEOUT)
    {
      wderr("ERROR: Cannot represent timeout=%" PRIu32 "\n", timeout);
      return -ERANGE;
    }

  if (priv->started)
    {
      wdwarn("WARNING: Watchdog is already started\n");
      return -EBUSY;
    }

  priv->timeout = timeout;

  return OK;
}

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

  handle = watchdog_register(devpath,
                             (FAR struct watchdog_lowerhalf_s *)priv);
  return (handle != NULL) ? OK : -ENODEV;
}
