/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_clockconfig.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_CLOCKCONFIG_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_CLOCKCONFIG_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/* Target frequency programmed by bl616cl_timer_clock_init(). */

#define BL616CL_MTIMER_FREQ 1000000

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_clock_early_init
 *
 * Description:
 *   Power on the 40 MHz XTAL and the WIFIPLL, switch the MCU system clock to
 *   WIFIPLL 320 MHz and select XTAL as the MCU XCLK. The function is placed
 *   in the .sclock_rlt_code.bl616cl_clock_early_init section
 *   (BL616CL_CLOCK_SAFE).
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void bl616cl_clock_early_init(void);

/****************************************************************************
 * Name: bl616cl_timer_clock_init
 *
 * Description:
 *   Enable the MTIMER clock, sourced from the MCU XCLK and divided down to
 *   BL616CL_MTIMER_FREQ. The divider is derived from the current XCLK rate
 *   (asserted to be non-zero).
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void bl616cl_timer_clock_init(void);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_CLOCKCONFIG_H */
