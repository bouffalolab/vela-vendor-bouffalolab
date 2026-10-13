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

#define BL616CL_PWM_TEST_NCHANNELS 4

/* Per-channel state. The register fields are valid only while the PWM clock
 * is enabled; the pin levels follow the pins only while they are plain GPIO
 * outputs, that is after STOP or before the first START.
 */

struct bl616cl_pwm_test_channel_s
{
  int8_t pos_pin;               /* Positive output GPIO, -1 if unused */
  int8_t neg_pin;               /* Negative output GPIO, -1 if unused */
  uint16_t threshold_low;
  uint16_t threshold_high;
  uint8_t cpol;                 /* Last applied cpol */
  uint8_t dcpol;                /* Last applied dcpol */
  bool configured;              /* Channel provided by the board */
  bool active;                  /* Running since the last START */
  bool enabled;                 /* Output enables set by the lower half */
  bool pos_output;              /* PEN */
  bool neg_output;              /* NEN */
  bool pos_polarity_high;       /* PPL */
  bool neg_polarity_high;       /* NPL */
  bool pos_stop_active;         /* PSI */
  bool neg_stop_active;         /* NSI */
  bool pos_level;               /* GPIO input level of pos_pin */
  bool neg_level;               /* GPIO input level of neg_pin */
};

struct bl616cl_pwm_test_diag_s
{
  uint32_t setup_calls;
  uint32_t start_calls;
  uint32_t stop_calls;
  uint32_t shutdown_calls;
  uint32_t solve_calls;
  uint32_t source_frequency;
  uint32_t actual_frequency;
  uint32_t error_count;
  int last_error;
  uint16_t divider;
  uint16_t period;
  bool pin_acquired;
  bool clock_enabled;
  bool started;
  struct bl616cl_pwm_test_channel_s channels[BL616CL_PWM_TEST_NCHANNELS];
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_pwm_test_reset
 *
 * Description:
 *   Clear the test fault, call counters, last error and the cached
 *   divider/period solution.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void bl616cl_pwm_test_reset(void);

/****************************************************************************
 * Name: bl616cl_pwm_test_set_fault
 *
 * Description:
 *   Select the fault to inject into later operations.
 *
 * Input Parameters:
 *   fault - Fault to inject, BL616CL_PWM_TEST_FAULT_NONE to disable
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. -EINVAL if the
 *   fault value is out of range.
 *
 ****************************************************************************/

int bl616cl_pwm_test_set_fault(enum bl616cl_pwm_test_fault_e fault);

/****************************************************************************
 * Name: bl616cl_pwm_test_get_diag
 *
 * Description:
 *   Copy the call counters, last error, cached settings and the per-channel
 *   state to a diagnostics structure. Output enables, polarity and stop
 *   states are read from the registers while the clock is enabled; the pin
 *   levels are read from the GPIO input register.
 *
 * Input Parameters:
 *   diag - Location that receives the diagnostics
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. -EINVAL if diag
 *   is NULL.
 *
 ****************************************************************************/

int bl616cl_pwm_test_get_diag(struct bl616cl_pwm_test_diag_s *diag);

#endif

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_PWM_H */
