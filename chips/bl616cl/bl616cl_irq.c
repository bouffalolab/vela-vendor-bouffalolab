/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_irq.c
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

#include <arch/csr.h>
#include <arch/irq.h>

#include "bl616cl_irq.h"
#include "chip.h"
#include "riscv_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_CLICCFG_OFFSET           0x0000
#define BL616CL_CLICINFO_OFFSET          0x0004
#define BL616CL_CLICINT_OFFSET           0x1000
#define BL616CL_CLICINT_STRIDE           0x04
#define BL616CL_CLICINT_IP_OFFSET        0x00
#define BL616CL_CLICINT_IE_OFFSET        0x01
#define BL616CL_CLICINT_ATTR_OFFSET      0x02
#define BL616CL_CLICINT_CTL_OFFSET       0x03

#define BL616CL_CLICINFO_CTLBITS_SHIFT   21
#define BL616CL_CLICINFO_CTLBITS_MASK    (0x0f << 21)
#define BL616CL_CLICCFG_NLBITS_SHIFT     1

#define BL616CL_IRQ_RAW_MSOFT            3
#define BL616CL_IRQ_RAW_MTIME            7
#define BL616CL_IRQ_RAW_MEXT             11
#define BL616CL_IRQ_RAW_SDU_SOFT_RST     19
#define BL616CL_IRQ_DEFAULT_PRIORITY     0x40
#define BL616CL_CLIC_ATTR_EDGE_POSITIVE  0x02

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uintptr_t bl616cl_clic_int_addr(int irq, uintptr_t offset)
{
  return BL616CL_CLIC_BASE + BL616CL_CLICINT_OFFSET +
         (irq * BL616CL_CLICINT_STRIDE) + offset;
}

