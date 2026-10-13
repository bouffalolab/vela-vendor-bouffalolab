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
 * Public Types
 ****************************************************************************/

/* Output pins of one PWM0 channel. GPIO n carries channel (n % 8) / 2: the
 * positive output on an even n % 8 and the negative output on the odd one.
 * The negative output is the complement of the positive output.
 */

struct bl616cl_pwm_output_s
{
  uint8_t channel;              /* Hardware channel, 0 to 3 */
  int8_t pos_pin;               /* Positive output GPIO, -1 if unused */
  int8_t neg_pin;               /* Negative output GPIO, -1 if unused */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_pwm_initialize
 *
 * Description:
 *   Get the PWM0 lower half for the given channel outputs and look up the
 *   PWM and GPIO devices. Each channel may use a positive output, a
 *   negative output or both. Without CONFIG_PWM_MULTICHAN exactly one
 *   channel must be given; with it, channel numbers 1 to 4 in pwm_info_s
 *   select hardware channels 0 to 3.
 *
 * Input Parameters:
 *   outputs  - Channel outputs, one entry per channel
 *   noutputs - Number of entries in outputs
 *
 * Returned Value:
 *   A pointer to the PWM lower half on success; NULL if an entry is invalid
 *   (unknown or repeated channel, no pin, or a pin that does not carry that
 *   channel output), the number of entries is not supported, or a device
 *   lookup fails.
 *
 ****************************************************************************/

struct pwm_lowerhalf_s *
bl616cl_pwm_initialize(const struct bl616cl_pwm_output_s *outputs,
                       unsigned int noutputs);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_PWM_H */
