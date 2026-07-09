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

#include <sys/types.h>

#include "bl616cl_bod.h"
#include "bl616cl_bus.h"
#include "bl616cl_sec_mutex.h"

#include "bl616cldg.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cldg_flash_late_initialize
 ****************************************************************************/

static int bl616cldg_flash_late_initialize(void)
{
  /* SDK board_init() calls bflb_flash_init() and retunes XIP flash here.
   * Keep that disabled until the flash clock path runs from RAM-safe code.
   */

  return OK;
}

/****************************************************************************
 * Name: bl616cldg_clock_late_initialize
 ****************************************************************************/

static int bl616cldg_clock_late_initialize(void)
{
  /* SDK system_clock_init() switches MCU clock and sets the MTimer divider.
   * The chip layer owns the current early clock and timer lower-half setup.
   */

  return OK;
}

/****************************************************************************
 * Name: bl616cldg_peripheral_late_initialize
 ****************************************************************************/

static int bl616cldg_peripheral_late_initialize(void)
{
  /* Do not copy SDK peripheral_clock_init() wholesale. Each NuttX driver
   * should enable its own clock when it is registered.
   */

  return OK;
}

/****************************************************************************
 * Name: bl616cldg_irq_late_initialize
 ****************************************************************************/

static int bl616cldg_irq_late_initialize(void)
{
  /* up_irqinitialize() owns the CLIC table. Board IRQs such as WiFi and
   * bus-error handlers will be attached by their own drivers or debug hooks.
   */

  return OK;
}

/****************************************************************************
 * Name: bl616cldg_power_late_initialize
 ****************************************************************************/

static int bl616cldg_power_late_initialize(void)
{
  return bl616cl_bod_initialize();
}

/****************************************************************************
 * Name: bl616cldg_heap_late_initialize
 ****************************************************************************/

static int bl616cldg_heap_late_initialize(void)
{
  /* up_allocate_heap() provides the base heap. PSRAM and WiFi shared memory
   * must stay out of the generic heap until their ownership is explicit.
   */

  return OK;
}

/****************************************************************************
 * Name: bl616cldg_security_late_initialize
 ****************************************************************************/

static int bl616cldg_security_late_initialize(void)
{
  /* SDK board_init() initializes the security-engine mutexes after board
   * services are up. Keep the hook independent from flash or RF bring-up.
   */

  bl616cl_sec_mutex_init();
  return OK;
}

/****************************************************************************
 * Name: bl616cldg_diagnostics_late_initialize
 ****************************************************************************/

static int bl616cldg_diagnostics_late_initialize(void)
{
  /* SDK banner, version, anti-rollback and flash-info output are
   * diagnostics, not kernel bring-up requirements. Keep them out of
   * default late init.
   */

  return OK;
}

/****************************************************************************
 * Name: bl616cldg_debug_late_initialize
 ****************************************************************************/

static int bl616cldg_debug_late_initialize(void)
{
  return bl616cl_bus_error_initialize();
}

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

  ret = bl616cldg_flash_late_initialize();
  if (ret < 0)
    {
      return ret;
    }

  ret = bl616cldg_clock_late_initialize();
  if (ret < 0)
    {
      return ret;
    }

  ret = bl616cldg_peripheral_late_initialize();
  if (ret < 0)
    {
      return ret;
    }

  ret = bl616cldg_irq_late_initialize();
  if (ret < 0)
    {
      return ret;
    }

  ret = bl616cldg_power_late_initialize();
  if (ret < 0)
    {
      return ret;
    }

  ret = bl616cldg_heap_late_initialize();
  if (ret < 0)
    {
      return ret;
    }

  ret = bl616cldg_security_late_initialize();
  if (ret < 0)
    {
      return ret;
    }

  ret = bl616cldg_diagnostics_late_initialize();
  if (ret < 0)
    {
      return ret;
    }

  return bl616cldg_debug_late_initialize();
}
