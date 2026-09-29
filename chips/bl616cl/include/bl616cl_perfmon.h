/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/include/bl616cl_perfmon.h
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

/****************************************************************************
 * Name: bl616cl_perfmon_read
 *
 * Description:
 *   Read the 64-bit cycle and instruction counters and the eight event
 *   counters, using a high-low-high sequence so the result is consistent on
 *   RV32.
 *
 *   The perfmon application uses this public, configuration-gated interface;
 *   the generic NuttX perf and IRQ monitors keep neither cache events nor
 *   the total time of each interrupt.
 *
 * Input Parameters:
 *   counters - Location to receive the counter values
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_perfmon_read(struct bl616cl_perfmon_counters_s *counters);

/****************************************************************************
 * Name: bl616cl_perfmon_irq_read
 *
 * Description:
 *   Copy the per-IRQ interrupt count and accumulated cycles into a caller
 *   buffer with interrupts disabled.
 *
 *   The entries are indexed by NuttX IRQ number.
 *
 * Input Parameters:
 *   irqs - Array to receive the per-IRQ statistics
 *   nirqs - Number of entries in irqs; limited to NR_IRQS
 *
 * Returned Value:
 *   The number of entries filled.
 *
 ****************************************************************************/

int bl616cl_perfmon_irq_read(struct bl616cl_perfmon_irq_s *irqs,
                             int nirqs);

#ifdef CONFIG_BL616CL_PERFMON_PCSAMPLE

/****************************************************************************
 * Name: bl616cl_perfmon_text_range
 *
 * Description:
 *   Return the range of the kernel text section, from _stext to
 *   __bl616cl_text_end. Only built with CONFIG_BL616CL_PERFMON_PCSAMPLE.
 *
 *   The range is used for sizing a histogram.
 *
 * Input Parameters:
 *   start - Location to receive the text start address
 *   size - Location to receive the text size in bytes
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_perfmon_text_range(uintptr_t *start, size_t *size);

/****************************************************************************
 * Name: bl616cl_perfmon_pc_start
 *
 * Description:
 *   Start program counter sampling into a caller-supplied histogram. Each
 *   bucket covers 2^shift bytes starting at base. The statistics are reset.
 *   Only built with CONFIG_BL616CL_PERFMON_PCSAMPLE.
 *
 *   Sampling happens on every machine timer interrupt that does not interrupt
 *   the idle task; the interrupted PC is counted in
 *   hist[(pc - base) >> shift], saturating at UINT16_MAX, until stopped. The
 *   caller owns hist and keeps it valid until bl616cl_perfmon_pc_stop().
 *
 * Input Parameters:
 *   hist - Histogram of 16-bit buckets, nbuckets entries
 *   base - Address covered by the first bucket
 *   nbuckets - Number of buckets
 *   shift - Address shift per bucket, 1 to 16
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure:
 *   -EINVAL - hist is NULL, nbuckets is zero or shift is out of range.
 *   -EBUSY - sampling is already active.
 *
 ****************************************************************************/

int bl616cl_perfmon_pc_start(uint16_t *hist, uintptr_t base,
                             size_t nbuckets, unsigned int shift);

/****************************************************************************
 * Name: bl616cl_perfmon_pc_stop
 *
 * Description:
 *   Stop program counter sampling and return the sample statistics. Only
 *   built with CONFIG_BL616CL_PERFMON_PCSAMPLE.
 *
 * Input Parameters:
 *   stat - Location to receive the sample statistics
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_perfmon_pc_stop(struct bl616cl_perfmon_pcstat_s *stat);

#endif

#endif /* CONFIG_BL616CL_PERFMON */
#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_PERFMON_H */
