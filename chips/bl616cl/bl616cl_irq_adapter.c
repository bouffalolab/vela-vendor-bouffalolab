/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_irq_adapter.c
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

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/irq.h>

#include <arch/irq.h>

#include "bl616cl_lhal.h"
#include "bflb_irq.h"
#include "bl616cl_irq_internal.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bl616cl_irq_adapter_s
{
  irq_callback handler;
  void *arg;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bl616cl_irq_adapter_s g_bl616cl_irq_adapter
                                              [BL616CL_IRQ_CLIC_COUNT];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_raw_irq_valid
 *
 * Description:
 *   Check that a raw interrupt number lies in the CLIC range [0,
 *   BL616CL_IRQ_CLIC_COUNT).
 *
 * Input Parameters:
 *   irq - Raw CLIC interrupt number (SDK numbering)
 *
 * Returned Value:
 *   true if irq is a valid raw interrupt number; false otherwise.
 *
 ****************************************************************************/

static bool bl616cl_raw_irq_valid(int irq)
{
  return irq >= 0 && irq < BL616CL_IRQ_CLIC_COUNT;
}

/****************************************************************************
 * Name: bl616cl_lhal_interrupt
 *
 * Description:
 *   NuttX interrupt handler shared by all attached lhal ISRs. It calls the
 *   lhal handler registered in the adapter slot with the raw interrupt number
 *   and the registered argument.
 *
 * Input Parameters:
 *   irq - NuttX IRQ number
 *   context - Interrupt register context (unused)
 *   arg - Pointer to the adapter slot of this interrupt
 *
 * Returned Value:
 *   OK is always returned.
 *
 ****************************************************************************/

static int bl616cl_lhal_interrupt(int irq, void *context, void *arg)
{
  struct bl616cl_irq_adapter_s *adapter = arg;

  UNUSED(context);

  if (adapter != NULL && adapter->handler != NULL)
    {
      adapter->handler(bl616cl_irq_nuttx_to_raw(irq), adapter->arg);
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bflb_irq_initialize
 *
 * Description:
 *   Replaces the excluded lhal bflb_irq_initialize(). Nothing to do because
 *   the interrupt controller is set up by up_irqinitialize().
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bflb_irq_initialize(void)
{
}

/****************************************************************************
 * Name: bflb_irq_save
 *
 * Description:
 *   Replaces the excluded lhal bflb_irq_save(). Disable interrupts through
 *   up_irq_save().
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The previous interrupt state to pass to bflb_irq_restore().
 *
 ****************************************************************************/

uintptr_t bflb_irq_save(void)
{
  return up_irq_save();
}

/****************************************************************************
 * Name: bflb_irq_restore
 *
 * Description:
 *   Replaces the excluded lhal bflb_irq_restore(). Restore the interrupt
 *   state through up_irq_restore().
 *
 * Input Parameters:
 *   flags - Value returned by bflb_irq_save()
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bflb_irq_restore(uintptr_t flags)
{
  up_irq_restore((irqstate_t)flags);
}

/****************************************************************************
 * Name: bflb_irq_attach
 *
 * Description:
 *   Replaces the excluded lhal bflb_irq_attach(). Record the lhal handler and
 *   argument in the adapter slot of the raw interrupt and attach the common
 *   adapter handler to the matching NuttX IRQ.
 *
 * Input Parameters:
 *   irq - Raw CLIC interrupt number (SDK numbering)
 *   isr - lhal interrupt handler; must not be NULL
 *   arg - Argument passed to the handler
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *   -EINVAL - irq is out of range or isr is NULL.
 *   Other errors are returned by irq_attach().
 *
 ****************************************************************************/

int bflb_irq_attach(int irq, irq_callback isr, void *arg)
{
  int nuttx_irq;

  if (!bl616cl_raw_irq_valid(irq) || isr == NULL)
    {
      return -EINVAL;
    }

  nuttx_irq = bl616cl_irq_raw_to_nuttx(irq);
  g_bl616cl_irq_adapter[irq].handler = isr;
  g_bl616cl_irq_adapter[irq].arg = arg;

  return irq_attach(nuttx_irq, bl616cl_lhal_interrupt,
                    &g_bl616cl_irq_adapter[irq]);
}

/****************************************************************************
 * Name: bflb_irq_detach
 *
 * Description:
 *   Replaces the excluded lhal bflb_irq_detach(). Disable the interrupt,
 *   clear the adapter slot and detach the NuttX handler.
 *
 * Input Parameters:
 *   irq - Raw CLIC interrupt number (SDK numbering)
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *   -EINVAL - irq is out of range.
 *   Other errors are returned by irq_detach().
 *
 ****************************************************************************/

int bflb_irq_detach(int irq)
{
  if (!bl616cl_raw_irq_valid(irq))
    {
      return -EINVAL;
    }

  up_disable_irq(bl616cl_irq_raw_to_nuttx(irq));
  g_bl616cl_irq_adapter[irq].handler = NULL;
  g_bl616cl_irq_adapter[irq].arg = NULL;

  return irq_detach(bl616cl_irq_raw_to_nuttx(irq));
}

/****************************************************************************
 * Name: bflb_irq_enable
 *
 * Description:
 *   Replaces the excluded lhal bflb_irq_enable(). Enable the interrupt
 *   through up_enable_irq(). Out-of-range numbers are ignored.
 *
 * Input Parameters:
 *   irq - Raw CLIC interrupt number (SDK numbering)
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bflb_irq_enable(int irq)
{
  if (bl616cl_raw_irq_valid(irq))
    {
      up_enable_irq(bl616cl_irq_raw_to_nuttx(irq));
    }
}

/****************************************************************************
 * Name: bflb_irq_disable
 *
 * Description:
 *   Replaces the excluded lhal bflb_irq_disable(). Disable the interrupt
 *   through up_disable_irq(). Out-of-range numbers are ignored.
 *
 * Input Parameters:
 *   irq - Raw CLIC interrupt number (SDK numbering)
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bflb_irq_disable(int irq)
{
  if (bl616cl_raw_irq_valid(irq))
    {
      up_disable_irq(bl616cl_irq_raw_to_nuttx(irq));
    }
}

/****************************************************************************
 * Name: bflb_irq_set_pending
 *
 * Description:
 *   Replaces the excluded lhal bflb_irq_set_pending(). Set the CLIC pending
 *   bit of the interrupt.
 *
 * Input Parameters:
 *   irq - Raw CLIC interrupt number (SDK numbering)
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bflb_irq_set_pending(int irq)
{
  bl616cl_clic_set_pending_raw(irq);
}

/****************************************************************************
 * Name: bflb_irq_clear_pending
 *
 * Description:
 *   Replaces the excluded lhal bflb_irq_clear_pending(). Clear the CLIC
 *   pending bit of the interrupt.
 *
 * Input Parameters:
 *   irq - Raw CLIC interrupt number (SDK numbering)
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bflb_irq_clear_pending(int irq)
{
  bl616cl_clic_clear_pending_raw(irq);
}

/****************************************************************************
 * Name: bflb_irq_set_nlbits
 *
 * Description:
 *   Replaces the excluded lhal bflb_irq_set_nlbits(). Program the CLIC nlbits
 *   field.
 *
 * Input Parameters:
 *   nlbits - Number of level bits
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bflb_irq_set_nlbits(uint8_t nlbits)
{
  bl616cl_clic_set_nlbits(nlbits);
}

/****************************************************************************
 * Name: bflb_irq_set_priority
 *
 * Description:
 *   Replaces the excluded lhal bflb_irq_set_priority(). Set the CLIC
 *   preemption level and sub-priority of the interrupt.
 *
 * Input Parameters:
 *   irq - Raw CLIC interrupt number (SDK numbering)
 *   preemptprio - Preemption level
 *   subprio - Sub-priority
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bflb_irq_set_priority(int irq, uint8_t preemptprio, uint8_t subprio)
{
  bl616cl_clic_set_priority_raw(irq, preemptprio, subprio);
}
