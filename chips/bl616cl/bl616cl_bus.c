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
#include "chip.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_MCU_MISC_MCU_BUS_CFG0_OFFSET 0x0000
#define BL616CL_MCU_MISC_TIMEOUT_EN          (1u << 0)
#define BL616CL_MCU_MISC_DEC_ERR_RSP         (1u << 2)

#define BL616CL_MCU_BUS_CFG0 \
  (BL616CL_MCU_MISC_BASE + BL616CL_MCU_MISC_MCU_BUS_CFG0_OFFSET)

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

  regval = getreg32(BL616CL_MCU_BUS_CFG0);
  regval |= BL616CL_MCU_MISC_TIMEOUT_EN;
  regval &= ~BL616CL_MCU_MISC_DEC_ERR_RSP;
  putreg32(regval, BL616CL_MCU_BUS_CFG0);
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
