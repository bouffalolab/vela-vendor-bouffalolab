/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_perfmon_internal.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_PERFMON_INTERNAL_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_PERFMON_INTERNAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <arch/chip/bl616cl_perfmon.h>
#include <arch/irq.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_perfmon_initialize
 *
 * Description:
 *   Select the events counted by the hardware performance counters: I-cache
 *   access and miss, conditional branch and mispredict, and D-cache read and
 *   write access and miss. Event numbers: see enum bl616cl_perfmon_event_e
 *   and rv_hpm.h.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_perfmon_initialize(void);

/****************************************************************************
 * Name: bl616cl_perfmon_dispatch
 *
 * Description:
 *   Interrupt dispatch wrapper called from riscv_dispatch_irq(). Take a PC
 *   sample on the machine timer interrupt when sampling is active, run the
 *   interrupt through riscv_doirq() and accumulate its count and mcycle
 *   cycles per IRQ.
 *
 * Input Parameters:
 *   irq - NuttX IRQ number
 *   regs - Saved register context of the interrupted code
 *
 * Returned Value:
 *   The register context to restore, as returned by riscv_doirq().
 *
 ****************************************************************************/

void *bl616cl_perfmon_dispatch(int irq, uintreg_t *regs);

#endif
