/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_lowputc.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <stdint.h>

#include "bflb_clock.h"
#include "bflb_gpio.h"
#include "bflb_uart.h"
#include "bl616cl_lowputc.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_SDK_ENABLE         1
#define BL616CL_SDK_UART_CLK_XCLK  2
#define BL616CL_SDK_UART_CLK_DIV   0

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

extern int bl616cl_sdk_glb_set_uart_clk(uint8_t enable, uint8_t clk_sel,
                                        uint8_t div)
  __asm__("GLB_Set_UART_CLK");
extern int bl616cl_sdk_pm_disable_gpio_keep(uint32_t pin)
  __asm__("pm_disable_gpio_keep");

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_BL616CL_UART0
static uint8_t bl616cl_data_bits(uint8_t bits)
{
  switch (bits)
    {
      case 5:
        return UART_DATA_BITS_5;

      case 6:
        return UART_DATA_BITS_6;

      case 7:
        return UART_DATA_BITS_7;

      default:
        return UART_DATA_BITS_8;
    }
}

static void bl616cl_uart_clock_enable(uint8_t id)
{
  switch (id)
    {
      case 0:
        PERIPHERAL_CLOCK_UART0_ENABLE();
        break;

      case 1:
        PERIPHERAL_CLOCK_UART1_ENABLE();
        break;

      default:
        break;
    }

  (void)bl616cl_sdk_glb_set_uart_clk(BL616CL_SDK_ENABLE,
                                     BL616CL_SDK_UART_CLK_XCLK,
                                     BL616CL_SDK_UART_CLK_DIV);
}

static const char *bl616cl_uart_name(uint8_t id)
{
  switch (id)
    {
      case 0:
        return BFLB_NAME_UART0;

      case 1:
        return BFLB_NAME_UART1;

      default:
        return NULL;
    }
}
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef CONFIG_BL616CL_UART0
struct bl616cl_uart_s g_uart0_config =
{
  .id         = 0,
  .irq        = BL616CL_IRQ_UART0,
  .txpin      = CONFIG_BL616CL_UART0_TXPIN,
  .rxpin      = CONFIG_BL616CL_UART0_RXPIN,
  .baud       = CONFIG_UART0_BAUD,
  .data_bits  = CONFIG_UART0_BITS,
  .stop_b2    = CONFIG_UART0_2STOP,
  .parity     = CONFIG_UART0_PARITY,
  .tx_fifo_th = 7,
  .rx_fifo_th = 7,
};
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_lowputc_config
 *
 * Description:
 *   Configure a UART for non-DMA operation.
 *
 ****************************************************************************/

void bl616cl_lowputc_config(struct bl616cl_uart_s *config)
{
#ifdef CONFIG_BL616CL_UART0
  struct bflb_uart_config_s cfg;
  struct bflb_device_s *gpio;

  DEBUGASSERT(config != NULL);

  bl616cl_uart_clock_enable(config->id);

  gpio = bflb_device_get_by_name(BFLB_NAME_GPIO);
  DEBUGASSERT(gpio != NULL);
  if (gpio == NULL)
    {
      return;
    }

  (void)bl616cl_sdk_pm_disable_gpio_keep(config->txpin);
  (void)bl616cl_sdk_pm_disable_gpio_keep(config->rxpin);

  bflb_gpio_uart_init(gpio, config->txpin,
                      (config->id * 4) + GPIO_UART_FUNC_UART0_TX);
  bflb_gpio_uart_init(gpio, config->rxpin,
                      (config->id * 4) + GPIO_UART_FUNC_UART0_RX);

  config->device = bflb_device_get_by_name(bl616cl_uart_name(config->id));
  DEBUGASSERT(config->device != NULL);
  if (config->device == NULL)
    {
      return;
    }

  cfg.baudrate          = config->baud;
  cfg.direction         = UART_DIRECTION_TXRX;
  cfg.data_bits         = bl616cl_data_bits(config->data_bits);
  cfg.stop_bits         = config->stop_b2 ? UART_STOP_BITS_2 :
                                            UART_STOP_BITS_1;
  cfg.parity            = config->parity;
  cfg.bit_order         = UART_LSB_FIRST;
  cfg.flow_ctrl         = UART_FLOWCTRL_NONE;
  cfg.tx_fifo_threshold = config->tx_fifo_th;
  cfg.rx_fifo_threshold = config->rx_fifo_th;

  bflb_uart_init(config->device, &cfg);
#else
  UNUSED(config);
#endif
}

/****************************************************************************
 * Name: riscv_lowputc
 *
 * Description:
 *   Output one byte on the serial console.
 *
 ****************************************************************************/

void riscv_lowputc(char ch)
{
#if defined(HAVE_SERIAL_CONSOLE) && defined(CONFIG_UART0_SERIAL_CONSOLE)
  if (g_uart0_config.device == NULL)
    {
      bl616cl_lowsetup();
    }

  if (g_uart0_config.device != NULL)
    {
      bflb_uart_putchar(g_uart0_config.device, ch);
    }
#endif
}

/****************************************************************************
 * Name: bl616cl_lowsetup
 *
 * Description:
 *   Initialize the serial console before the full serial driver is
 *   registered.
 *
 ****************************************************************************/

void bl616cl_lowsetup(void)
{
#if defined(HAVE_SERIAL_CONSOLE) && defined(CONFIG_BL616CL_UART0) && \
    !defined(CONFIG_SUPPRESS_UART_CONFIG)
  bl616cl_lowputc_config(&g_uart0_config);
#endif
}
