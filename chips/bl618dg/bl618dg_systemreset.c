/****************************************************************************
 * vendor/bouffalolab/chips/bl618dg/bl618dg_systemreset.c
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

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include "bl618dg_sdk.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include "bl618dg_aon.h"
#include "bl618dg_glb.h"
#include "bl618dg_hbn.h"
#pragma GCC diagnostic pop

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_systemreset
 *
 * Description:
 *   Switch the 32K clock to RC, wait 1 ms and power off the 32K crystal.
 *   Disable interrupts, software-reset the wireless blocks, wait 10 ms
 *   and trigger a software power-on reset.
 *
 * Returned Value:
 *   Does not return.
 *
 ****************************************************************************/

void up_systemreset(void)
{
  HBN_32K_Sel(HBN_32K_RC);
  up_mdelay(1);
  AON_Power_Off_Xtal_32K();

  up_irq_save();

  GLB_AHB_MCU_Software_Reset(GLB_AHB_MCU_SW_WIFI);
  GLB_AHB_MCU_Software_Reset(GLB_AHB_MCU_SW_BTDM);
  GLB_AHB_MCU_Software_Reset(GLB_AHB_MCU_SW_ZIGBEE);
  GLB_AHB_MCU_Software_Reset(GLB_AHB_MCU_SW_BLE2);
  GLB_AHB_MCU_Software_Reset(GLB_AHB_MCU_SW_ZIGBEE2);

  up_mdelay(10);
  GLB_SW_POR_Reset();

  for (; ; )
    {
    }
}
