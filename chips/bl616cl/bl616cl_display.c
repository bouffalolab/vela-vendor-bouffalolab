/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_display.c
 *
 * GPIO part of the display framework platform layer (display_os.h), as in
 * the SDK's display_os.c. It needs LHAL, so it lives in the chip; the rest
 * of the layer is components/display/nuttx/display_os_nuttx.c.
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include "bl616cl_lhal.h"
#include "bflb_gpio.h"
#include "bflb_name.h"
#include "display_os.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Get the GPIO device, looked up once on first use */

static struct bflb_device_s *bl616cl_display_gpio(void)
{
  static struct bflb_device_s *gpio;

  if (!gpio)
    {
      gpio = bflb_device_get_by_name(BFLB_NAME_GPIO);
    }

  return gpio;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void display_os_gpio_write(uint8_t pin, bool level)
{
  if (level)
    {
      bflb_gpio_set(bl616cl_display_gpio(), pin);
    }
  else
    {
      bflb_gpio_reset(bl616cl_display_gpio(), pin);
    }
}

void display_os_gpio_output(uint8_t pin, bool level)
{
  /* Write the output register first, then enable the output, then write
   * again because bflb_gpio_init() may reset the output value.
   */

  display_os_gpio_write(pin, level);
  bflb_gpio_init(bl616cl_display_gpio(), pin,
                 GPIO_OUTPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_1);
  display_os_gpio_write(pin, level);
}
