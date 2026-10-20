/****************************************************************************
 * vendor/bouffalolab/boards/bl616cl/bl616cl-ave-v10/src/bl616cl_ave_v10_board.c
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

#include "bl616cl_board_common.h"
#include "bl616cl_ave_v10.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_board_initialize
 *
 * Description:
 *   Initialize peripherals whose registration depends on this board's
 *   wiring.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   None. A peripheral that fails is logged and the others are still
 *   initialized.
 *
 ****************************************************************************/

void bl616cl_board_initialize(void)
{
#if defined(CONFIG_BL616CL_AVE_V10_I2C0) || \
    defined(CONFIG_BL616CL_AVE_V10_AUDIO)
  int ret;
#endif

#ifdef CONFIG_BL616CL_AVE_V10_I2C0
  ret = bl616cl_ave_v10_i2c_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize I2C0: %d\n", ret);
    }
#endif

#ifdef CONFIG_BL616CL_AVE_V10_AUDIO
  ret = bl616cl_ave_v10_audio_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize audio: %d\n", ret);
    }
#endif
}
