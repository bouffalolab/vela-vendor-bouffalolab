/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_lowputc.c
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
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/irq.h>
#include <nuttx/spinlock.h>

#include "riscv_internal.h"

#include "bl616cl_sdk.h"
#include "bl616cl_glb.h"
#include "bl616cl_hbn.h"
#include "bl616cl_pm.h"
#include "bflb_clock.h"
#include "bflb_gpio.h"
#include "bflb_uart.h"
#include "bl616cl_lowputc.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#if defined(CONFIG_BL616CL_UART0) || defined(CONFIG_BL616CL_UART1)
static bool g_uart_clock_configured;

/****************************************************************************
 * Name: bl616cl_data_bits
 *
 * Description:
 *   Map a data bit count to the lhal UART_DATA_BITS_* value. Counts of 5, 6
 *   and 7 map to their own values; any other count maps to 8 bits.
 *
 * Input Parameters:
 *   bits - Number of data bits.
 *
 * Returned Value:
 *   The lhal UART_DATA_BITS_* value.
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: bl616cl_uart_clock_enable
 *
 * Description:
 *   Enable the peripheral clock of a UART and, on the first call, set the
 *   shared UART clock source to XCLK with GLB_Set_UART_CLK().
 *
 * Input Parameters:
 *   id - UART index, 0 or 1.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure:
 *
 *     -EINVAL - id is not 0 or 1.
 *     -EIO    - GLB_Set_UART_CLK() failed.
 *
 ****************************************************************************/

static int bl616cl_uart_clock_enable(uint8_t id)
{
  irqstate_t flags;
  int ret = OK;

  switch (id)
    {
      case 0:
        PERIPHERAL_CLOCK_UART0_ENABLE();
        break;

      case 1:
        PERIPHERAL_CLOCK_UART1_ENABLE();
        break;

      default:
        return -EINVAL;
    }

  flags = enter_critical_section();
  if (!g_uart_clock_configured)
    {
      if (GLB_Set_UART_CLK(ENABLE, HBN_UART_CLK_XCLK, 0) == SUCCESS)
        {
          g_uart_clock_configured = true;
        }
      else
        {
          ret = -EIO;
        }
    }

  leave_critical_section(flags);

  if (ret < 0)
    {
      return ret;
    }

  return OK;
}

/****************************************************************************
 * Name: bl616cl_uart_name
 *
 * Description:
 *   Look up the lhal device name of a UART.
 *
 * Input Parameters:
 *   id - UART index, 0 or 1.
 *
 * Returned Value:
 *   BFLB_NAME_UART0 or BFLB_NAME_UART1; NULL if id is not valid.
 *
 ****************************************************************************/

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
  .irq        = BL616CL_IRQ_NUM_UART0,
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

#ifdef CONFIG_BL616CL_UART1
struct bl616cl_uart_s g_uart1_config =
{
  .id         = 1,
  .irq        = BL616CL_IRQ_NUM_UART1,
  .baud       = CONFIG_UART1_BAUD,
  .data_bits  = CONFIG_UART1_BITS,
  .stop_b2    = CONFIG_UART1_2STOP,
  .parity     = CONFIG_UART1_PARITY,
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
 *   Enable the UART clock, route the TX and RX pins (after disabling their
 *   GPIO keep setting), look up the lhal device and initialize it from the
 *   settings in config with no flow control and LSB first bit order.
 *
 * Input Parameters:
 *   config - UART configuration. On success config->device is set to the lhal
 *            UART device.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure:
 *
 *     -EIO    - The UART clock could not be enabled.
 *     -ENODEV - The GPIO or UART lhal device was not found.
 *     -ENOSYS - Neither CONFIG_BL616CL_UART0 nor CONFIG_BL616CL_UART1 is set.
 *
 ****************************************************************************/

int bl616cl_lowputc_config(struct bl616cl_uart_s *config)
{
#if defined(CONFIG_BL616CL_UART0) || defined(CONFIG_BL616CL_UART1)
  struct bflb_uart_config_s cfg;
  struct bflb_device_s *gpio;

  DEBUGASSERT(config != NULL);

  if (bl616cl_uart_clock_enable(config->id) < 0)
    {
      return -EIO;
    }

  gpio = bflb_device_get_by_name(BFLB_NAME_GPIO);
  DEBUGASSERT(gpio != NULL);
  if (gpio == NULL)
    {
      return -ENODEV;
    }

  pm_disable_gpio_keep(config->txpin);
  pm_disable_gpio_keep(config->rxpin);

  bflb_gpio_uart_init(gpio, config->txpin,
                      (config->id * 4) + GPIO_UART_FUNC_UART0_TX);
  bflb_gpio_uart_init(gpio, config->rxpin,
                      (config->id * 4) + GPIO_UART_FUNC_UART0_RX);

  config->device = bflb_device_get_by_name(bl616cl_uart_name(config->id));
  DEBUGASSERT(config->device != NULL);
  if (config->device == NULL)
    {
      return -ENODEV;
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
  return OK;
#else
  UNUSED(config);
  return -ENOSYS;
#endif
}

/****************************************************************************
 * Name: riscv_lowputc
 *
 * Description:
 *   Output one byte on the serial console.
 *
 *   Implements the RISC-V architecture hook used for early console output.
 *   It is active only when UART0 is the serial console; UART0 is set up with
 *   bl616cl_lowsetup() on first use.  Otherwise it does nothing.
 *
 * Input Parameters:
 *   ch - Character to output.
 *
 * Returned Value:
 *   None.
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
 * Name: bl616cl_pinmux_early_uart
 *
 * Description:
 *   Write 0xffffffff to GLB_UART_CFG1 and 0x0000ffff to GLB_UART_CFG2, then
 *   disable the HBN hardware pull-up/pull-down configuration. Called from
 *   __bl616cl_start() before riscv_earlyserialinit().
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void bl616cl_pinmux_early_uart(void)
{
  putreg32(0xffffffff, GLB_BASE + GLB_UART_CFG1_OFFSET);
  putreg32(0x0000ffff, GLB_BASE + GLB_UART_CFG2_OFFSET);

  HBN_Hw_Pu_Pd_Cfg(DISABLE);
}

/****************************************************************************
 * Name: bl616cl_lowsetup
 *
 * Description:
 *   Initialize the serial console before the full serial driver is
 *   registered.
 *
 *   UART0 is configured with bl616cl_lowputc_config() only when it is the
 *   serial console and CONFIG_SUPPRESS_UART_CONFIG is not set.  Otherwise
 *   it does nothing.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void bl616cl_lowsetup(void)
{
#if defined(HAVE_SERIAL_CONSOLE) && defined(CONFIG_BL616CL_UART0) && \
    !defined(CONFIG_SUPPRESS_UART_CONFIG)
  bl616cl_lowputc_config(&g_uart0_config);
#endif
}
