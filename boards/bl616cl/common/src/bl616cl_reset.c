/****************************************************************************
 * vendor/bouffalolab/boards/bl616cl/common/src/bl616cl_reset.c
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

#include <debug.h>
#include <stdlib.h>

#include <nuttx/arch.h>
#include <nuttx/board.h>

#include "bl616cl_systemreset.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_reset
 *
 * Description:
 *   Reset the board, for BOARDIOC_RESET and for the reset on assertion.
 *   With CONFIG_BOARDCTL_RESET_CAUSE, first record the reason for the next
 *   boot: FATAL when status is CONFIG_BOARD_ASSERT_RESET_VALUE and
 *   CONFIG_BOARD_RESET_ON_ASSERT is enabled, SOFTWARE otherwise.
 *
 * Input Parameters:
 *   status - Board-specific reset status. CONFIG_BOARD_ASSERT_RESET_VALUE
 *            marks a reset after an assertion.
 *
 * Returned Value:
 *   Does not return; up_systemreset() resets the chip.
 *
 ****************************************************************************/

#ifdef CONFIG_BOARDCTL_RESET
int board_reset(int status)
{
#ifdef CONFIG_BOARDCTL_RESET_CAUSE
  enum bl616cl_reset_reason_e reason = BL616CL_RESET_SOFTWARE;

#if CONFIG_BOARD_RESET_ON_ASSERT >= 1
  if (status == CONFIG_BOARD_ASSERT_RESET_VALUE)
    {
      reason = BL616CL_RESET_FATAL;
    }
#endif

  bl616cl_reset_reason_set(reason);
#endif

  up_systemreset();
  return 0;
}
#endif

/****************************************************************************
 * Name: board_reset_cause
 *
 * Description:
 *   Report the cause of the last reset for BOARDIOC_RESET_CAUSE, as
 *   determined at boot by bl616cl_reset_reason_initialize(): a watchdog
 *   reset, a reset after an assertion, a user reboot, or otherwise a
 *   power-on reset.
 *
 * Input Parameters:
 *   cause - Location to return the reset cause and flag.
 *
 * Returned Value:
 *   Zero (OK) always.
 *
 ****************************************************************************/

#ifdef CONFIG_BOARDCTL_RESET_CAUSE
int board_reset_cause(struct boardioc_reset_cause_s *cause)
{
  DEBUGASSERT(cause != NULL);

  cause->flag = 0;

  switch (bl616cl_reset_reason_get())
    {
      case BL616CL_RESET_WATCHDOG:
        cause->cause = BOARDIOC_RESETCAUSE_SYS_RWDT;
        break;

      case BL616CL_RESET_FATAL:
        cause->cause = BOARDIOC_RESETCAUSE_CPU_SOFT;
        cause->flag = BOARDIOC_SOFTRESETCAUSE_ASSERT;
        break;

      case BL616CL_RESET_SOFTWARE:
        cause->cause = BOARDIOC_RESETCAUSE_CPU_SOFT;
        cause->flag = BOARDIOC_SOFTRESETCAUSE_USER_REBOOT;
        break;

      case BL616CL_RESET_POWER_ON:
      default:
        cause->cause = BOARDIOC_RESETCAUSE_SYS_CHIPPOR;
        break;
    }

  return OK;
}
#endif
