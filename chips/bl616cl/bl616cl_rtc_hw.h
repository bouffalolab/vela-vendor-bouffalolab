/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_rtc_hw.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_RTC_HW_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_RTC_HW_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void bl616cl_rtc_hw_initialize(void);
void bl616cl_rtc_hw_counter(uint32_t *low, uint32_t *high);
#ifdef CONFIG_BL616CL_RTC_ALARM
void bl616cl_rtc_hw_set_alarm(uint64_t counter);
void bl616cl_rtc_hw_clear_alarm(void);
int bl616cl_rtc_hw_alarm_pending(void);
#endif

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_RTC_HW_H */
