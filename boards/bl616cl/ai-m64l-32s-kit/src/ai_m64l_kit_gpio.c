/****************************************************************************
 * apps/vendor/bouffalolab/boards/bl616cl/ai-m64l-32s-kit/src/ai_m64l_kit_gpio.c
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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <sys/param.h>

#include <nuttx/ioexpander/gpio.h>

#include "bl616cl_gpio.h"

#ifdef CONFIG_BL616CL_GPIO

/****************************************************************************
 * Pre-processor Types
 ****************************************************************************/

/* One entry per board-exposed test pin (Ai-M64L-32S-Kit):
 *
 *   GPIO19  RGB led red    (board onboard, active high)
 *   GPIO22  RGB led green  (board onboard, active high)
 *   GPIO18  RGB led blue   (board onboard, active high)
 *   GPIO23  warm-white led (board onboard, active high)
 *   GPIO13  cold-white led (board onboard, active high)
 *   GPIO12  loopback output (wire to GPIO20 for readback tests)
 *   GPIO20  loopback input  (wire to GPIO12 for readback tests)
 *
 * Pins shared with the module flash (GPIO6..11, GPIO21), USB
 * (GPIO32/33) and UART0 console (GPIO34/35) are intentionally not
 * registered.
 */

struct ai_m64l_kit_gpio_pin_s
{
  uint8_t pin;                  /* Pin number == /dev/gpioN minor */
  enum gpio_pintype_e pintype;  /* Default pintype at registration */
};

static const struct ai_m64l_kit_gpio_pin_s g_ai_m64l_kit_gpio_pins[] =
{
  { 19, GPIO_OUTPUT_PIN },
  { 22, GPIO_OUTPUT_PIN },
  { 18, GPIO_OUTPUT_PIN },
  { 23, GPIO_OUTPUT_PIN },
  { 13, GPIO_OUTPUT_PIN },
  { 12, GPIO_OUTPUT_PIN },
  { 20, GPIO_INPUT_PIN  },
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ai_m64l_kit_gpio_initialize
 *
 * Description:
 *   Initialize the GPIO controller and register the board test pins listed
 *   in g_ai_m64l_kit_gpio_pins as /dev/gpioN character devices. The pintype
 *   can be changed at runtime through GPIOC_SETPINTYPE.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int ai_m64l_kit_gpio_initialize(void)
{
  FAR struct ioexpander_dev_s *ioe;
  unsigned int i;
  int ret;

  ioe = bl616cl_gpio_initialize();
  if (ioe == NULL)
    {
      gpioerr("ERROR: bl616cl_gpio_initialize failed\n");
      return -ENODEV;
    }

  for (i = 0; i < nitems(g_ai_m64l_kit_gpio_pins); i++)
    {
      FAR const struct ai_m64l_kit_gpio_pin_s *p =
        &g_ai_m64l_kit_gpio_pins[i];

      ret = gpio_lower_half(ioe, p->pin, p->pintype, p->pin);
      if (ret < 0)
        {
          gpioerr("ERROR: Failed to register /dev/gpio%d: %d\n",
                  p->pin, ret);
          return ret;
        }
    }

  return OK;
}

#endif /* CONFIG_BL616CL_GPIO */
