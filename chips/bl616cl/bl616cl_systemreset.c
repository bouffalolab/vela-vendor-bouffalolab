/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_systemreset.c
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
#include "bl616cl_sdk.h"
#include "bl616cl_glb.h"
#include "hardware/timer_reg.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Access keys are literals in the upstream WDT implementation; register
 * offsets and fields are imported from the upstream hardware headers.
 */

#define BL616CL_WDT_ACCESS_KEY1      0xbabau
#define BL616CL_WDT_ACCESS_KEY2      0xeb10u

#define BL616CL_RESET_MAGIC          0xb616c100u
#define BL616CL_RESET_MAGIC_MASK     0xffffff00u
#define BL616CL_RESET_REASON_MASK    0xffu

#define BL616CL_HBN_REG(offset) \
  (*(volatile uint32_t *)(HBN_BASE + (offset)))

#define BL616CL_TIMER_REG(offset) \
  (*(volatile uint32_t *)(TIMER_BASE + (offset)))

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
void bl616cl_reset_reason_initialize(void)
{
  uint32_t saved = BL616CL_HBN_REG(HBN_RSV0_OFFSET);
  uint32_t reason = saved & BL616CL_RESET_REASON_MASK;
  uint32_t timer_status;

  if ((saved & BL616CL_RESET_MAGIC_MASK) == BL616CL_RESET_MAGIC &&
      reason <= BL616CL_RESET_SOFTWARE)
    {
      g_bl616cl_reset_reason = (enum bl616cl_reset_reason_e)reason;
      BL616CL_HBN_REG(HBN_RSV0_OFFSET) = 0;
    }
  else
    {
      timer_status = BL616CL_TIMER_REG(TIMER_WSR_OFFSET);
      if ((BL616CL_HBN_REG(HBN_WSR_OFFSET) & HBN_WTS_MSK) != 0 ||
          (timer_status & TIMER_WTS) != 0)
        {
          g_bl616cl_reset_reason = BL616CL_RESET_WATCHDOG;
        }

      /* WDT status is sticky until cleared with the access key. */

      BL616CL_TIMER_REG(TIMER_WFAR_OFFSET) =
        BL616CL_WDT_ACCESS_KEY1;
      BL616CL_TIMER_REG(TIMER_WSAR_OFFSET) =
        BL616CL_WDT_ACCESS_KEY2;
      BL616CL_TIMER_REG(TIMER_WSR_OFFSET) =
        timer_status & ~TIMER_WTS;
    }
}

void bl616cl_reset_reason_set(enum bl616cl_reset_reason_e reason)
{
  BL616CL_HBN_REG(HBN_RSV0_OFFSET) =
    BL616CL_RESET_MAGIC | ((uint32_t)reason & BL616CL_RESET_REASON_MASK);
}

enum bl616cl_reset_reason_e bl616cl_reset_reason_get(void)
{
  return g_bl616cl_reset_reason;
}
#endif

void up_systemreset(void)
{
  (void)HBN_32K_Sel(HBN_32K_RC);
  (void)HBN_Power_Off_Xtal_32K();

  __asm__ volatile("csrc mstatus, 8");
  __asm__ volatile("li a0, 0x800");
  __asm__ volatile("csrc mie, a0");

  (void)GLB_AHB_MCU_Software_Reset(GLB_AHB_MCU_SW_WIFI);
  (void)GLB_AHB_MCU_Software_Reset(GLB_AHB_MCU_SW_BTDM);
  (void)GLB_AHB_MCU_Software_Reset(GLB_AHB_MCU_SW_BLE2);

  up_mdelay(10);
  (void)GLB_SW_POR_Reset();

  for (; ; )
    {
    }
}
