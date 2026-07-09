/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_clock.c
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

#include <stdint.h>

#include "riscv_internal.h"

#include "bl616cl_clock.h"
#include "chip.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_GLB_UART_CFG1_OFFSET      0x154
#define BL616CL_GLB_UART_CFG2_OFFSET      0x158
#define BL616CL_HBN_GPIO5_FIXUP_REG       0x2000f014
#define BL616CL_HBN_GPIO5_UNCOMMON_BIT    (1u << 16)

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_clock_early_init
 ****************************************************************************/

void bl616cl_clock_early_init(void)
{
  /* Keep the boot clock tree for now. Board clock switching must later
   * move into RAM-safe code before changing the flash clock.
   */
}

/****************************************************************************
 * Name: bl616cl_pinmux_early_uart
 ****************************************************************************/

void bl616cl_pinmux_early_uart(void)
{
  uint32_t regval;

  putreg32(0xffffffff, BL616CL_GLB_BASE + BL616CL_GLB_UART_CFG1_OFFSET);
  putreg32(0x0000ffff, BL616CL_GLB_BASE + BL616CL_GLB_UART_CFG2_OFFSET);

  regval = getreg32(BL616CL_HBN_GPIO5_FIXUP_REG);
  regval &= ~BL616CL_HBN_GPIO5_UNCOMMON_BIT;
  putreg32(regval, BL616CL_HBN_GPIO5_FIXUP_REG);
}
