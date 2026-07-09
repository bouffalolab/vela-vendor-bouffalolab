/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_irq_adapter.c
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

#include "bflb_irq.h"
#include "bl616cl_irq.h"
#include "chip.h"

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

static bool bl616cl_raw_irq_valid(int irq)
{
  return irq >= 0 && irq < BL616CL_IRQ_CLIC_COUNT;
}

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
 ****************************************************************************/

void bflb_irq_initialize(void)
{
}

/****************************************************************************
 * Name: bflb_irq_save
 ****************************************************************************/

uintptr_t bflb_irq_save(void)
{
  return up_irq_save();
}

/****************************************************************************
 * Name: bflb_irq_restore
 ****************************************************************************/

void bflb_irq_restore(uintptr_t flags)
{
  up_irq_restore((irqstate_t)flags);
}

/****************************************************************************
 * Name: bflb_irq_attach
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
 ****************************************************************************/

void bflb_irq_set_pending(int irq)
{
  bl616cl_clic_set_pending_raw(irq);
}

/****************************************************************************
 * Name: bflb_irq_clear_pending
 ****************************************************************************/

void bflb_irq_clear_pending(int irq)
{
  bl616cl_clic_clear_pending_raw(irq);
}

/****************************************************************************
 * Name: bflb_irq_set_nlbits
 ****************************************************************************/

void bflb_irq_set_nlbits(uint8_t nlbits)
{
  bl616cl_clic_set_nlbits(nlbits);
}

/****************************************************************************
 * Name: bflb_irq_set_priority
 ****************************************************************************/

void bflb_irq_set_priority(int irq, uint8_t preemptprio, uint8_t subprio)
{
  bl616cl_clic_set_priority_raw(irq, preemptprio, subprio);
}
