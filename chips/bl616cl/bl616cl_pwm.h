/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_pwm.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_PWM_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_PWM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <nuttx/timers/pwm.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_pwm_initialize
 *
 * Description:
 *   Get the PWM lower half. Only the fixed channel and pin of this port are
 *   supported. Looks up the PWM and GPIO devices.
 *
 * Input Parameters:
 *   channel - PWM channel number; must be BL616CL_PWM_CHANNEL
 *   pin     - GPIO pin number; must be BL616CL_PWM_PIN
 *
 * Returned Value:
 *   A pointer to the PWM lower half on success; NULL if the channel or pin is
 *   unsupported or a device lookup fails.
 *
 ****************************************************************************/

struct pwm_lowerhalf_s *bl616cl_pwm_initialize(uint8_t channel,
                                               uint8_t pin);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_PWM_H */
