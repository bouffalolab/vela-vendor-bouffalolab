/****************************************************************************
 * vendor/bouffalolab/boards/bl616cl/ai-m64l-32s-kit/src/ai_m64l_kit_pwm.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>

#include <nuttx/timers/pwm.h>

#include "bl616cl_pwm.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define AI_M64L_KIT_PWM_CHANNEL 3
#define AI_M64L_KIT_PWM_PIN     22

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ai_m64l_kit_pwm_initialize
 *
 * Description:
 *   Initialize PWM0 channel 3 on GPIO22 and register it as /dev/pwm0.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero (OK) on success; -ENODEV if the PWM cannot be initialized, or the
 *   negated errno value from pwm_register().
 *
 ****************************************************************************/

int ai_m64l_kit_pwm_initialize(void)
{
  struct pwm_lowerhalf_s *pwm;

  pwm = bl616cl_pwm_initialize(AI_M64L_KIT_PWM_CHANNEL,
                               AI_M64L_KIT_PWM_PIN);
  if (pwm == NULL)
    {
      return -ENODEV;
    }

  return pwm_register("/dev/pwm0", pwm);
}
