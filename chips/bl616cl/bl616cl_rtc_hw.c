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

#include "bl616cl_rtc_hw.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_RTC_DIG32K_DIV 1221
#define BL616CL_SDK_DISABLE    0
#define BL616CL_SDK_ENABLE     1
#define BL616CL_SDK_HBN_F32K   0
#define BL616CL_SDK_HBN_RC32K  0
#define BL616CL_SDK_HBN_DIG32K 3
#define BL616CL_SDK_HBN_RTC    16
#define BL616CL_SDK_RTC_DELAY  1
#define BL616CL_SDK_RTC_48BIT  1
#define BL616CL_SDK_GLB_XCLK   1

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

extern int bl616cl_sdk_hbn_set_rtc_clk_sel(uint8_t type)
  __asm__("HBN_Set_RTC_CLK_Sel");
extern int bl616cl_sdk_hbn_32k_sel(uint8_t type) __asm__("HBN_32K_Sel");
extern int bl616cl_sdk_hbn_keep_on_rc32k(void)
  __asm__("HBN_Keep_On_RC32K");
extern int bl616cl_sdk_hbn_enable_rtc_counter(void)
  __asm__("HBN_Enable_RTC_Counter");
extern int bl616cl_sdk_hbn_get_rtc_timer_val(uint32_t *low,
                                            uint32_t *high)
  __asm__("HBN_Get_RTC_Timer_Val");
extern int bl616cl_sdk_hbn_set_rtc_timer(uint8_t delay, uint32_t low,
                                         uint32_t high, uint8_t mode)
  __asm__("HBN_Set_RTC_Timer");
extern int bl616cl_sdk_hbn_clear_rtc_int(void)
  __asm__("HBN_Clear_RTC_INT");
extern int bl616cl_sdk_hbn_clear_irq(uint8_t type) __asm__("HBN_Clear_IRQ");
extern int bl616cl_sdk_hbn_get_int_state(uint8_t type)
  __asm__("HBN_Get_INT_State");
extern int bl616cl_sdk_glb_set_dig_clk_sel(uint8_t type)
  __asm__("GLB_Set_DIG_CLK_Sel");
extern int bl616cl_sdk_glb_set_dig_32k_clk(uint8_t enable,
                                           uint8_t compensation,
                                           uint16_t div)
  __asm__("GLB_Set_DIG_32K_CLK");

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void bl616cl_rtc_hw_initialize(void)
{
  (void)bl616cl_sdk_hbn_set_rtc_clk_sel(BL616CL_SDK_HBN_F32K);

#ifdef CONFIG_BL616CL_RTC_CLOCK_DIG32K
  (void)bl616cl_sdk_glb_set_dig_clk_sel(BL616CL_SDK_GLB_XCLK);
  (void)bl616cl_sdk_glb_set_dig_32k_clk(BL616CL_SDK_ENABLE,
                                        BL616CL_SDK_DISABLE,
                                        BL616CL_RTC_DIG32K_DIV);
  (void)bl616cl_sdk_hbn_32k_sel(BL616CL_SDK_HBN_DIG32K);
#else
  (void)bl616cl_sdk_hbn_keep_on_rc32k();
  (void)bl616cl_sdk_hbn_32k_sel(BL616CL_SDK_HBN_RC32K);
#endif

  (void)bl616cl_sdk_hbn_enable_rtc_counter();
}

void bl616cl_rtc_hw_counter(uint32_t *low, uint32_t *high)
{
  (void)bl616cl_sdk_hbn_get_rtc_timer_val(low, high);
}

#ifdef CONFIG_BL616CL_RTC_ALARM
void bl616cl_rtc_hw_set_alarm(uint64_t counter)
{
  (void)bl616cl_sdk_hbn_set_rtc_timer(BL616CL_SDK_RTC_DELAY,
                                      (uint32_t)counter,
                                      (uint32_t)(counter >> 32),
                                      BL616CL_SDK_RTC_48BIT);
}

void bl616cl_rtc_hw_clear_alarm(void)
{
  (void)bl616cl_sdk_hbn_clear_rtc_int();
  (void)bl616cl_sdk_hbn_clear_irq(BL616CL_SDK_HBN_RTC);
}

int bl616cl_rtc_hw_alarm_pending(void)
{
  return bl616cl_sdk_hbn_get_int_state(BL616CL_SDK_HBN_RTC) != 0;
}
#endif
