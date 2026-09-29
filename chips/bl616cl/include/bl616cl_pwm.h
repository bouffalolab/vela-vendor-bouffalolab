/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/include/bl616cl_pwm.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_PWM_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_PWM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

#ifdef CONFIG_BL616CL_PWM_TEST
/* The PWM test application uses this public, configuration-gated extension
 * because the standard PWM API cannot inject setup faults or expose lower-half
 * diagnostics.
 */

enum bl616cl_pwm_test_fault_e
{
  BL616CL_PWM_TEST_FAULT_NONE = 0,
  BL616CL_PWM_TEST_FAULT_INIT_TIMEOUT,
  BL616CL_PWM_TEST_FAULT_START_TIMEOUT,
  BL616CL_PWM_TEST_FAULT_STOP_TIMEOUT,
  BL616CL_PWM_TEST_FAULT_DEINIT_TIMEOUT,
};

struct bl616cl_pwm_test_diag_s
{
  uint32_t setup_calls;
  uint32_t start_calls;
  uint32_t stop_calls;
  uint32_t shutdown_calls;
  uint32_t source_frequency;
  uint32_t actual_frequency;
  uint32_t error_count;
  int last_error;
  uint16_t divider;
  uint16_t period;
  uint16_t threshold_low;
  uint16_t threshold_high;
  uint8_t cpol;
  uint8_t dcpol;
  bool polarity_active_high;
  bool stop_active;
  bool channel_enabled;
  bool pin_acquired;
  bool clock_enabled;
  bool started;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void bl616cl_pwm_test_reset(void);
int bl616cl_pwm_test_set_fault(enum bl616cl_pwm_test_fault_e fault);
int bl616cl_pwm_test_get_diag(FAR struct bl616cl_pwm_test_diag_s *diag);
#endif

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_PWM_H */
