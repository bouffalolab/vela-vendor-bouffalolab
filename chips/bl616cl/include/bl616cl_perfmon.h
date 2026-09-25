/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/include/bl616cl_perfmon.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_PERFMON_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_PERFMON_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stddef.h>
#include <stdint.h>

#ifdef CONFIG_BL616CL_PERFMON

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* E907 events counted by the performance monitor.  The event numbers come
 * from the T-Head helpers in LHAL rv_hpm.h.
 */

enum bl616cl_perfmon_event_e
{
  BL616CL_PERFMON_ICACHE_ACCESS = 0,
  BL616CL_PERFMON_ICACHE_MISS,
  BL616CL_PERFMON_BRANCH_MISPREDICT,
  BL616CL_PERFMON_BRANCH,
  BL616CL_PERFMON_DCACHE_READ,
  BL616CL_PERFMON_DCACHE_READ_MISS,
  BL616CL_PERFMON_DCACHE_WRITE,
  BL616CL_PERFMON_DCACHE_WRITE_MISS,
  BL616CL_PERFMON_NEVENTS
};

/* All counts run from boot; take the difference of two reads. */

struct bl616cl_perfmon_counters_s
{
  uint64_t cycle;                          /* mcycle */
  uint64_t instret;                        /* minstret */
  uint64_t event[BL616CL_PERFMON_NEVENTS]; /* enum bl616cl_perfmon_event_e */
};

struct bl616cl_perfmon_irq_s
{
  uint32_t count;  /* Calls of riscv_doirq() for this IRQ or exception */
  uint64_t cycles; /* mcycle spent in them, trap entry and exit excluded */
};

#ifdef CONFIG_BL616CL_PERFMON_PCSAMPLE
struct bl616cl_perfmon_pcstat_s
{
  uint32_t samples; /* All ticks while sampling */
  uint32_t idle;    /* Ticks that interrupted the idle task */
  uint32_t outside; /* Busy ticks outside the histogram (RAM code) */
};
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* The perfmon application uses this public, configuration-gated interface;
 * the generic NuttX perf and IRQ monitors keep neither cache events nor
 * the total time of each interrupt.
 */

void bl616cl_perfmon_read(FAR struct bl616cl_perfmon_counters_s *counters);

/* Copy min(nirqs, NR_IRQS) entries, indexed by NuttX IRQ number, taken
 * with interrupts disabled.  Returns the number copied.
 */

int bl616cl_perfmon_irq_read(FAR struct bl616cl_perfmon_irq_s *irqs,
                             int nirqs);

#ifdef CONFIG_BL616CL_PERFMON_PCSAMPLE
/* Address range of the XIP code (.text), for sizing a histogram. */

void bl616cl_perfmon_text_range(FAR uintptr_t *start, FAR size_t *size);

/* On every machine timer interrupt that does not interrupt the idle task,
 * count the interrupted PC in hist[(pc - base) >> shift] (saturating at
 * UINT16_MAX) until stopped.  The caller owns hist and keeps it valid
 * until bl616cl_perfmon_pc_stop().  Returns -EBUSY if sampling already
 * runs and -EINVAL for bad arguments.
 */

int bl616cl_perfmon_pc_start(FAR uint16_t *hist, uintptr_t base,
                             size_t nbuckets, unsigned int shift);

/* Stop sampling and report the tick counts. */

void bl616cl_perfmon_pc_stop(FAR struct bl616cl_perfmon_pcstat_s *stat);
#endif

#endif /* CONFIG_BL616CL_PERFMON */
#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_PERFMON_H */
