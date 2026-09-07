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
#include "bl616cl_sdk.h"
#include "../../drivers/soc/bl616cl/std/include/bl616cl_clock.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_CLOCK_SAFE                \
  __attribute__((section(".sclock_rlt_code.bl616cl_clock_early_init")))

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_clock_early_init
 ****************************************************************************/

void BL616CL_CLOCK_SAFE bl616cl_clock_early_init(void)
{
  /* SDK system_clock_init() powers the XTAL/WIFIPLL and moves the MCU
   * clock to WIFIPLL 320 MHz here. GLB_Set_MCU_System_CLK() also ungates
   * the WiFi PLL; flash retuning stays out of this early hook.
   */

  GLB_Power_On_XTAL_And_PLL_CLK(GLB_XTAL_40M, GLB_PLL_WIFIPLL);
  GLB_Set_MCU_System_CLK(GLB_MCU_SYS_CLK_TOP_WIFIPLL_320M);
  HBN_Set_MCU_XCLK_Sel(HBN_MCU_XCLK_XTAL);
}

/****************************************************************************
 * Name: bl616cl_timer_clock_init
 ****************************************************************************/

void bl616cl_timer_clock_init(void)
{
  uint32_t div;
  uint32_t xclk;

  xclk = Clock_System_Clock_Get(BL_SYSTEM_CLOCK_XCLK);
  div = xclk / BL616CL_MTIMER_FREQ;

  DEBUGASSERT(div > 0);

  CPU_Set_MTimer_CLK(ENABLE, BL_MTIMER_SOURCE_CLOCK_MCU_XCLK,
                    div - 1);
}

/****************************************************************************
 * Name: bl616cl_pinmux_early_uart
 ****************************************************************************/

void bl616cl_pinmux_early_uart(void)
{
  uint32_t regval;

  putreg32(0xffffffff, GLB_BASE + GLB_UART_CFG1_OFFSET);
  putreg32(0x0000ffff, GLB_BASE + GLB_UART_CFG2_OFFSET);

  regval = getreg32(HBN_BASE + HBN_IRQ_MODE_OFFSET);
  regval &= ~HBN_REG_EN_HW_PU_PD_MSK;
  putreg32(regval, HBN_BASE + HBN_IRQ_MODE_OFFSET);
}
