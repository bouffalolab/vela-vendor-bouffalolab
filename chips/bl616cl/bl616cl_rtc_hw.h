/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_rtc_hw.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_RTC_HW_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_RTC_HW_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* XCLK divider for CONFIG_BL616CL_RTC_CLOCK_DIG32K: 40 MHz / 1221 is about
 * 32.76 kHz.  bl616cl_rtc.c converts ticks with the same ratio.
 */

#define BL616CL_RTC_DIG32K_DIV      1221

/****************************************************************************
 * Public Function Prototypes
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

void bl616cl_rtc_hw_initialize(void);

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

void bl616cl_rtc_hw_counter(uint32_t *low, uint32_t *high);

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

void bl616cl_rtc_hw_set_alarm(uint64_t counter);

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

void bl616cl_rtc_hw_clear_alarm(void);

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

int bl616cl_rtc_hw_alarm_pending(void);

#endif

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_RTC_HW_H */
