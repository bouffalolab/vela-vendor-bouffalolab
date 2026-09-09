/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_pwm.h
 *
 * SPDX-License-Identifier: Apache-2.0
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

FAR struct pwm_lowerhalf_s *bl616cl_pwm_initialize(uint8_t channel,
                                                   uint8_t pin);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_PWM_H */
