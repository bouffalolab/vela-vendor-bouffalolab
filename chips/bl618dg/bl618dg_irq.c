/****************************************************************************
 * vendor/bouffalolab/chips/bl618dg/bl618dg_irq.c
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
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <arch/irq.h>

#include "riscv_internal.h"

#include "hardware/bl618dg_core.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl618dg_eclic_int_addr
 *
 * Description:
 *   Return the address of a register in a raw ECLIC interrupt slot.
 *
 * Input Parameters:
 *   irq - Raw ECLIC interrupt number.
 *   offset - Register offset within the per-interrupt control block.
 *
 * Returned Value:
 *   Address of the requested register.
 *
 ****************************************************************************/

static uintptr_t bl618dg_eclic_int_addr(int irq, uintptr_t offset)
{
  return ECLIC_BASE + BL618DG_ECLIC_INT_OFFSET +
         (irq * BL618DG_ECLIC_INT_STRIDE) + offset;
}

/****************************************************************************
 * Name: bl618dg_irq_valid
 *
 * Description:
 *   Check whether a NuttX IRQ number maps to an ECLIC interrupt slot.
 *
 * Input Parameters:
 *   irq - NuttX IRQ number.
 *
 * Returned Value:
 *   True if irq is in the ECLIC interrupt range; false otherwise.
 *
 ****************************************************************************/

