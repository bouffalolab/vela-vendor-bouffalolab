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

/* Select the E907 counter events. */

void bl616cl_perfmon_initialize(void);

/* riscv_dispatch_irq() tail: run riscv_doirq() and account for it. */

void *bl616cl_perfmon_dispatch(int irq, uintreg_t *regs);

#endif
