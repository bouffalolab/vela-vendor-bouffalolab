/****************************************************************************
 * vendor/bouffalolab/boards/bl616cl/ai-m64p-32s-kit/src/ai_m64p_kit_pwm.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <sys/param.h>

#include <nuttx/timers/pwm.h>

#include "bl616cl_pwm.h"
#include "ai_m64p_kit.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* A selected channel needs at least one pin, and each pin must carry that
 * channel output: positive on pin % 8 == 2 * channel, negative on the next
 * odd slot.
 */

#define AI_M64P_KIT_PWM_PIN_BAD(pin, slot) ((pin) >= 0 && (pin) % 8 != (slot))
#define AI_M64P_KIT_PWM_BAD(pos, neg, ch)                    \
  (AI_M64P_KIT_PWM_PIN_BAD(pos, (ch) * 2) ||                 \
   AI_M64P_KIT_PWM_PIN_BAD(neg, (ch) * 2 + 1) ||             \
   ((pos) < 0 && (neg) < 0))
#define AI_M64P_KIT_PWM_USED(pos, neg) ((pos) >= 0 || (neg) >= 0)

#if defined(CONFIG_AI_M64P_KIT_PWM_CH0) && \
  AI_M64P_KIT_PWM_BAD(AI_M64P_KIT_PWM_CH0_POS, AI_M64P_KIT_PWM_CH0_NEG, 0)
#  error "PWM channel 0 needs pin % 8 == 0 (positive) or 1 (negative)"
#endif
#if defined(CONFIG_AI_M64P_KIT_PWM_CH1) && \
  AI_M64P_KIT_PWM_BAD(AI_M64P_KIT_PWM_CH1_POS, AI_M64P_KIT_PWM_CH1_NEG, 1)
#  error "PWM channel 1 needs pin % 8 == 2 (positive) or 3 (negative)"
#endif
#if defined(CONFIG_AI_M64P_KIT_PWM_CH2) && \
  AI_M64P_KIT_PWM_BAD(AI_M64P_KIT_PWM_CH2_POS, AI_M64P_KIT_PWM_CH2_NEG, 2)
#  error "PWM channel 2 needs pin % 8 == 4 (positive) or 5 (negative)"
#endif
#if defined(CONFIG_AI_M64P_KIT_PWM_CH3) && \
  AI_M64P_KIT_PWM_BAD(AI_M64P_KIT_PWM_CH3_POS, AI_M64P_KIT_PWM_CH3_NEG, 3)
#  error "PWM channel 3 needs pin % 8 == 6 (positive) or 7 (negative)"
#endif

#define AI_M64P_KIT_PWM_NCHANNELS                                           \
  (AI_M64P_KIT_PWM_USED(AI_M64P_KIT_PWM_CH0_POS, AI_M64P_KIT_PWM_CH0_NEG) + \
   AI_M64P_KIT_PWM_USED(AI_M64P_KIT_PWM_CH1_POS, AI_M64P_KIT_PWM_CH1_NEG) + \
   AI_M64P_KIT_PWM_USED(AI_M64P_KIT_PWM_CH2_POS, AI_M64P_KIT_PWM_CH2_NEG) + \
   AI_M64P_KIT_PWM_USED(AI_M64P_KIT_PWM_CH3_POS, AI_M64P_KIT_PWM_CH3_NEG))

#if AI_M64P_KIT_PWM_NCHANNELS == 0
#  error "Select at least one PWM channel"
#endif
#if !defined(CONFIG_PWM_MULTICHAN) && AI_M64P_KIT_PWM_NCHANNELS != 1
#  error "Select exactly one PWM channel unless CONFIG_PWM_MULTICHAN is set"
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct bl616cl_pwm_output_s g_ai_m64p_kit_pwm_outputs[] =
{
#ifdef CONFIG_AI_M64P_KIT_PWM_CH0
  { 0, AI_M64P_KIT_PWM_CH0_POS, AI_M64P_KIT_PWM_CH0_NEG },
#endif
#ifdef CONFIG_AI_M64P_KIT_PWM_CH1
  { 1, AI_M64P_KIT_PWM_CH1_POS, AI_M64P_KIT_PWM_CH1_NEG },
#endif
#ifdef CONFIG_AI_M64P_KIT_PWM_CH2
  { 2, AI_M64P_KIT_PWM_CH2_POS, AI_M64P_KIT_PWM_CH2_NEG },
#endif
#ifdef CONFIG_AI_M64P_KIT_PWM_CH3
  { 3, AI_M64P_KIT_PWM_CH3_POS, AI_M64P_KIT_PWM_CH3_NEG },
#endif
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ai_m64p_kit_pwm_initialize
 *
 * Description:
 *   Initialize the PWM0 channels selected in Kconfig on their positive and
 *   negative output pins and register them as /dev/pwm0.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero (OK) on success; -ENODEV if the PWM cannot be initialized, or the
 *   negated errno value from pwm_register().
 *
 ****************************************************************************/

int ai_m64p_kit_pwm_initialize(void)
{
  struct pwm_lowerhalf_s *pwm;

  pwm = bl616cl_pwm_initialize(g_ai_m64p_kit_pwm_outputs,
                               nitems(g_ai_m64p_kit_pwm_outputs));
  if (pwm == NULL)
    {
      return -ENODEV;
    }

  return pwm_register("/dev/pwm0", pwm);
}
