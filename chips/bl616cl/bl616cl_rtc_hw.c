/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_rtc_hw.c
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

#include "bl616cl_sdk.h"
#include "bl616cl_glb.h"
#include "bl616cl_hbn.h"
#include "bl616cl_rtc_hw.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_RTC_DIG32K_DIV 1221

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_rtc_hw_initialize
 *
 * Description:
 *   Select the RTC clock and start the RTC counter. With
 *   CONFIG_BL616CL_RTC_CLOCK_DIG32K the 32 kHz clock is derived from XCLK
 *   with a divider of BL616CL_RTC_DIG32K_DIV; otherwise the RC32K oscillator
 *   is kept on and used.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void bl616cl_rtc_hw_initialize(void)
{
  HBN_Set_RTC_CLK_Sel(HBN_RTC_CLK_F32K);

#ifdef CONFIG_BL616CL_RTC_CLOCK_DIG32K
  GLB_Set_DIG_CLK_Sel(GLB_DIG_CLK_XCLK);
  GLB_Set_DIG_32K_CLK(ENABLE, DISABLE, BL616CL_RTC_DIG32K_DIV);
  HBN_32K_Sel(HBN_32K_DIG);
#else
  HBN_Keep_On_RC32K();
  HBN_32K_Sel(HBN_32K_RC);
#endif

  HBN_Enable_RTC_Counter();
}

/****************************************************************************
 * Name: bl616cl_rtc_hw_counter
 *
 * Description:
 *   Read the RTC counter value from the HBN block.
 *
 * Input Parameters:
 *   low - Location to return the low 32 bits of the counter.
 *   high - Location to return the high 32 bits of the counter.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void bl616cl_rtc_hw_counter(uint32_t *low, uint32_t *high)
{
  HBN_Get_RTC_Timer_Val(low, high);
}

#ifdef CONFIG_BL616CL_RTC_ALARM
/****************************************************************************
 * Name: bl616cl_rtc_hw_set_alarm
 *
 * Description:
 *   Program the RTC alarm comparator with no interrupt delay, comparing bits
 *   0 to 47 of the counter.
 *
 * Input Parameters:
 *   counter - Counter value at which the alarm interrupt fires.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void bl616cl_rtc_hw_set_alarm(uint64_t counter)
{
  HBN_Set_RTC_Timer(HBN_RTC_INT_DELAY_0T, (uint32_t)counter,
                   (uint32_t)(counter >> 32),
                   HBN_RTC_COMP_BIT0_47);
}

/****************************************************************************
 * Name: bl616cl_rtc_hw_clear_alarm
 *
 * Description:
 *   Clear the RTC interrupt and its HBN interrupt flag.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void bl616cl_rtc_hw_clear_alarm(void)
{
  HBN_Clear_RTC_INT();
  HBN_Clear_IRQ(HBN_INT_RTC);
}

/****************************************************************************
 * Name: bl616cl_rtc_hw_alarm_pending
 *
 * Description:
 *   Check whether the RTC interrupt is pending in the HBN block.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Non-zero if the RTC interrupt is pending; zero otherwise.
 *
 ****************************************************************************/

int bl616cl_rtc_hw_alarm_pending(void)
{
  return HBN_Get_INT_State(HBN_INT_RTC) != RESET;
}
#endif
