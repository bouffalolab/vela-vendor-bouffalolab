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
#define BL616CL_SDK_GLB_XTAL_40M          4
#define BL616CL_SDK_GLB_PLL_WIFIPLL       1
#define BL616CL_SDK_GLB_SYS_CLK_WIFIPLL   5
#define BL616CL_SDK_HBN_MCU_XCLK_XTAL     1
#define BL616CL_SDK_SYSTEM_CLOCK_XCLK     5
#define BL616CL_SDK_MTIMER_SOURCE_XCLK    0
#define BL616CL_SDK_ENABLE                1

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

extern uint32_t bl616cl_sdk_clock_system_clock_get(int type)
  __asm__("Clock_System_Clock_Get");
extern int bl616cl_sdk_glb_power_on_xtal_and_pll_clk(uint8_t xtal_type,
                                                     uint8_t pll_type)
  __asm__("GLB_Power_On_XTAL_And_PLL_CLK");
extern int bl616cl_sdk_glb_set_mcu_system_clk(uint8_t clk_freq)
  __asm__("GLB_Set_MCU_System_CLK");
extern int bl616cl_sdk_hbn_set_mcu_xclk_sel(uint8_t xclk)
  __asm__("HBN_Set_MCU_XCLK_Sel");
extern int bl616cl_sdk_cpu_set_mtimer_clk(uint8_t enable, int source,
                                          uint16_t div)
  __asm__("CPU_Set_MTimer_CLK");

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_clock_early_init
 ****************************************************************************/

void bl616cl_clock_early_init(void)
{
  /* SDK system_clock_init() powers the XTAL/WIFIPLL and moves the MCU
   * clock to WIFIPLL 320 MHz here. Flash retuning and WiFi clock ungate
   * stay out of this early hook.
   */

  (void)bl616cl_sdk_glb_power_on_xtal_and_pll_clk(
    BL616CL_SDK_GLB_XTAL_40M,
    BL616CL_SDK_GLB_PLL_WIFIPLL);
  (void)bl616cl_sdk_glb_set_mcu_system_clk(
    BL616CL_SDK_GLB_SYS_CLK_WIFIPLL);
  (void)bl616cl_sdk_hbn_set_mcu_xclk_sel(BL616CL_SDK_HBN_MCU_XCLK_XTAL);
}

/****************************************************************************
 * Name: bl616cl_timer_clock_init
 ****************************************************************************/

void bl616cl_timer_clock_init(void)
{
  uint32_t div;
  uint32_t xclk;

  xclk = bl616cl_sdk_clock_system_clock_get(BL616CL_SDK_SYSTEM_CLOCK_XCLK);
  div = xclk / BL616CL_MTIMER_FREQ;

  DEBUGASSERT(div > 0);

  (void)bl616cl_sdk_cpu_set_mtimer_clk(BL616CL_SDK_ENABLE,
                                       BL616CL_SDK_MTIMER_SOURCE_XCLK,
                                       div - 1);
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
