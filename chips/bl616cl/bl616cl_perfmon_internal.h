/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_perfmon_internal.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_PERFMON_INTERNAL_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_PERFMON_INTERNAL_H

#include <arch/chip/bl616cl_perfmon.h>
#include <arch/irq.h>

/* Select the E907 counter events. */

void bl616cl_perfmon_initialize(void);

/* riscv_dispatch_irq() tail: run riscv_doirq() and account for it. */

FAR void *bl616cl_perfmon_dispatch(int irq, FAR uintreg_t *regs);

#endif
