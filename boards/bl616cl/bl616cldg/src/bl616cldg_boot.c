/****************************************************************************
 * apps/vendor/bouffalolab/boards/bl616cl/bl616cldg/src/bl616cldg_boot.c
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

#include "bl616cldg.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_early_initialize
 *
 * Description:
 *   This hook runs after up_initialize() on the startup thread. Keep it for
 *   board-specific early hardware that is safe before the bring-up thread.
 *
 ****************************************************************************/

#ifdef CONFIG_BOARD_EARLY_INITIALIZE
void board_early_initialize(void)
{
}
#endif

/****************************************************************************
 * Name: board_late_initialize
 *
 * Description:
 *   This hook runs in the AppBringUp thread and starts BL616CLDG board
 *   initialization that may depend on scheduler context.
 *
 ****************************************************************************/

#ifdef CONFIG_BOARD_LATE_INITIALIZE
void board_late_initialize(void)
{
  int ret;

  ret = bl616cldg_bringup();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: BL616CLDG bringup failed: %d\n", ret);
    }
}
#endif
