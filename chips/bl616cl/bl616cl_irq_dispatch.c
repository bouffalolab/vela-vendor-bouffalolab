/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_irq_dispatch.c
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

#include <nuttx/irq.h>

#include <arch/irq.h>

#include "riscv_internal.h"

#ifdef CONFIG_BL616CL_PERFMON
#  include "bl616cl_perfmon_internal.h"
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: riscv_dispatch_irq
 *
 * Description:
 *   NuttX RISC-V architecture interface: decode mcause into a NuttX IRQ
 *   number (adding RISCV_IRQ_ASYNC for interrupts), acknowledge it and
 *   dispatch it. With CONFIG_BL616CL_PERFMON the dispatch goes through
 *   bl616cl_perfmon_dispatch(), otherwise directly to riscv_doirq().
 *
 * Input Parameters:
 *   mcause - Value of the mcause CSR
 *   regs - Saved register context of the interrupted code
 *
 * Returned Value:
 *   The register context to restore on return from the interrupt.
 *
 ****************************************************************************/

void *riscv_dispatch_irq(uintreg_t mcause, uintreg_t *regs)
{
  int irq = mcause & 0xfff;

  if ((mcause & RISCV_IRQ_BIT) != 0)
    {
      irq += RISCV_IRQ_ASYNC;
    }

  riscv_ack_irq(irq);
#ifdef CONFIG_BL616CL_PERFMON
  return bl616cl_perfmon_dispatch(irq, regs);
#else
  return riscv_doirq(irq, regs);
#endif
}