static bool bl616cl_irq_raw_valid(int irq)
{
  return irq >= 0 && irq < BL616CL_IRQ_CLIC_COUNT;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_irq_raw_to_nuttx
 ****************************************************************************/

int bl616cl_irq_raw_to_nuttx(int irq)
{
  return irq + RISCV_IRQ_ASYNC;
}

/****************************************************************************
 * Name: bl616cl_irq_nuttx_to_raw
 ****************************************************************************/

int bl616cl_irq_nuttx_to_raw(int irq)
{
  return irq - RISCV_IRQ_ASYNC;
}

/****************************************************************************
 * Name: bl616cl_clic_enable_raw
 ****************************************************************************/

void bl616cl_clic_enable_raw(int irq)
{
  if (bl616cl_irq_raw_valid(irq))
    {
      putreg8(1, bl616cl_clic_int_addr(irq, BL616CL_CLICINT_IE_OFFSET));
    }
}

/****************************************************************************
 * Name: bl616cl_clic_disable_raw
 ****************************************************************************/

void bl616cl_clic_disable_raw(int irq)
{
  if (bl616cl_irq_raw_valid(irq))
    {
      putreg8(0, bl616cl_clic_int_addr(irq, BL616CL_CLICINT_IE_OFFSET));
    }
}

/****************************************************************************
 * Name: bl616cl_clic_set_pending_raw
 ****************************************************************************/

void bl616cl_clic_set_pending_raw(int irq)
{
  if (bl616cl_irq_raw_valid(irq))
    {
      putreg8(1, bl616cl_clic_int_addr(irq, BL616CL_CLICINT_IP_OFFSET));
    }
}

/****************************************************************************
 * Name: bl616cl_clic_clear_pending_raw
 ****************************************************************************/

void bl616cl_clic_clear_pending_raw(int irq)
{
  if (bl616cl_irq_raw_valid(irq))
    {
      putreg8(0, bl616cl_clic_int_addr(irq, BL616CL_CLICINT_IP_OFFSET));
    }
}

/****************************************************************************
 * Name: bl616cl_clic_set_nlbits
 ****************************************************************************/

void bl616cl_clic_set_nlbits(uint8_t nlbits)
{
  uint8_t cfg;

  cfg = (nlbits & 0x0f) << BL616CL_CLICCFG_NLBITS_SHIFT;
  putreg8(cfg, BL616CL_CLIC_BASE + BL616CL_CLICCFG_OFFSET);
}

/****************************************************************************
 * Name: bl616cl_clic_set_priority_raw
 ****************************************************************************/

void bl616cl_clic_set_priority_raw(int irq, uint8_t preemptprio,
                                   uint8_t subprio)
{
  uint8_t ctl;
  uint8_t clic_int_cfg;
  uint8_t nlbits;

  if (bl616cl_irq_raw_valid(irq))
    {
      nlbits = (getreg8(BL616CL_CLIC_BASE + BL616CL_CLICCFG_OFFSET) >>
                BL616CL_CLICCFG_NLBITS_SHIFT) & 0x0f;
      if (nlbits > 8)
        {
          nlbits = 8;
        }

      clic_int_cfg = getreg8(bl616cl_clic_int_addr(
        irq, BL616CL_CLICINT_CTL_OFFSET));
      ctl = (clic_int_cfg & 0x0f) |
            (preemptprio << (8 - nlbits)) |
            ((subprio & (0x0f >> nlbits)) << 4);
      putreg8(ctl, bl616cl_clic_int_addr(irq, BL616CL_CLICINT_CTL_OFFSET));
    }
}

/****************************************************************************
 * Name: up_irqinitialize
 ****************************************************************************/

void up_irqinitialize(void)
{
  uint32_t clicinfo;
  uint8_t nlbits;
  int irq;

  up_irq_save();

  clicinfo = getreg32(BL616CL_CLIC_BASE + BL616CL_CLICINFO_OFFSET);
  nlbits = (clicinfo & BL616CL_CLICINFO_CTLBITS_MASK) >>
           BL616CL_CLICINFO_CTLBITS_SHIFT;
  bl616cl_clic_set_nlbits(nlbits);

  for (irq = 0; irq < BL616CL_IRQ_CLIC_COUNT; irq++)
    {
      bl616cl_clic_disable_raw(irq);
      bl616cl_clic_clear_pending_raw(irq);
      putreg8(0, bl616cl_clic_int_addr(irq, BL616CL_CLICINT_ATTR_OFFSET));
      putreg8(BL616CL_IRQ_DEFAULT_PRIORITY,
              bl616cl_clic_int_addr(irq, BL616CL_CLICINT_CTL_OFFSET));
    }

  putreg8(BL616CL_CLIC_ATTR_EDGE_POSITIVE,
          bl616cl_clic_int_addr(BL616CL_IRQ_RAW_MSOFT,
                                BL616CL_CLICINT_ATTR_OFFSET));
  putreg8(BL616CL_CLIC_ATTR_EDGE_POSITIVE,
          bl616cl_clic_int_addr(BL616CL_IRQ_RAW_SDU_SOFT_RST,
                                BL616CL_CLICINT_ATTR_OFFSET));

  riscv_exception_attach();

#ifndef CONFIG_SUPPRESS_INTERRUPTS
  up_irq_enable();
#endif
}

/****************************************************************************
 * Name: up_enable_irq
 *
 * Description:
 *   Enable the interrupt specified by 'irq'
 *
 ****************************************************************************/

void up_enable_irq(int irq)
{
  int rawirq;

  if (irq < RISCV_IRQ_ASYNC)
    {
      return;
    }

  rawirq = bl616cl_irq_nuttx_to_raw(irq);
  if (!bl616cl_irq_raw_valid(rawirq))
    {
      ASSERT(false);
      return;
    }

  if (rawirq == BL616CL_IRQ_RAW_MSOFT)
    {
      SET_CSR(mie, MIE_MSIE);
    }
  else if (rawirq == BL616CL_IRQ_RAW_MTIME)
    {
      SET_CSR(mie, MIE_MTIE);
    }
  else if (rawirq == BL616CL_IRQ_RAW_MEXT)
    {
      SET_CSR(mie, MIE_MEIE);
    }

  bl616cl_clic_enable_raw(rawirq);
}

/****************************************************************************
 * Name: up_disable_irq
 *
 * Description:
 *   Disable the interrupt specified by 'irq'
 *
 ****************************************************************************/

void up_disable_irq(int irq)
{
  int rawirq;

  if (irq < RISCV_IRQ_ASYNC)
    {
      return;
    }

  rawirq = bl616cl_irq_nuttx_to_raw(irq);
  if (!bl616cl_irq_raw_valid(rawirq))
    {
      ASSERT(false);
      return;
    }

  if (rawirq == BL616CL_IRQ_RAW_MSOFT)
    {
      CLEAR_CSR(mie, MIE_MSIE);
    }
  else if (rawirq == BL616CL_IRQ_RAW_MTIME)
    {
      CLEAR_CSR(mie, MIE_MTIE);
    }
  else if (rawirq == BL616CL_IRQ_RAW_MEXT)
    {
      CLEAR_CSR(mie, MIE_MEIE);
    }

  bl616cl_clic_disable_raw(rawirq);
}

/****************************************************************************
 * Name: riscv_ack_irq
 *
 * Description:
 *   Acknowledge the IRQ
 *
 ****************************************************************************/

void riscv_ack_irq(int irq)
{
  int rawirq;

  if (irq < RISCV_IRQ_ASYNC)
    {
      return;
    }

  rawirq = bl616cl_irq_nuttx_to_raw(irq);
  bl616cl_clic_clear_pending_raw(rawirq);
}

/****************************************************************************
 * Name: up_irq_enable
 ****************************************************************************/

irqstate_t up_irq_enable(void)
{
  irqstate_t flags;

  SET_CSR(mie, MIE_MEIE);

  __asm__ __volatile__("csrrs %0, mstatus, %1\n"
                       : "=r"(flags)
                       : "r"(STATUS_IE)
                       : "memory");

  return flags;
}
