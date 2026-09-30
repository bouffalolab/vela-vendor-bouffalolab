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

#include <sys/param.h>

#include <nuttx/arch.h>

#include "bl616cl_systemreset.h"
#include "bl616cl_wdt.h"
#include "bl616cl_sdk.h"
#include "bl616cl_glb.h"
#include "hardware/timer_reg.h"

#ifdef CONFIG_BOARDCTL_RESET_CAUSE
#include "bl616cl_lp.h"
#include "bl616cl_sys.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_BOARDCTL_RESET_CAUSE
/* The reset reason is kept where upstream bl_sys_rstinfo_set() keeps it
 * (drivers/sys/bl616cl/bl616cl_sys.c): iot2lp_para->reset_keep in HBN RAM,
 * which survives software and watchdog resets and which the upstream
 * low-power code leaves alone.  HBN_RSV0 is not used because upstream PM
 * and boot2 own it.  The check value is RST_REASON_CHK_VAL there, which is
 * private to that file.
 */

#define BL616CL_RESET_KEEP \
  (&((iot2lp_para_t *)IOT2LP_PARA_ADDR)->reset_keep)
#define BL616CL_RESET_KEEP_CHK       0xbf1ba55au
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_BOARDCTL_RESET_CAUSE
static enum bl616cl_reset_reason_e g_bl616cl_reset_reason =
  BL616CL_RESET_POWER_ON;

/* Upstream BL_RST_REASON_E code of each reason, as stored in reset_keep */

static const uint8_t g_bl616cl_reset_keep_code[] =
{
  [BL616CL_RESET_POWER_ON] = BL_RST_POWER_OFF,
  [BL616CL_RESET_WATCHDOG] = BL_RST_HARDWARE_WATCHDOG,
  [BL616CL_RESET_FATAL]    = BL_RST_FATAL_EXCEPTION,
  [BL616CL_RESET_SOFTWARE] = BL_RST_SOFTWARE,
};
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_BOARDCTL_RESET_CAUSE
/****************************************************************************
 * Name: bl616cl_reset_reason_initialize
 *
 * Description:
 *   Determine the reset reason at boot. If reset_keep in HBN RAM holds a
 *   reason with a valid check word, left by bl616cl_reset_reason_set() or
 *   upstream bl_sys_rstinfo_set(), use it. Otherwise report a watchdog
 *   reset if the HBN or timer watchdog status is set, and clear the sticky
 *   timer watchdog status. reset_keep is invalidated either way. The
 *   default is BL616CL_RESET_POWER_ON; HBN RAM is random after power-on,
 *   which the check word rejects. Only built with
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
  uint32_t saved = BL616CL_RESET_KEEP->reset_reason;
  bool valid = (saved ^ BL616CL_RESET_KEEP_CHK) ==
               BL616CL_RESET_KEEP->reset_reason_chk;
  uint32_t timer_status;
  unsigned int reason;

  for (reason = 0; valid && reason < nitems(g_bl616cl_reset_keep_code);
       reason++)
    {
      if (g_bl616cl_reset_keep_code[reason] == saved)
        {
          break;
        }
    }

  BL616CL_RESET_KEEP->reset_reason = BL_RST_POWER_OFF;
  BL616CL_RESET_KEEP->reset_reason_chk = BL_RST_POWER_OFF;

  if (valid && reason < nitems(g_bl616cl_reset_keep_code))
    {
      g_bl616cl_reset_reason = (enum bl616cl_reset_reason_e)reason;
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
 *   Record a reset reason in reset_keep in HBN RAM, as the upstream
 *   BL_RST_REASON_E code with its check word, so that it survives the next
 *   reset and is picked up by bl616cl_reset_reason_initialize(). Only built
 *   with CONFIG_BOARDCTL_RESET_CAUSE.
 *
 * Input Parameters:
 *   reason - Reset reason to record
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_reset_reason_set(enum bl616cl_reset_reason_e reason)
{
  uint32_t code;

  DEBUGASSERT(reason < nitems(g_bl616cl_reset_keep_code));
  code = g_bl616cl_reset_keep_code[reason];
  BL616CL_RESET_KEEP->reset_reason = code;
  BL616CL_RESET_KEEP->reset_reason_chk = code ^ BL616CL_RESET_KEEP_CHK;
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
