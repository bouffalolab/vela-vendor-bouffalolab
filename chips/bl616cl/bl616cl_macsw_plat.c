/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_macsw_plat.c
 *
 * BL616CL platform hooks for the macsw core: Wi-Fi task suspend/resume,
 * the time sources, and low-power interface stubs for the STA-only,
 * no-low-power build.  The macsw core calls the low-power hooks from the wifi_main startup and the
 * wake-up paths; without the low-power firmware path they report "no
 * low-power state" and keep the MAC in full-power mode.
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <nuttx/semaphore.h>
#include <nuttx/signal.h>

#include "coexm.h"
#include "macsw.h"
#include "macsw_plat.h"

#include "bl616cl_macsw_plat.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Wi-Fi task notification: wifi_task_resume() posts, wifi_task_suspend()
 * waits.
 */

static sem_t g_wifi_notify_sem = SEM_INITIALIZER(0);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: hal_macsw_lp_rssi_restore
 *
 * Description:
 *   macsw low-power hook (declared in hal_machw.h). Stub: there is no
 *   saved RSSI to restore in this STA-only, no-low-power build.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Always 0.
 *
 ****************************************************************************/

int8_t hal_macsw_lp_rssi_restore(void)
{
  return 0;
}

/****************************************************************************
 * Name: hal_macsw_lp_get_resume_wifi
 *
 * Description:
 *   macsw low-power hook. Stub: no Wi-Fi resume state is kept.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Always 0 (not resuming).
 *
 ****************************************************************************/

int hal_macsw_lp_get_resume_wifi(void)
{
  return 0;
}

/****************************************************************************
 * Name: hal_macsw_lp_set_resume_wifi
 *
 * Description:
 *   macsw low-power hook. Stub: does nothing, no resume state is kept.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void hal_macsw_lp_set_resume_wifi(void)
{
}

/****************************************************************************
 * Name: hal_macsw_lp_clear_resume_wifi
 *
 * Description:
 *   macsw low-power hook. Stub: does nothing, no resume state is kept.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void hal_macsw_lp_clear_resume_wifi(void)
{
}

/****************************************************************************
 * Name: hal_macsw_lp_is_wake_by_traffic
 *
 * Description:
 *   macsw low-power hook. Stub: reports that the system was never
 *   woken by traffic.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Always 0.
 *
 ****************************************************************************/

int hal_macsw_lp_is_wake_by_traffic(void)
{
  return 0;
}

/****************************************************************************
 * Name: hal_macsw_lp_is_wake_by_ap_existence_check
 *
 * Description:
 *   macsw low-power hook. Stub: reports that the system was never
 *   woken by an AP existence check.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Always 0.
 *
 ****************************************************************************/

int hal_macsw_lp_is_wake_by_ap_existence_check(void)
{
  return 0;
}

/****************************************************************************
 * Name: hal_macsw_lp_is_wake_by_ap_disconnected
 *
 * Description:
 *   macsw low-power hook. Stub: reports that the system was never
 *   woken by an AP disconnection.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Always 0.
 *
 ****************************************************************************/

int hal_macsw_lp_is_wake_by_ap_disconnected(void)
{
  return 0;
}

/****************************************************************************
 * Name: macsw_platform_get_time_us
 *
 * Description:
 *   macsw platform hook (macsw_plat.h). Monotonic time source for the
 *   MAC stack, read from CLOCK_MONOTONIC.
 *
 * Input Parameters:
 *   time_us - Receives the monotonic time in microseconds.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *   -EIO if clock_gettime() fails.
 *
 ****************************************************************************/

int macsw_platform_get_time_us(uint64_t *time_us)
{
  struct timespec ts;

  DEBUGASSERT(time_us != NULL);

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
      return -EIO;
    }

  *time_us = (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000;
  return 0;
}

/****************************************************************************
 * Name: wifi_task_suspend
 *
 * Description:
 *   macsw platform hook.  Suspend the Wi-Fi task until it is notified by
 *   wifi_task_resume().  Waits on g_wifi_notify_sem, bracketed by the
 *   coex_coord_on_wifi_suspend_enter()/coex_coord_on_wifi_wake() calls.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void wifi_task_suspend(void)
{
  bool slept_committed = coex_coord_on_wifi_suspend_enter();

  /* Wait for notification using semaphore */

  nxsem_wait(&g_wifi_notify_sem);

  coex_coord_on_wifi_wake(slept_committed);
}

/****************************************************************************
 * Name: wifi_task_resume
 *
 * Description:
 *   macsw platform hook.  Resume the Wi-Fi task by posting
 *   g_wifi_notify_sem.  NuttX semaphores are the same in ISR and task
 *   context, so isr is not used.  A post failure is only logged.
 *
 * Input Parameters:
 *   isr - Whether called from interrupt context (unused).
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void wifi_task_resume(bool isr)
{
  int ret;

  /* NuttX doesn't distinguish between ISR and task context for semaphores */

  ret = nxsem_post(&g_wifi_notify_sem);

  if (ret != 0)
    {
      wlerr("failed to resume WiFi task: %d\n", ret);
    }
}

/****************************************************************************
 * Name: wifi_sys_now_ms
 *
 * Description:
 *   macsw platform hook.  Get system time in milliseconds, from
 *   CLOCK_MONOTONIC.
 *
 * Input Parameters:
 *   isr - Whether called from interrupt context (unused).
 *
 * Returned Value:
 *   System time in milliseconds.
 *
 ****************************************************************************/

uint32_t wifi_sys_now_ms(bool isr)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/****************************************************************************
 * Name: bl616cl_macsw_wait_suspended
 *
 * Description:
 *   Wait until the Wi-Fi task first blocks in wifi_task_suspend(), that is,
 *   until g_wifi_notify_sem has a waiter.  Polling keeps
 *   wifi_task_suspend(), which is on the hot path, unchanged.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_macsw_wait_suspended(void)
{
  int semcount;

  while (nxsem_get_value(&g_wifi_notify_sem, &semcount) == OK &&
         semcount >= 0)
    {
      nxsig_usleep(1000);
    }
}
