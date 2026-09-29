/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_tim.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIP_BL616CL_BL616CL_TIM_H
#define __VENDOR_BOUFFALOLAB_CHIP_BL616CL_BL616CL_TIM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_timer_initialize
 *
 * Description:
 *   Register a hardware timer as a timer driver device. Enable the timer
 *   0/1/watchdog peripheral clock, stop the timer with its interrupt masked
 *   and register it with timer_register().
 *
 * Input Parameters:
 *   devpath - The device path to register, for example /dev/timer0.
 *   timer - Timer index: 0 for TIMER0 or 1 for TIMER1; it must be enabled by
 *           CONFIG_BL616CL_TIMER0 or CONFIG_BL616CL_TIMER1.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure:
 *
 *     -ENODEV - The timer is not enabled in the configuration or its lhal
 *               device was not found.
 *     -EEXIST - timer_register() failed.
 *
 ****************************************************************************/

int bl616cl_timer_initialize(const char *devpath, uint8_t timer);

#endif /* __VENDOR_BOUFFALOLAB_CHIP_BL616CL_BL616CL_TIM_H */
