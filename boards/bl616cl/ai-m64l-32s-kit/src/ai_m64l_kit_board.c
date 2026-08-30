/****************************************************************************
 * apps/vendor/bouffalolab/boards/bl616cl/ai-m64l-32s-kit/src/ai_m64l_kit_board.c
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
#include "ai_m64l_kit.h"

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
 ****************************************************************************/

int bl616cl_board_initialize(void)
{
#if defined(CONFIG_BL616CL_GPIO) || defined(CONFIG_AI_M64L_KIT_SPI0)
  int ret;
#endif

#ifdef CONFIG_BL616CL_GPIO
  ret = ai_m64l_kit_gpio_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize GPIO driver: %d\n", ret);
      return ret;
    }
#endif

#ifdef CONFIG_AI_M64L_KIT_SPI0
  ret = ai_m64l_kit_spi_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize SPI buses: %d\n", ret);
      return ret;
    }
#endif

  return OK;
}
