/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_systemreset.c
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

#include <nuttx/arch.h>

#include "bl616cl_systemreset.h"
#include "bl616cl_wdt.h"
#include "bl616cl_sdk.h"
#include "bl616cl_glb.h"
#include "hardware/timer_reg.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_RESET_MAGIC          0xb616c100u
#define BL616CL_RESET_MAGIC_MASK     0xffffff00u
#define BL616CL_RESET_REASON_MASK    0xffu

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_BOARDCTL_RESET_CAUSE
static enum bl616cl_reset_reason_e g_bl616cl_reset_reason =
  BL616CL_RESET_POWER_ON;
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_BOARDCTL_RESET_CAUSE
/****************************************************************************
 * Name: bl616cl_reset_reason_initialize
 *
 * Description:
 *   Determine the reset reason at boot. If the HBN status flag holds a valid
 *   reset-reason word (magic 0xb616c1xx, reason not above
 *   BL616CL_RESET_SOFTWARE) left by bl616cl_reset_reason_set(), use it and
 *   clear the flag. Otherwise report a watchdog reset if the HBN or timer
 *   watchdog status is set, and clear the sticky timer watchdog status. The
 *   default is BL616CL_RESET_POWER_ON. Only built with
 *   CONFIG_BOARDCTL_RESET_CAUSE.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_reset_reason_initialize(void)
{
  uint32_t saved = HBN_Get_Status_Flag();
  uint32_t reason = saved & BL616CL_RESET_REASON_MASK;
  uint32_t timer_status;

  if ((saved & BL616CL_RESET_MAGIC_MASK) == BL616CL_RESET_MAGIC &&
      reason <= BL616CL_RESET_SOFTWARE)
    {
      g_bl616cl_reset_reason = (enum bl616cl_reset_reason_e)reason;
      HBN_Set_Status_Flag(0);
    }
  else
    {
      timer_status = getreg32(TIMER_BASE + TIMER_WSR_OFFSET);
      if ((getreg32(HBN_BASE + HBN_WSR_OFFSET) & HBN_WTS_MSK) != 0 ||
          (timer_status & TIMER_WTS) != 0)
        {
          g_bl616cl_reset_reason = BL616CL_RESET_WATCHDOG;
        }

      /* WDT status is sticky until cleared with the access key. */

      putreg32(BL616CL_WDT_ACCESS_KEY1, TIMER_BASE + TIMER_WFAR_OFFSET);
      putreg32(BL616CL_WDT_ACCESS_KEY2, TIMER_BASE + TIMER_WSAR_OFFSET);
      putreg32(timer_status & ~TIMER_WTS, TIMER_BASE + TIMER_WSR_OFFSET);
    }
}

/****************************************************************************
 * Name: bl616cl_reset_reason_set
 *
 * Description:
 *   Record a reset reason in the HBN status flag, tagged with a magic value,
 *   so that it survives the next reset and is picked up by
 *   bl616cl_reset_reason_initialize(). Only built with
 *   CONFIG_BOARDCTL_RESET_CAUSE.
 *
 * Input Parameters:
 *   reason - Reset reason to record; only the low 8 bits are stored
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_reset_reason_set(enum bl616cl_reset_reason_e reason)
{
  HBN_Set_Status_Flag(BL616CL_RESET_MAGIC |
                      ((uint32_t)reason & BL616CL_RESET_REASON_MASK));
}

/****************************************************************************
 * Name: bl616cl_reset_reason_get
 *
 * Description:
 *   Return the reset reason determined at boot by
 *   bl616cl_reset_reason_initialize(). Only built with
 *   CONFIG_BOARDCTL_RESET_CAUSE.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The reset reason.
 *
 ****************************************************************************/

enum bl616cl_reset_reason_e bl616cl_reset_reason_get(void)
{
  return g_bl616cl_reset_reason;
}
#endif

/****************************************************************************
 * Name: up_systemreset
 *
 * Description:
 *   NuttX architecture interface: reset the chip. Switch the 32K clock to RC
 *   and power off the 32K crystal, disable global and machine external
 *   interrupts, software-reset the WiFi, BTDM and BLE2 blocks, wait 10 ms and
 *   trigger a software power-on reset.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Does not return.
 *
 ****************************************************************************/

void up_systemreset(void)
{
  HBN_32K_Sel(HBN_32K_RC);
  HBN_Power_Off_Xtal_32K();

  __asm__ volatile("csrc mstatus, 8");
  __asm__ volatile("li a0, 0x800");
  __asm__ volatile("csrc mie, a0");

  GLB_AHB_MCU_Software_Reset(GLB_AHB_MCU_SW_WIFI);
  GLB_AHB_MCU_Software_Reset(GLB_AHB_MCU_SW_BTDM);
  GLB_AHB_MCU_Software_Reset(GLB_AHB_MCU_SW_BLE2);

  up_mdelay(10);
  GLB_SW_POR_Reset();

  for (; ; )
    {
    }
}
