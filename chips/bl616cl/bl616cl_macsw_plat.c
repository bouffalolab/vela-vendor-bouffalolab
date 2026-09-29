/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_macsw_plat.c
 *
 * BL616CL platform hooks for the macsw core: low-power interface stubs for
 * the STA-only, no-low-power build, and the monotonic time source.  The
 * macsw core calls the low-power hooks from the wifi_main startup and the
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

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int8_t hal_macsw_lp_rssi_restore(void)
{
  return 0;
}

int hal_macsw_lp_get_resume_wifi(void)
{
  return 0;
}

void hal_macsw_lp_set_resume_wifi(void)
{
}

void hal_macsw_lp_clear_resume_wifi(void)
{
}

int hal_macsw_lp_is_wake_by_traffic(void)
{
  return 0;
}

int hal_macsw_lp_is_wake_by_ap_existence_check(void)
{
  return 0;
}

int hal_macsw_lp_is_wake_by_ap_disconnected(void)
{
  return 0;
}

/**
 ****************************************************************************************
 * @brief Monotonic time source for the MAC stack (macsw_plat.h).
 *
 * @param[out] time_us Monotonic time in microseconds.
 * @return 0 on success.
 ****************************************************************************************
 */
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
