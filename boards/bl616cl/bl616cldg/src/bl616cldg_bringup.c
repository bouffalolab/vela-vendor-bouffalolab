/****************************************************************************
 * apps/vendor/bouffalolab/boards/bl616cl/bl616cldg/src/bl616cldg_bringup.c
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

#include <syslog.h>

#include <sys/types.h>

#include <nuttx/ioexpander/gpio.h>

#include "bl616cl_bod.h"
#include "bl616cl_bus.h"
#include "bl616cl_sec_mutex.h"

#ifdef CONFIG_BL616CL_WDT
#  include "bl616cl_wdt.h"
#endif

#ifdef CONFIG_BL616CL_GPIO
#  include "bl616cl_gpio.h"
#endif

#ifdef CONFIG_BL616CL_TIMER
#  include "bl616cl_tim.h"
#endif

#include "bl616cldg.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cldg_bringup
 *
 * Description:
 *   Perform board-specific late initialization after nx_start() has entered
 *   the NuttX initialization path.
 *
 ****************************************************************************/

int bl616cldg_bringup(void)
{
  int ret;

  ret = bl616cl_bod_initialize();
  if (ret < 0)
    {
      return ret;
    }

  bl616cl_sec_mutex_init();

  ret = bl616cl_bus_error_initialize();
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_BL616CL_WDT
  ret = bl616cl_wdt_initialize("/dev/watchdog0");
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize watchdog driver: %d\n",
             ret);
      return ret;
    }
#endif

#ifdef CONFIG_BL616CL_GPIO
  ret = bl616cldg_gpio_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize GPIO driver: %d\n",
             ret);
      return ret;
    }
#endif

#ifdef CONFIG_BL616CL_TIMER0
  ret = bl616cl_timer_initialize("/dev/timer0", 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize timer driver: %d\n",
             ret);
      return ret;
    }
#endif

  return OK;
}
