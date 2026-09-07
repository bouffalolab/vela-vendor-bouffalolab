/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_bus.c
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

#include <assert.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include "riscv_internal.h"

#include "bl616cl_bus.h"
#include "bl616cl_sdk.h"
#include "bl616cl_glb.h"
#include "mcu_misc_reg.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Register offsets and bitfields come directly from mcu_misc_reg.h. */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_bus_error_interrupt
 ****************************************************************************/

static int bl616cl_bus_error_interrupt(int irq, void *context, void *arg)
{
  UNUSED(irq);
  UNUSED(context);
  UNUSED(arg);

  PANIC();
  return OK;
}

/****************************************************************************
 * Name: bl616cl_bus_error_enable
 ****************************************************************************/

static void bl616cl_bus_error_enable(void)
{
  uint32_t regval;

  GLB_Bus_Decoder_Err_Disable();

  regval = getreg32(MCU_MISC_BASE +
                    MCU_MISC_MCU_BUS_CFG0_OFFSET);
  regval |= MCU_MISC_REG_MCU_INFRA_TIMEOUT_EN_MSK;
  putreg32(regval, MCU_MISC_BASE +
                   MCU_MISC_MCU_BUS_CFG0_OFFSET);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_bus_error_initialize
 *
 * Description:
 *   Enable BL616CL MCU bus error and timeout traps after core bring-up.
 *
 ****************************************************************************/

int bl616cl_bus_error_initialize(void)
{
  int ret;

  ret = irq_attach(BL616CL_IRQ_BMX_MCU_BUS_ERR,
                   bl616cl_bus_error_interrupt,
                   NULL);
  if (ret < 0)
    {
      return ret;
    }

  ret = irq_attach(BL616CL_IRQ_BMX_MCU_TIMEOUT,
                   bl616cl_bus_error_interrupt,
                   NULL);
  if (ret < 0)
    {
      return ret;
    }

  bl616cl_bus_error_enable();

  up_enable_irq(BL616CL_IRQ_BMX_MCU_BUS_ERR);
  up_enable_irq(BL616CL_IRQ_BMX_MCU_TIMEOUT);

  return OK;
}
