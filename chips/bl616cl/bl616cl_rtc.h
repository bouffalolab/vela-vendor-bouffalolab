/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_rtc.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_RTC_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_RTC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_rtc_register
 *
 * Description:
 *   Register the RTC as an RTC character device with rtc_initialize().
 *
 * Input Parameters:
 *   minor - Device minor number of the RTC device.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure:
 *
 *     -ENODEV - up_rtc_initialize() has not completed.
 *     Other errors are returned from rtc_initialize().
 *
 ****************************************************************************/

int bl616cl_rtc_register(int minor);

/****************************************************************************
 * Name: bl616cl_rtc_counter
 *
 * Description:
 *   Read the 48-bit RTC hardware counter.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The RTC counter value.
 *
 ****************************************************************************/

uint64_t bl616cl_rtc_counter(void);

/****************************************************************************
 * Name: bl616cl_rtc_clock_numerator
 *
 * Description:
 *   Get the numerator of the RTC clock frequency ratio; the RTC counter runs
 *   at numerator / denominator Hz.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The numerator of the RTC clock frequency.
 *
 ****************************************************************************/

uint32_t bl616cl_rtc_clock_numerator(void);

/****************************************************************************
 * Name: bl616cl_rtc_clock_denominator
 *
 * Description:
 *   Get the denominator of the RTC clock frequency ratio; the RTC counter
 *   runs at numerator / denominator Hz.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The denominator of the RTC clock frequency.
 *
 ****************************************************************************/

uint32_t bl616cl_rtc_clock_denominator(void);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_RTC_H */
