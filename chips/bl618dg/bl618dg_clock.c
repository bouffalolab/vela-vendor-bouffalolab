/****************************************************************************
 * vendor/bouffalolab/chips/bl618dg/bl618dg_clock.c
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

#include <sys/types.h>

#include "bl618dg_sdk.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include "bl618dg_aon.h"
#include "bl618dg_clock.h"
#include "bl618dg_glb.h"
#include "bl618dg_hbn.h"
#pragma GCC diagnostic pop

#include "bl618dg_clockconfig.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl618dg_clock_early_init
 *
 * Description:
 *   Establish the SDK AP clock tree: 40 MHz crystal, CPU PLL and MCU XCLK
 *   from the crystal. The Nuclei AP MTIMER always uses MCU XCLK; the SDK
 *   setter programs its 16-bit divider. Reject a rate that is not exactly
 *   representable and verify the configured rate before starting NuttX.
 *
 * Returned Value:
 *   Zero on success, -1 if clock setup or timer readback fails.
 *
 ****************************************************************************/

int __attribute__((section(".sclock_rlt_code.bl618dg_clock_early_init")))
bl618dg_clock_early_init(void)
{
  uint32_t source;
  uint32_t divider;

  if (GLB_Power_On_XTAL_And_PLL_CLK(GLB_XTAL_40M,
        GLB_PLL_WIFIPLL | GLB_PLL_CPUPLL) != SUCCESS ||
      GLB_Set_MCU_System_CLK(GLB_MCU_SYS_CLK_CPUPLL_DIV1) != SUCCESS ||
      HBN_Set_MCU_XCLK_Sel(HBN_MCU_XCLK_XTAL) != SUCCESS)
    {
      return -1;
    }

  AON_Set_RC32M_Speed_As_8M(0);

  source = CPU_Get_MTimer_Source_Clock();
  if (source == 0 || source % BL618DG_MTIMER_FREQ != 0)
    {
      return -1;
    }

  divider = source / BL618DG_MTIMER_FREQ;
  if (divider == 0 || divider > 65536)
    {
      return -1;
    }

  if (CPU_Set_MTimer_CLK(ENABLE, BL_MTIMER_SOURCE_CLOCK_MCU_XCLK,
                         divider - 1) != SUCCESS ||
      CPU_Get_MTimer_Clock() != BL618DG_MTIMER_FREQ)
    {
      return -1;
    }

  return 0;
}
