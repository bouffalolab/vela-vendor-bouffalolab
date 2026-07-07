/****************************************************************************
 * vendor/bouffalolab/chip/bl616cl/include/irq.h
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

/* This file should never be included directly but, rather, only indirectly
 * through nuttx/irq.h
 */

#ifndef __VENDOR_BOUFFALOLAB_CHIP_BL616CL_INCLUDE_IRQ_H
#define __VENDOR_BOUFFALOLAB_CHIP_BL616CL_INCLUDE_IRQ_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

/* Include chip-specific IRQ definitions (including IRQ numbers) */

#include <nuttx/config.h>

#include <sys/types.h>

#include <arch/chip/irq.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_IRQ_FIRST 16

#define BL616CL_IRQ_BMX_MCU_BUS_ERR      (BL616CL_IRQ_FIRST + 0)
#define BL616CL_IRQ_UART0                (BL616CL_IRQ_FIRST + 28)
#define BL616CL_IRQ_WDT1                 (BL616CL_IRQ_FIRST + 66)

#define NR_IRQS                          (BL616CL_IRQ_WDT1 + 1)

/****************************************************************************
 * Public Types
 ****************************************************************************/

/****************************************************************************
 * Public Types
 ****************************************************************************/

/****************************************************************************
 * Inline functions
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

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
 * Inline Functions
 ****************************************************************************/

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __VENDOR_BOUFFALOLAB_CHIP_BL616CL_INCLUDE_IRQ_H */
