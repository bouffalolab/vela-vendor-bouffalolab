/****************************************************************************
 * vendor/bouffalolab/boards/bl616cl/ai-m64p-32s-kit/src/ai_m64p_kit_adc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>
#include <sys/param.h>

#include <nuttx/analog/adc.h>

#include "bl616cl_adc.h"
#include "ai_m64p_kit.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define AI_M64P_KIT_ADC_USED(pin) ((pin) >= 0)

#define AI_M64P_KIT_ADC_NCHANNELS \
  (AI_M64P_KIT_ADC_USED(AI_M64P_KIT_ADC_CH0_PIN) + \
   AI_M64P_KIT_ADC_USED(AI_M64P_KIT_ADC_CH1_PIN) + \
   AI_M64P_KIT_ADC_USED(AI_M64P_KIT_ADC_CH2_PIN) + \
   AI_M64P_KIT_ADC_USED(AI_M64P_KIT_ADC_CH3_PIN) + \
   AI_M64P_KIT_ADC_USED(AI_M64P_KIT_ADC_CH4_PIN) + \
   AI_M64P_KIT_ADC_USED(AI_M64P_KIT_ADC_CH5_PIN) + \
   AI_M64P_KIT_ADC_USED(AI_M64P_KIT_ADC_CH6_PIN) + \
   AI_M64P_KIT_ADC_USED(AI_M64P_KIT_ADC_CH7_PIN) + \
   AI_M64P_KIT_ADC_USED(AI_M64P_KIT_ADC_CH8_PIN) + \
   AI_M64P_KIT_ADC_USED(AI_M64P_KIT_ADC_CH9_PIN))

#if AI_M64P_KIT_ADC_NCHANNELS == 0
#  error "Select at least one ADC channel"
#endif

/* The UART0 pins are configurable; the bring-up would turn one shared
 * with an ADC input into an analog input.
 */

#ifdef CONFIG_BL616CL_UART0
#  if AI_M64P_KIT_ADC_OWNS(CONFIG_BL616CL_UART0_TXPIN) || \
      AI_M64P_KIT_ADC_OWNS(CONFIG_BL616CL_UART0_RXPIN)
#    error "UART0 pin conflicts with an ADC input"
#  endif
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Scanned in channel order */

static const uint8_t g_ai_m64p_kit_adc_channels[] =
{
#ifdef CONFIG_AI_M64P_KIT_ADC_CH0
  0,
#endif
#ifdef CONFIG_AI_M64P_KIT_ADC_CH1
  1,
#endif
#ifdef CONFIG_AI_M64P_KIT_ADC_CH2
  2,
#endif
#ifdef CONFIG_AI_M64P_KIT_ADC_CH3
  3,
#endif
#ifdef CONFIG_AI_M64P_KIT_ADC_CH4
  4,
#endif
#ifdef CONFIG_AI_M64P_KIT_ADC_CH5
  5,
#endif
#ifdef CONFIG_AI_M64P_KIT_ADC_CH6
  6,
#endif
#ifdef CONFIG_AI_M64P_KIT_ADC_CH7
  7,
#endif
#ifdef CONFIG_AI_M64P_KIT_ADC_CH8
  8,
#endif
#ifdef CONFIG_AI_M64P_KIT_ADC_CH9
  9,
#endif
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ai_m64p_kit_adc_initialize
 *
 * Description:
 *   Set up the GPADC on the ADC channels selected in Kconfig and register
 *   it as /dev/adc0.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero (OK) on success; -ENODEV if the GPADC cannot be set up, or the
 *   negated errno value from adc_register().
 *
 ****************************************************************************/

int ai_m64p_kit_adc_initialize(void)
{
  struct adc_dev_s *adc;

  adc = bl616cl_adc_initialize(g_ai_m64p_kit_adc_channels,
                               nitems(g_ai_m64p_kit_adc_channels));
  if (adc == NULL)
    {
      return -ENODEV;
    }

  return adc_register("/dev/adc0", adc);
}
