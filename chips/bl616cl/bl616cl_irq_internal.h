/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_irq_internal.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_IRQ_INTERNAL_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_IRQ_INTERNAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/arch.h>
#include <nuttx/config.h>
#include <nuttx/irq.h>

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Number of raw CLIC slots, excluding NuttX's exception-number prefix. */

#define BL616CL_IRQ_CLIC_COUNT (NR_IRQS - BL616CL_RISCV_IRQ_ASYNC)

#ifndef __ASSEMBLY__

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_irq_raw_to_nuttx
 *
 * Description:
 *   Convert a raw SDK/CLIC interrupt number to the NuttX IRQ number by adding
 *   RISCV_IRQ_ASYNC.
 *
 *   LHAL uses raw CLIC indices, while NuttX reserves RISCV_IRQ_ASYNC entries
 *   before asynchronous interrupts. Convert only at the adapter boundary.
 *
 * Input Parameters:
 *   irq - Raw CLIC interrupt number (SDK numbering)
 *
 * Returned Value:
 *   The NuttX IRQ number. The input is not range checked.
 *
 ****************************************************************************/

int bl616cl_irq_raw_to_nuttx(int irq);

/****************************************************************************
 * Name: bl616cl_irq_nuttx_to_raw
 *
 * Description:
 *   Convert a NuttX IRQ number to the raw SDK/CLIC interrupt number by
 *   subtracting RISCV_IRQ_ASYNC.
 *
 * Input Parameters:
 *   irq - NuttX IRQ number
 *
 * Returned Value:
 *   The raw interrupt number. The input is not range checked.
 *
 ****************************************************************************/

int bl616cl_irq_nuttx_to_raw(int irq);

/****************************************************************************
 * Name: bl616cl_clic_enable_raw
 *
 * Description:
 *   Set the CLIC interrupt-enable bit of a raw interrupt. Out-of-range
 *   numbers are ignored.
 *
 * Input Parameters:
 *   irq - Raw CLIC interrupt number (SDK numbering)
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_clic_enable_raw(int irq);

/****************************************************************************
 * Name: bl616cl_clic_disable_raw
 *
 * Description:
 *   Clear the CLIC interrupt-enable bit of a raw interrupt. Out-of-range
 *   numbers are ignored.
 *
 * Input Parameters:
 *   irq - Raw CLIC interrupt number (SDK numbering)
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_clic_disable_raw(int irq);

/****************************************************************************
 * Name: bl616cl_clic_set_pending_raw
 *
 * Description:
 *   Set the CLIC pending bit of a raw interrupt, which triggers it in
 *   software. Out-of-range numbers are ignored.
 *
 * Input Parameters:
 *   irq - Raw CLIC interrupt number (SDK numbering)
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_clic_set_pending_raw(int irq);

/****************************************************************************
 * Name: bl616cl_clic_clear_pending_raw
 *
 * Description:
 *   Clear the CLIC pending bit of a raw interrupt. Out-of-range numbers are
 *   ignored.
 *
 * Input Parameters:
 *   irq - Raw CLIC interrupt number (SDK numbering)
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_clic_clear_pending_raw(int irq);

/****************************************************************************
 * Name: bl616cl_clic_set_nlbits
 *
 * Description:
 *   Program the nlbits field of cliccfg, which splits the clicintctl bits
 *   between preemption level and sub-priority.
 *
 * Input Parameters:
 *   nlbits - Number of level bits; only the low 4 bits are used
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_clic_set_nlbits(uint8_t nlbits);

/****************************************************************************
 * Name: bl616cl_clic_set_priority_raw
 *
 * Description:
 *   Update the clicintctl priority of a raw interrupt. The preemption level
 *   is placed in the upper nlbits bits (nlbits read back from cliccfg, capped
 *   at 8) and the sub-priority in the bits above the low nibble, which is
 *   preserved. Out-of-range numbers are ignored.
 *
 * Input Parameters:
 *   irq - Raw CLIC interrupt number (SDK numbering)
 *   preemptprio - Preemption level
 *   subprio - Sub-priority, masked to the bits left by nlbits
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_clic_set_priority_raw(int irq, uint8_t preemptprio,
                                   uint8_t subprio);

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_IRQ_INTERNAL_H */
