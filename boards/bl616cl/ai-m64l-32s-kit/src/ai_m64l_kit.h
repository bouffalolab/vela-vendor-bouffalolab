/****************************************************************************
 * vendor/bouffalolab/boards/bl616cl/ai-m64l-32s-kit/src/ai_m64l_kit.h
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

#ifndef __VENDOR_BOUFFALOLAB_BOARDS_BL616CL_AI_M64L_KIT_AI_M64L_KIT_H
#define __VENDOR_BOUFFALOLAB_BOARDS_BL616CL_AI_M64L_KIT_AI_M64L_KIT_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define AI_M64L_KIT_UART1_TX_PIN 14
#define AI_M64L_KIT_UART1_RX_PIN 15

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_AI_M64L_KIT_UART1

/****************************************************************************
 * Name: ai_m64l_kit_uart_initialize
 *
 * Description:
 *   Register UART1 as /dev/ttyS1 on AI_M64L_KIT_UART1_TX_PIN (GPIO14) and
 *   AI_M64L_KIT_UART1_RX_PIN (GPIO15).
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   The result of bl616cl_uart1_register(): zero (OK) on success; a negated
 *   errno value on failure.
 *
 ****************************************************************************/

int ai_m64l_kit_uart_initialize(void);
#endif

#ifdef CONFIG_BL616CL_GPIO

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

int ai_m64l_kit_gpio_initialize(void);
#endif

#if defined(CONFIG_AI_M64L_KIT_I2C0) || defined(CONFIG_AI_M64L_KIT_I2C1)

/****************************************************************************
 * Name: ai_m64l_kit_i2c_initialize
 *
 * Description:
 *   Register the I2C buses enabled for this board, I2C0 and/or I2C1, on the
 *   pins selected by CONFIG_AI_M64L_KIT_I2Cn_SCL_PIN and
 *   CONFIG_AI_M64L_KIT_I2Cn_SDA_PIN.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value if a bus fails to register.
 *
 ****************************************************************************/

int ai_m64l_kit_i2c_initialize(void);
#endif

#ifdef CONFIG_AI_M64L_KIT_SPI0

/****************************************************************************
 * Name: ai_m64l_kit_spi_initialize
 *
 * Description:
 *   Configure the SPI0 pins selected in Kconfig, drive each target chip
 *   select to its inactive level before enabling it as an output, and
 *   register SPI0 as /dev/spi0 with the board chip select callback. The
 *   bus is uninitialized again if registration fails.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero (OK) on success; -ENODEV if the GPIO device or the SPI bus is not
 *   available, or the negated errno value from
 *   bl616cl_spi_configure_pins() or spi_register().
 *
 ****************************************************************************/

int ai_m64l_kit_spi_initialize(void);
#endif

#ifdef CONFIG_AI_M64L_KIT_PWM

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

int ai_m64l_kit_pwm_initialize(void);
#endif

#endif /* __VENDOR_BOUFFALOLAB_BOARDS_BL616CL_AI_M64L_KIT_AI_M64L_KIT_H */
