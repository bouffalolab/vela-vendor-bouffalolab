/****************************************************************************
 * vendor/bouffalolab/chips/bl618dg/bl618dg_lowputc.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL618DG_BL618DG_LOWPUTC_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL618DG_BL618DG_LOWPUTC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/config.h>

#include "chip.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bflb_device_s;

struct bl618dg_uart_s
{
  uint8_t id;
  uint8_t irq;
  uint8_t txpin;
  uint8_t rxpin;
  uint32_t baud;
  uint8_t data_bits;
  uint8_t stop_b2;
  uint8_t parity;
  uint8_t tx_fifo_th;
  uint8_t rx_fifo_th;
  struct bflb_device_s *device;
};

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef CONFIG_BL618DG_UART0
EXTERN struct bl618dg_uart_s g_uart0_config;
#endif


/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl618dg_pinmux_early_uart
 *
 * Description:
 *   Write 0xffffffff to GLB_UART_CFG1 and 0x0000ffff to GLB_UART_CFG2, then
 *   disable the HBN hardware pull-up/pull-down configuration. Called from
 *   __bl618dg_start() before riscv_earlyserialinit().
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void bl618dg_pinmux_early_uart(void);

/****************************************************************************
 * Name: bl618dg_lowsetup
 *
 * Description:
 *   Initialize the serial console before the full serial driver is
 *   registered.
 *
 *   UART0 is configured with bl618dg_lowputc_config() only when it is the
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

void bl618dg_lowsetup(void);

/****************************************************************************
 * Name: bl618dg_lowputc_config
 *
 * Description:
 *   Configure a UART for non-DMA operation.
 *
 *   Enable the UART clock, route the TX and RX pins, look up the lhal device and initialize it from the
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
 *     -ENOSYS - CONFIG_BL618DG_UART0 is not set.
 *
 ****************************************************************************/

int bl618dg_lowputc_config(struct bl618dg_uart_s *config);

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL618DG_BL618DG_LOWPUTC_H */
