/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_rtc.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_RTC_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_RTC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bl616cl_rtc_register(int minor);
uint64_t bl616cl_rtc_counter(void);
uint32_t bl616cl_rtc_clock_numerator(void);
uint32_t bl616cl_rtc_clock_denominator(void);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_RTC_H */
