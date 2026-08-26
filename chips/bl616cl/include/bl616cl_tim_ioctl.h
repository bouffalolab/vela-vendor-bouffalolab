/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/include/bl616cl_tim_ioctl.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIP_BL616CL_INCLUDE_BL616CL_TIM_IOCTL_H
#define __VENDOR_BOUFFALOLAB_CHIP_BL616CL_INCLUDE_BL616CL_TIM_IOCTL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/timers/timer.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Set the timer clock divider (0..255). */

#define BL616CL_TCIOC_SETCLOCKDIV _TCIOC(0x0010)

#endif /* __VENDOR_BOUFFALOLAB_CHIP_BL616CL_INCLUDE_BL616CL_TIM_IOCTL_H */
