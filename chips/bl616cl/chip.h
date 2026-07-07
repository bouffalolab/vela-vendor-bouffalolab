/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/chip.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_CHIP_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_CHIP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_FLASH_XIP_BASE   0x80000000
#define BL616CL_OCRAM_BASE       0x20fc0000
#define BL616CL_OCRAM_SIZE       (224 * 1024)
#define BL616CL_WRAM_BASE        0x20ff8000
#define BL616CL_WRAM_SIZE        (160 * 1024)
#define BL616CL_RAM_BASE         BL616CL_OCRAM_BASE
#define BL616CL_RAM_SIZE         (BL616CL_OCRAM_SIZE + BL616CL_WRAM_SIZE)

#define BL616CL_UART0_BASE       0x2000a000
#define BL616CL_CLIC_BASE        0xe0800000

#define BL616CL_IRQ_ASYNC_BASE   16
#define BL616CL_IRQ_MTIME        7
#define BL616CL_IRQ_LAST         (BL616CL_IRQ_ASYNC_BASE + 67)

#define BL616CL_UART_TXFIFO_SIZE 32
#define BL616CL_UART_CLOCK       40000000

#define HAVE_UART_DEVICE         1
#define HAVE_SERIAL_CONSOLE      1

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_CHIP_H */
