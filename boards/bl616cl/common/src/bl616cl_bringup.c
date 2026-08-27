/****************************************************************************
 * apps/vendor/bouffalolab/boards/bl616cl/common/src/bl616cl_bringup.c
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

#include "bl616cl_bod.h"
#include "bl616cl_bus.h"
#include "bl616cl_sec_mutex.h"

#ifdef CONFIG_BOARDCTL_RESET_CAUSE
#  include "bl616cl_systemreset.h"
#endif

#ifdef CONFIG_BL616CL_WDT
#  include "bl616cl_wdt.h"
#endif

#ifdef CONFIG_BL616CL_ONESHOT
#  include "bl616cl_oneshot.h"
#endif

#ifdef CONFIG_BL616CL_TIMER
#  include "bl616cl_tim.h"
#endif

#include "bl616cl_board_common.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_bringup
 *
 * Description:
 *   Perform common BL616CL board initialization after nx_start() has entered
 *   the NuttX initialization path.
 *
 ****************************************************************************/

int bl616cl_bringup(void)
{
  int ret;

#ifdef CONFIG_BOARDCTL_RESET_CAUSE
  bl616cl_reset_reason_initialize();
#endif

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

  ret = bl616cl_board_initialize();
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_BL616CL_ONESHOT
  ret = bl616cl_oneshot_initialize("/dev/oneshot");
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize oneshot driver: %d\n",
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
