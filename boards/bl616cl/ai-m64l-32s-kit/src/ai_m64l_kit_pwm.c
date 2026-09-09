/****************************************************************************
 * apps/vendor/bouffalolab/boards/bl616cl/ai-m64l-32s-kit/src/ai_m64l_kit_pwm.c
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

int ai_m64l_kit_pwm_initialize(void)
{
  FAR struct pwm_lowerhalf_s *pwm;

  pwm = bl616cl_pwm_initialize(AI_M64L_KIT_PWM_CHANNEL,
                               AI_M64L_KIT_PWM_PIN);
  if (pwm == NULL)
    {
      return -ENODEV;
    }

  return pwm_register("/dev/pwm0", pwm);
}
