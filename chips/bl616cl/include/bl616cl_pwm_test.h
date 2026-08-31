/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/include/bl616cl_pwm_test.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_PWM_TEST_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_PWM_TEST_H

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

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_PWM_TEST_H */
