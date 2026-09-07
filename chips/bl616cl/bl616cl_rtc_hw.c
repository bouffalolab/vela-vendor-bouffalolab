/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_rtc_hw.c
 *
 * SPDX-License-Identifier: Apache-2.0
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

void bl616cl_rtc_hw_counter(uint32_t *low, uint32_t *high)
{
  HBN_Get_RTC_Timer_Val(low, high);
}

#ifdef CONFIG_BL616CL_RTC_ALARM
void bl616cl_rtc_hw_set_alarm(uint64_t counter)
{
  HBN_Set_RTC_Timer(HBN_RTC_INT_DELAY_0T, (uint32_t)counter,
                   (uint32_t)(counter >> 32),
                   HBN_RTC_COMP_BIT0_47);
}

void bl616cl_rtc_hw_clear_alarm(void)
{
  HBN_Clear_RTC_INT();
  HBN_Clear_IRQ(HBN_INT_RTC);
}

int bl616cl_rtc_hw_alarm_pending(void)
{
  return HBN_Get_INT_State(HBN_INT_RTC) != RESET;
}
#endif
