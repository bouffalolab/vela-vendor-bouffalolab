/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_lowputc.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_LOWPUTC_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_LOWPUTC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/config.h>

#include "chip.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bflb_device_s;

struct bl616cl_uart_s
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

#ifdef CONFIG_BL616CL_UART0
EXTERN struct bl616cl_uart_s g_uart0_config;
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void bl616cl_lowsetup(void);
void bl616cl_lowputc_config(struct bl616cl_uart_s *config);
void bl616cl_lowputc_rxint(bool enable);

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_LOWPUTC_H */