static bool bl618dg_irq_valid(int irq)
{
  return irq >= RISCV_IRQ_ASYNC && irq < NR_IRQS;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_irqinitialize
 *
 * Description:
 *   Initialize all ECLIC slots as disabled, non-vectored, level-triggered
 *   interrupts at level 1 and clear their pending bits. Set the machine
 *   software interrupt to edge-triggered mode and attach the exception
 *   handlers. Enable global interrupts unless suppressed by configuration.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void up_irqinitialize(void)
{
  uintptr_t addr;
  uint32_t clicinfo;
  uint8_t nlbits;
  int irq;

  up_irq_save();

  clicinfo = getreg32(ECLIC_BASE + BL618DG_ECLIC_INFO_OFFSET);
  nlbits = (clicinfo & CLIC_CLICINFO_CTLBIT_Msk) >>
           CLIC_CLICINFO_CTLBIT_Pos;
  addr = ECLIC_BASE + BL618DG_ECLIC_CFG_OFFSET;
  putreg8((getreg8(addr) & ~CLIC_CLICCFG_NLBIT_Msk) |
          ((nlbits << CLIC_CLICCFG_NLBIT_Pos) & CLIC_CLICCFG_NLBIT_Msk), addr);

  for (irq = 0; irq < BL618DG_ECLIC_IRQ_COUNT; irq++)
    {
      addr = bl618dg_eclic_int_addr(irq, BL618DG_ECLIC_INT_IE_OFFSET);
      putreg8(getreg8(addr) & ~CLIC_INTIE_IE_Msk, addr);
      addr = bl618dg_eclic_int_addr(irq, BL618DG_ECLIC_INT_IP_OFFSET);
      putreg8(getreg8(addr) & ~CLIC_INTIP_IP_Msk, addr);
      addr = bl618dg_eclic_int_addr(irq, BL618DG_ECLIC_INT_ATTR_OFFSET);
      putreg8(getreg8(addr) & ~(CLIC_INTATTR_SHV_Msk | CLIC_INTATTR_TRIG_Msk),
              addr);

      /* Level bits occupy the most significant implemented control bits. */

      if (nlbits > 0 && nlbits <= ECLIC_MAX_NLBITS)
        {
          addr = bl618dg_eclic_int_addr(irq, BL618DG_ECLIC_INT_CTL_OFFSET);
          putreg8((getreg8(addr) & (UINT8_MAX >> nlbits)) |
                  (1u << (ECLIC_MAX_NLBITS - nlbits)), addr);
        }
    }

  addr = bl618dg_eclic_int_addr(RISCV_IRQ_MSOFT - RISCV_IRQ_ASYNC,
                               BL618DG_ECLIC_INT_ATTR_OFFSET);
  putreg8((getreg8(addr) & ~CLIC_INTATTR_TRIG_Msk) |
          (1u << CLIC_INTATTR_TRIG_Pos), addr);
  riscv_exception_attach();

#ifndef CONFIG_SUPPRESS_INTERRUPTS
  riscv_color_intstack();
  up_irq_enable();
#endif
}

/****************************************************************************
 * Name: up_enable_irq
 *
 * Description:
 *   Enable an ECLIC interrupt after translating its NuttX IRQ number to
 *   the raw controller index. Invalid numbers trigger DEBUGASSERT and
 *   are ignored when assertions are disabled.
 *
 * Input Parameters:
 *   irq - NuttX IRQ number.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void up_enable_irq(int irq)
{
  uintptr_t addr;

  DEBUGASSERT(bl618dg_irq_valid(irq));
  if (bl618dg_irq_valid(irq))
    {
      addr = bl618dg_eclic_int_addr(irq - RISCV_IRQ_ASYNC,
                                   BL618DG_ECLIC_INT_IE_OFFSET);
      putreg8(getreg8(addr) | CLIC_INTIE_IE_Msk, addr);
    }
}

/****************************************************************************
 * Name: up_disable_irq
 *
 * Description:
 *   Disable an ECLIC interrupt after translating its NuttX IRQ number to
 *   the raw controller index. Invalid numbers trigger DEBUGASSERT and
 *   are ignored when assertions are disabled.
 *
 * Input Parameters:
 *   irq - NuttX IRQ number.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void up_disable_irq(int irq)
{
  uintptr_t addr;

  DEBUGASSERT(bl618dg_irq_valid(irq));
  if (bl618dg_irq_valid(irq))
    {
      addr = bl618dg_eclic_int_addr(irq - RISCV_IRQ_ASYNC,
                                   BL618DG_ECLIC_INT_IE_OFFSET);
      putreg8(getreg8(addr) & ~CLIC_INTIE_IE_Msk, addr);
    }
}

/****************************************************************************
 * Name: riscv_ack_irq
 *
 * Description:
 *   Clear the pending bit of the ECLIC interrupt mapped to irq. Numbers
 *   outside the ECLIC interrupt range are ignored.
 *
 * Input Parameters:
 *   irq - NuttX IRQ number.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void riscv_ack_irq(int irq)
{
  uintptr_t addr;

  if (bl618dg_irq_valid(irq))
    {
      addr = bl618dg_eclic_int_addr(irq - RISCV_IRQ_ASYNC,
                                   BL618DG_ECLIC_INT_IP_OFFSET);
      putreg8(getreg8(addr) & ~CLIC_INTIP_IP_Msk, addr);
    }
}

/****************************************************************************
 * Name: up_irq_enable
 *
 * Description:
 *   Set the global interrupt enable bit in mstatus.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The previous mstatus value, suitable for up_irq_restore().
 *
 ****************************************************************************/

irqstate_t up_irq_enable(void)
{
  irqstate_t flags;

  __asm__ __volatile__("csrrs %0, mstatus, %1"
                       : "=r" (flags)
                       : "r" (STATUS_IE)
                       : "memory");
  return flags;
}

/****************************************************************************
 * Name: riscv_dispatch_irq
 *
 * Description:
 *   Decode mcause into a NuttX IRQ number. For asynchronous interrupts,
 *   add RISCV_IRQ_ASYNC and acknowledge the ECLIC slot before dispatching
 *   to riscv_doirq(). Synchronous exceptions use their original cause.
 *   NMI (cause 0xfff) and out-of-range causes take the fatal exception
 *   path without accessing an ECLIC slot or the NuttX IRQ table.
 *
 * Input Parameters:
 *   mcause - Value of the mcause CSR.
 *   regs - Saved register context of the interrupted code.
 *
 * Returned Value:
 *   The register context to restore on return from the interrupt.
 *
 ****************************************************************************/

void *riscv_dispatch_irq(uintreg_t mcause, uintreg_t *regs)
{
  int irq = mcause & 0xfff;
  bool async = (mcause & RISCV_IRQ_BIT) != 0;

  if (irq >= (async ? BL618DG_ECLIC_IRQ_COUNT : RISCV_IRQ_ASYNC))
    {
      riscv_exception(irq, regs, NULL);
      return regs;
    }

  if (async)
    {
      irq += RISCV_IRQ_ASYNC;
      riscv_ack_irq(irq);
    }

  return riscv_doirq(irq, regs);
}
