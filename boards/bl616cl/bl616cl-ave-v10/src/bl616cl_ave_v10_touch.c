/****************************************************************************
 * vendor/bouffalolab/boards/bl616cl/bl616cl-ave-v10/src/bl616cl_ave_v10_touch.c
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

#include <nuttx/config.h>

#include <errno.h>

#include <nuttx/i2c/i2c_master.h>

#include "bl616cl_i2c.h"
#include "touch/touch_poll.h"
#include "bl616cl_ave_v10.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if defined(CONFIG_BL616CL_AVE_V10_TOUCH_GT911)
#  define AVE_TOUCH_CHIP (&g_touch_gt911)
#elif defined(CONFIG_BL616CL_AVE_V10_TOUCH_FT6X36)
#  define AVE_TOUCH_CHIP (&g_touch_ft6x36)
#else
#  error "Select the touch controller of U10"
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bl616cl_ave_v10_touch_initialize(void)
{
  struct i2c_master_s *i2c;
  int ret;

  i2c = bl616cl_i2cbus_initialize(0, BL616CL_AVE_V10_I2C0_SCL_PIN,
                                  BL616CL_AVE_V10_I2C0_SDA_PIN);
  if (i2c == NULL)
    {
      return -ENODEV;
    }

  ret = touch_poll_register(i2c, AVE_TOUCH_CHIP, "/dev/input0",
                            CONFIG_BL616CL_AVE_V10_TOUCH_PERIOD);
  if (ret < 0)
    {
      bl616cl_i2cbus_uninitialize(i2c);
    }

  return ret;
}
